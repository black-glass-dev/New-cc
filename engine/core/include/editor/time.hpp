#pragma once

#include <cstdint>
#include <stdexcept>

namespace editor {

// One tick represents numerator / denominator seconds. Values are normalized.
struct TimeBase final {
    std::int64_t numerator{1};
    std::int64_t denominator{1};

    TimeBase(std::int64_t numerator, std::int64_t denominator);
    friend bool operator==(const TimeBase&, const TimeBase&) = default;
};

// An integer timestamp tied to an explicit timebase (never floating-point seconds).
struct Timestamp final {
    std::int64_t ticks{0};
    TimeBase time_base{1, 1};

    [[nodiscard]] Timestamp rescaled(TimeBase destination) const;
    friend bool operator==(const Timestamp& lhs, const Timestamp& rhs) noexcept;
    friend bool operator<(const Timestamp& lhs, const Timestamp& rhs) noexcept;
};

[[nodiscard]] Timestamp operator+(Timestamp lhs, Timestamp rhs);
[[nodiscard]] Timestamp operator-(Timestamp lhs, Timestamp rhs);

} // namespace editor
