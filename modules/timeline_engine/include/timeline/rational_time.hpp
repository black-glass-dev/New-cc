#pragma once

#include "timeline/color_hook.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace owncut::timeline {

enum class RoundingMode : uint8_t {
    NearestHalfAwayFromZero = 0,
    Floor = 1,
    Ceil = 2,
    Truncate = 3
};

struct FrameRate final {
    int64_t num{30};
    int64_t den{1};

    constexpr FrameRate() noexcept = default;
    FrameRate(int64_t fps_num, int64_t fps_den);

    static FrameRate fps_23_976() noexcept { return {24000, 1001}; }
    static FrameRate fps_24() noexcept { return {24, 1}; }
    static FrameRate fps_25() noexcept { return {25, 1}; }
    static FrameRate fps_29_97() noexcept { return {30000, 1001}; }
    static FrameRate fps_30() noexcept { return {30, 1}; }
    static FrameRate fps_50() noexcept { return {50, 1}; }
    static FrameRate fps_59_94() noexcept { return {60000, 1001}; }
    static FrameRate fps_60() noexcept { return {60, 1}; }
    static FrameRate fps_120() noexcept { return {120, 1}; }

    [[nodiscard]] double to_double() const noexcept {
        return static_cast<double>(num) / static_cast<double>(den);
    }

    [[nodiscard]] bool is_drop_frame_capable() const noexcept {
        return (num == 30000 && den == 1001) || (num == 60000 && den == 1001);
    }

    // Duration of one frame in seconds: den / num
    [[nodiscard]] int64_t nominal_fps_int() const noexcept;

    friend bool operator==(const FrameRate&, const FrameRate&) noexcept = default;
};

// Exact 64-bit integer rational time in seconds (value = num / den seconds).
// Normalized with den > 0 and gcd(|num|, den) == 1.
class RationalTime final {
public:
    constexpr RationalTime() noexcept : num_(0), den_(1) {}
    RationalTime(int64_t num, int64_t den);

    static RationalTime zero() noexcept { return RationalTime(); }
    static RationalTime from_frames(int64_t frame_index, FrameRate rate);
    static RationalTime from_milliseconds(int64_t ms);
    static RationalTime from_samples(int64_t sample_index, int64_t sample_rate);
    static RationalTime from_seconds_approx(double seconds, int64_t precision_den = 1'000'000);
    static std::optional<RationalTime> from_timecode(std::string_view tc, FrameRate rate, bool drop_frame = false);

    [[nodiscard]] int64_t num() const noexcept { return num_; }
    [[nodiscard]] int64_t den() const noexcept { return den_; }
    [[nodiscard]] RationalTime64 to_c64() const noexcept { return {num_, den_}; }
    static RationalTime from_c64(RationalTime64 raw) { return RationalTime(raw.num, raw.den); }

    [[nodiscard]] bool is_zero() const noexcept { return num_ == 0; }
    [[nodiscard]] bool is_negative() const noexcept { return num_ < 0; }
    [[nodiscard]] RationalTime abs() const;

    [[nodiscard]] int64_t to_frames(FrameRate rate, RoundingMode mode = RoundingMode::NearestHalfAwayFromZero) const;
    [[nodiscard]] RationalTime quantized_to_frame(FrameRate rate, RoundingMode mode = RoundingMode::NearestHalfAwayFromZero) const;
    [[nodiscard]] int64_t to_milliseconds(RoundingMode mode = RoundingMode::NearestHalfAwayFromZero) const;
    [[nodiscard]] int64_t to_samples(int64_t sample_rate, RoundingMode mode = RoundingMode::NearestHalfAwayFromZero) const;
    [[nodiscard]] double to_seconds() const noexcept {
        return static_cast<double>(num_) / static_cast<double>(den_);
    }

    // Formats SMPTE NDF (HH:MM:SS:FF) or Drop-Frame (HH:MM:SS;FF for 29.97 / 59.94).
    [[nodiscard]] std::string to_timecode(FrameRate rate, bool drop_frame = false) const;

    [[nodiscard]] RationalTime operator-() const;
    RationalTime& operator+=(const RationalTime& rhs) { *this = *this + rhs; return *this; }
    RationalTime& operator-=(const RationalTime& rhs) { *this = *this - rhs; return *this; }
    friend RationalTime operator+(const RationalTime& lhs, const RationalTime& rhs);
    friend RationalTime operator-(const RationalTime& lhs, const RationalTime& rhs);
    friend RationalTime operator*(const RationalTime& lhs, int64_t scalar);
    friend RationalTime operator*(int64_t scalar, const RationalTime& rhs);
    friend RationalTime operator/(const RationalTime& lhs, int64_t divisor);
    friend RationalTime scale_rational(const RationalTime& value, int64_t mul_num, int64_t mul_den);

    friend bool operator==(const RationalTime& lhs, const RationalTime& rhs) noexcept {
        return lhs.num_ == rhs.num_ && lhs.den_ == rhs.den_;
    }
    friend std::strong_ordering operator<=>(const RationalTime& lhs, const RationalTime& rhs) noexcept;

private:
    int64_t num_;
    int64_t den_;
};

struct TimeRange final {
    RationalTime start;
    RationalTime duration;

    [[nodiscard]] RationalTime end() const { return start + duration; }
    [[nodiscard]] bool is_empty() const noexcept { return duration.num() <= 0; }
    [[nodiscard]] bool contains(const RationalTime& t) const {
        return t >= start && t < end();
    }
    [[nodiscard]] bool overlaps(const TimeRange& other) const {
        if (is_empty() || other.is_empty()) return false;
        return start < other.end() && other.start < end();
    }
    [[nodiscard]] std::optional<TimeRange> intersection(const TimeRange& other) const;

    friend bool operator==(const TimeRange&, const TimeRange&) noexcept = default;
};

} // namespace owncut::timeline
