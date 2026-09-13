#include "capture/linux/portal.hpp"
#include <algorithm>
#include <chrono>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <stdexcept>
#include <thread>
namespace larp {
namespace {
constexpr auto destination = "org.freedesktop.portal.Desktop";
constexpr auto desktop = "/org/freedesktop/portal/desktop";
constexpr auto screencast = "org.freedesktop.portal.ScreenCast";
struct Unref {
    template <class T> void operator()(T *p) const {
        g_object_unref(p);
    }
};
template <class T> using Object = std::unique_ptr<T, Unref>;
using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;
Variant own(GVariant *value) {
    return {value, g_variant_unref};
}
[[noreturn]] void error(GError *value) {
    const std::string message = value ? value->message : "portal call failed";
    if (value)
        g_error_free(value);
    throw std::runtime_error(message);
}
struct Subscription {
    GDBusConnection *connection = nullptr;
    guint id = 0;
    ~Subscription() {
        if (id)
            g_dbus_connection_signal_unsubscribe(connection, id);
    }
};
struct MainContext {
    GMainContext *value = g_main_context_new();
    MainContext() {
        g_main_context_push_thread_default(value);
    }
    ~MainContext() {
        g_main_context_pop_thread_default(value);
        g_main_context_unref(value);
    }
};
} // namespace
struct PortalSession::Impl {
    MainContext context;
    Object<GDBusConnection> bus;
    std::string session, target_serial;
    std::uint32_t target_node = 0;
    bool is_closed = false;
    Subscription closure;
    Impl() {
        GError *failure = nullptr;
        bus.reset(g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &failure));
        if (!bus)
            error(failure);
    }
    Variant call(const char *path, const char *interface, const char *method,
                 GVariant *parameters) {
        GError *failure = nullptr;
        auto result = own(
            g_dbus_connection_call_sync(bus.get(), destination, path, interface, method, parameters,
                                        nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &failure));
        if (!result)
            error(failure);
        return result;
    }
    void close_path(const std::string &path, const char *interface) noexcept {
        if (path.empty())
            return;
        auto result = own(g_dbus_connection_call_sync(
            bus.get(), destination, path.c_str(), interface, "Close", nullptr, nullptr,
            G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr));
    }
    ~Impl() {
        close_path(session, "org.freedesktop.portal.Session");
    }
    Variant request(const char *method, const char *token, GVariant *parameters) {
        std::string sender = g_dbus_connection_get_unique_name(bus.get());
        sender.erase(0, 1);
        std::replace(sender.begin(), sender.end(), '.', '_');
        const std::string path = std::string(desktop) + "/request/" + sender + "/" + token;
        struct Response {
            bool done = false;
            std::uint32_t status = 2;
            Variant value = own(nullptr);
        } response;
        Subscription subscription{
            bus.get(), g_dbus_connection_signal_subscribe(
                           bus.get(), destination, "org.freedesktop.portal.Request", "Response",
                           path.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                           [](GDBusConnection *, const gchar *, const gchar *, const gchar *,
                              const gchar *, GVariant *value, gpointer data) {
                               auto &result = *static_cast<Response *>(data);
                               if (!g_variant_is_of_type(value, G_VARIANT_TYPE("(ua{sv})"))) {
                                   result.done = true;
                                   return;
                               }
                               GVariant *dictionary = nullptr;
                               g_variant_get(value, "(u@a{sv})", &result.status, &dictionary);
                               result.value = own(dictionary);
                               result.done = true;
                           },
                           &response, nullptr)};
        try {
            const auto handle = call(desktop, screencast, method, parameters);
            const char *returned = nullptr;
            if (!g_variant_is_of_type(handle.get(), G_VARIANT_TYPE("(o)")))
                throw std::runtime_error("invalid portal handle");
            g_variant_get(handle.get(), "(&o)", &returned);
            if (path != returned)
                throw std::runtime_error("portal returned an unexpected request path");
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (!response.done && std::chrono::steady_clock::now() < until) {
                for (unsigned i = 0; i < 32 && g_main_context_iteration(context.value, FALSE);
                     ++i) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!response.done)
                throw std::runtime_error("screen selection timed out after 60 seconds");
            if (response.status != 0)
                throw std::runtime_error("screen selection cancelled or denied");
            return std::move(response.value);
        } catch (...) {
            close_path(path, "org.freedesktop.portal.Request");
            throw;
        }
    }
    void start() {
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "handle_token",
                              g_variant_new_string("larp_create"));
        g_variant_builder_add(&options, "{sv}", "session_handle_token",
                              g_variant_new_string("larp_session"));
        auto created = request("CreateSession", "larp_create", g_variant_new("(a{sv})", &options));
        const char *path = nullptr;
        if (!g_variant_lookup(created.get(), "session_handle", "&s", &path) ||
            !g_variant_is_object_path(path))
            throw std::runtime_error("portal omitted the capture session");
        session = path;
        closure.connection = bus.get();
        closure.id = g_dbus_connection_signal_subscribe(
            bus.get(), destination, "org.freedesktop.portal.Session", "Closed", session.c_str(),
            nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
               GVariant *, gpointer data) { static_cast<Impl *>(data)->is_closed = true; },
            this, nullptr);
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "handle_token",
                              g_variant_new_string("larp_select"));
        g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(1));
        g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(FALSE));
        request("SelectSources", "larp_select",
                g_variant_new("(oa{sv})", session.c_str(), &options));
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string("larp_start"));
        auto started = request("Start", "larp_start",
                               g_variant_new("(osa{sv})", session.c_str(), "", &options));
        auto streams =
            own(g_variant_lookup_value(started.get(), "streams", G_VARIANT_TYPE("a(ua{sv})")));
        if (!streams || g_variant_n_children(streams.get()) != 1)
            throw std::runtime_error("expected one screen stream");
        auto stream = own(g_variant_get_child_value(streams.get(), 0));
        GVariant *properties = nullptr;
        g_variant_get(stream.get(), "(u@a{sv})", &target_node, &properties);
        auto props = own(properties);
        guint64 serial_value = 0;
        if (g_variant_lookup(props.get(), "pipewire-serial", "t", &serial_value))
            target_serial = std::to_string(serial_value);
    }
};
PortalSession::PortalSession() : impl_(std::make_unique<Impl>()) {
    impl_->start();
}
PortalSession::~PortalSession() = default;
int PortalSession::open_remote() {
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    GUnixFDList *list = nullptr;
    GError *failure = nullptr;
    auto result = own(g_dbus_connection_call_with_unix_fd_list_sync(
        impl_->bus.get(), destination, desktop, screencast, "OpenPipeWireRemote",
        g_variant_new("(oa{sv})", impl_->session.c_str(), &options), G_VARIANT_TYPE("(h)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &list, nullptr, &failure));
    Object<GUnixFDList> descriptors(list);
    if (!result)
        error(failure);
    gint index = -1;
    g_variant_get(result.get(), "(h)", &index);
    if (!descriptors)
        throw std::runtime_error("portal omitted PipeWire descriptor");
    const int fd = g_unix_fd_list_get(descriptors.get(), index, &failure);
    if (fd < 0)
        error(failure);
    return fd;
}
std::uint32_t PortalSession::node() const {
    return impl_->target_node;
}
std::string PortalSession::serial() const {
    return impl_->target_serial;
}
bool PortalSession::closed() {
    for (unsigned i = 0; i < 32 && g_main_context_iteration(impl_->context.value, FALSE); ++i) {
    }
    return impl_->is_closed || g_dbus_connection_is_closed(impl_->bus.get());
}
} // namespace larp
