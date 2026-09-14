#include <cstdio>
#include <dlfcn.h>
#include <memory>
struct LibraryDelete {
    void operator()(void *library) const {
        dlclose(library);
    }
};
int main() {
    std::unique_ptr<void, LibraryDelete> library(dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL));
    if (!library) {
        std::fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
    auto init = reinterpret_cast<int (*)(unsigned)>(dlsym(library.get(), "cuInit"));
    if (!init)
        return 1;
    const auto result = init(0);
    std::fprintf(stderr, "cuInit: %d\n", result);
    return result == 0 ? 0 : 1;
}
