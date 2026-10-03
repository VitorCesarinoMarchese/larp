#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
namespace larp {
inline constexpr std::size_t probe_size = 32, feedback_size = 96;
struct Probe {
    std::uint64_t session, sequence, sent_us;
    bool operator==(const Probe &) const = default;
};
struct FeedbackCounters {
    std::uint64_t expected = 0, missing = 0, completed = 0, dropped = 0, skipped = 0, corrupt = 0,
                  bytes = 0;
    std::array<std::uint64_t, 7> values() const {
        return {expected, missing, completed, dropped, skipped, corrupt, bytes};
    }
};
struct Feedback {
    Probe probe;
    std::uint64_t received_us;
    FeedbackCounters counters;
};
namespace feedback_detail {
inline void put(std::span<std::byte> out, std::size_t offset, std::uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i)
        out[offset + i] = std::byte((value >> ((size - i - 1) * 8)) & 255);
}
inline std::uint64_t get(std::span<const std::byte> in, std::size_t offset, unsigned size) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = (value << 8) | std::to_integer<unsigned>(in[offset + i]);
    return value;
}
inline void header(std::span<std::byte> out, Probe probe, unsigned kind) {
    put(out, 0, 0x4c414642, 4);
    put(out, 4, 1, 2);
    put(out, 6, kind, 2);
    put(out, 8, probe.session, 8);
    put(out, 16, probe.sequence, 8);
    put(out, 24, probe.sent_us, 8);
}
inline bool header_valid(std::span<const std::byte> in, unsigned kind, std::size_t size) {
    return in.size() == size && get(in, 0, 4) == 0x4c414642 && get(in, 4, 2) == 1 &&
           get(in, 6, 2) == kind && get(in, 8, 8) && get(in, 16, 8);
}
inline Probe probe(std::span<const std::byte> in) {
    return {get(in, 8, 8), get(in, 16, 8), get(in, 24, 8)};
}
} // namespace feedback_detail
inline bool is_feedback(std::span<const std::byte> in) {
    return in.size() >= 4 && feedback_detail::get(in, 0, 4) == 0x4c414642;
}
inline std::array<std::byte, probe_size> encode_probe(Probe probe) {
    std::array<std::byte, probe_size> out{};
    feedback_detail::header(out, probe, 1);
    return out;
}
inline std::optional<Probe> decode_probe(std::span<const std::byte> in) {
    if (!feedback_detail::header_valid(in, 1, probe_size))
        return {};
    return feedback_detail::probe(in);
}
inline std::array<std::byte, feedback_size> encode_feedback(const Feedback &report) {
    std::array<std::byte, feedback_size> out{};
    feedback_detail::header(out, report.probe, 2);
    feedback_detail::put(out, 32, report.received_us, 8);
    const auto values = report.counters.values();
    for (std::size_t i = 0; i < values.size(); ++i)
        feedback_detail::put(out, 40 + i * 8, values[i], 8);
    return out;
}
inline std::optional<Feedback> decode_feedback(std::span<const std::byte> in) {
    using namespace feedback_detail;
    if (!header_valid(in, 2, feedback_size))
        return {};
    Feedback result{probe(in),
                    get(in, 32, 8),
                    {get(in, 40, 8), get(in, 48, 8), get(in, 56, 8), get(in, 64, 8), get(in, 72, 8),
                     get(in, 80, 8), get(in, 88, 8)}};
    const auto &c = result.counters;
    for (auto value : c.values())
        if (value > (std::uint64_t{1} << 60))
            return {};
    if (c.missing > c.expected || c.corrupt > c.completed || c.completed + c.dropped > c.expected)
        return {};
    return result;
}
} // namespace larp
