#include "timeline/rational_time.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace owncut::timeline {
namespace {
__extension__ using Wide = __int128;

int64_t checked_narrow(Wide v) {
    if (v < static_cast<Wide>(std::numeric_limits<int64_t>::min()) ||
        v > static_cast<Wide>(std::numeric_limits<int64_t>::max())) {
        throw std::overflow_error("RationalTime 64-bit integer overflow");
    }
    return static_cast<int64_t>(v);
}

Wide abs_wide(Wide v) noexcept { return v < 0 ? -v : v; }

Wide gcd_wide(Wide a, Wide b) noexcept {
    a = abs_wide(a);
    b = abs_wide(b);
    while (b != 0) {
        Wide t = a % b;
        a = b;
        b = t;
    }
    return a == 0 ? 1 : a;
}

RationalTime normalize_wide(Wide n, Wide d) {
    if (d == 0) {
        throw std::invalid_argument("RationalTime denominator cannot be zero");
    }
    if (d < 0) {
        n = -n;
        d = -d;
    }
    if (n == 0) {
        return RationalTime(0, 1);
    }
    const Wide g = gcd_wide(n, d);
    return RationalTime(checked_narrow(n / g), checked_narrow(d / g));
}

int64_t divide_round(Wide num, Wide den, RoundingMode mode) {
    if (den <= 0) {
        throw std::invalid_argument("divide_round requires positive denominator");
    }
    if (num == 0) return 0;

    switch (mode) {
        case RoundingMode::Truncate:
            return checked_narrow(num / den);
        case RoundingMode::Floor: {
            Wide q = num / den;
            Wide r = num % den;
            if (r != 0 && num < 0) --q;
            return checked_narrow(q);
        }
        case RoundingMode::Ceil: {
            Wide q = num / den;
            Wide r = num % den;
            if (r != 0 && num > 0) ++q;
            return checked_narrow(q);
        }
        case RoundingMode::NearestHalfAwayFromZero: {
            const Wide mag = abs_wide(num);
            const Wide rounded = (mag + den / 2) / den;
            return checked_narrow(num < 0 ? -rounded : rounded);
        }
    }
    return 0;
}

} // namespace

FrameRate::FrameRate(int64_t fps_num, int64_t fps_den) {
    if (fps_num <= 0 || fps_den <= 0) {
        throw std::invalid_argument("FrameRate terms must be positive");
    }
    const int64_t g = std::gcd(fps_num, fps_den);
    num = fps_num / g;
    den = fps_den / g;
}

int64_t FrameRate::nominal_fps_int() const noexcept {
    if (den <= 0) return 30;
    return (num + den / 2) / den;
}

RationalTime::RationalTime(int64_t num, int64_t den) {
    if (den == 0) {
        throw std::invalid_argument("RationalTime denominator cannot be zero");
    }
    if (num == 0) {
        num_ = 0;
        den_ = 1;
        return;
    }
    Wide wn = num;
    Wide wd = den;
    if (wd < 0) {
        wn = -wn;
        wd = -wd;
    }
    const Wide g = gcd_wide(wn, wd);
    num_ = checked_narrow(wn / g);
    den_ = checked_narrow(wd / g);
}

RationalTime RationalTime::from_frames(int64_t frame_index, FrameRate rate) {
    return RationalTime(checked_narrow(static_cast<Wide>(frame_index) * rate.den), rate.num);
}

RationalTime RationalTime::from_milliseconds(int64_t ms) {
    return RationalTime(ms, 1000);
}

RationalTime RationalTime::from_samples(int64_t sample_index, int64_t sample_rate) {
    if (sample_rate <= 0) {
        throw std::invalid_argument("sample_rate must be positive");
    }
    return RationalTime(sample_index, sample_rate);
}

RationalTime RationalTime::from_seconds_approx(double seconds, int64_t precision_den) {
    if (!std::isfinite(seconds) || precision_den <= 0) {
        throw std::invalid_argument("Invalid seconds or precision_den");
    }
    const double scaled = std::llround(seconds * static_cast<double>(precision_den));
    if (!std::isfinite(scaled) ||
        scaled < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        scaled > static_cast<double>(std::numeric_limits<int64_t>::max())) {
        throw std::overflow_error("seconds conversion overflow");
    }
    return RationalTime(static_cast<int64_t>(scaled), precision_den);
}

RationalTime RationalTime::abs() const {
    if (num_ == std::numeric_limits<int64_t>::min()) {
        throw std::overflow_error("RationalTime::abs overflow");
    }
    return RationalTime(num_ < 0 ? -num_ : num_, den_);
}

int64_t RationalTime::to_frames(FrameRate rate, RoundingMode mode) const {
    const Wide n = static_cast<Wide>(num_) * rate.num;
    const Wide d = static_cast<Wide>(den_) * rate.den;
    return divide_round(n, d, mode);
}

RationalTime RationalTime::quantized_to_frame(FrameRate rate, RoundingMode mode) const {
    return from_frames(to_frames(rate, mode), rate);
}

int64_t RationalTime::to_milliseconds(RoundingMode mode) const {
    return divide_round(static_cast<Wide>(num_) * 1000, static_cast<Wide>(den_), mode);
}

int64_t RationalTime::to_samples(int64_t sample_rate, RoundingMode mode) const {
    if (sample_rate <= 0) {
        throw std::invalid_argument("sample_rate must be positive");
    }
    return divide_round(static_cast<Wide>(num_) * sample_rate, static_cast<Wide>(den_), mode);
}

std::string RationalTime::to_timecode(FrameRate rate, bool drop_frame) const {
    const bool neg = num_ < 0;
    const RationalTime mag = neg ? -*this : *this;
    const int64_t nominal = std::max<int64_t>(1, rate.nominal_fps_int());
    const int64_t total_frames = mag.to_frames(rate, RoundingMode::NearestHalfAwayFromZero);
    const bool use_df = drop_frame && rate.is_drop_frame_capable();

    int64_t display_frames = total_frames;
    if (use_df) {
        const int64_t drop_count = (nominal == 60) ? 4 : 2; // 2 for 29.97, 4 for 59.94
        const int64_t frames_per_10_min = nominal * 600 - drop_count * 9;
        const int64_t frames_per_min = nominal * 60 - drop_count;
        const int64_t d10 = total_frames / frames_per_10_min;
        const int64_t m_rem = total_frames % frames_per_10_min;
        int64_t added = drop_count * 9 * d10;
        if (m_rem >= drop_count) {
            added += drop_count * ((m_rem - drop_count) / frames_per_min);
        }
        display_frames = total_frames + added;
    }

    const int64_t ff = display_frames % nominal;
    const int64_t total_sec = display_frames / nominal;
    const int64_t ss = total_sec % 60;
    const int64_t mm = (total_sec / 60) % 60;
    const int64_t hh = total_sec / 3600;
    char sep = use_df ? ';' : ':';
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%02lld:%02lld:%02lld%c%02lld",
                  neg ? "-" : "",
                  static_cast<long long>(hh),
                  static_cast<long long>(mm),
                  static_cast<long long>(ss),
                  sep,
                  static_cast<long long>(ff));
    return std::string(buf);
}

std::optional<RationalTime> RationalTime::from_timecode(std::string_view tc, FrameRate rate, bool drop_frame) {
    if (tc.empty()) return std::nullopt;
    bool neg = false;
    if (tc.front() == '-') {
        neg = true;
        tc.remove_prefix(1);
    }
    int parts[4]{0, 0, 0, 0};
    int part_idx = 0;
    bool saw_semicolon = false;
    int64_t accum = 0;
    bool has_digit = false;
    for (char c : tc) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            accum = accum * 10 + (c - '0');
            if (accum > 1000000) return std::nullopt;
            has_digit = true;
        } else if (c == ':' || c == ';') {
            if (!has_digit || part_idx >= 3) return std::nullopt;
            if (c == ';') saw_semicolon = true;
            parts[part_idx++] = static_cast<int>(accum);
            accum = 0;
            has_digit = false;
        } else {
            return std::nullopt;
        }
    }
    if (!has_digit || part_idx != 3) return std::nullopt;
    parts[3] = static_cast<int>(accum);

    const int64_t nominal = std::max<int64_t>(1, rate.nominal_fps_int());
    const int64_t hh = parts[0], mm = parts[1], ss = parts[2], ff = parts[3];
    if (mm < 0 || mm >= 60 || ss < 0 || ss >= 60 || ff < 0 || ff >= nominal) return std::nullopt;

    const bool use_df = (drop_frame || saw_semicolon) && rate.is_drop_frame_capable();
    int64_t frame_num = ((hh * 3600) + (mm * 60) + ss) * nominal + ff;
    if (use_df) {
        const int64_t drop_count = (nominal == 60) ? 4 : 2;
        if (ss == 0 && (mm % 10) != 0 && ff < drop_count) {
            return std::nullopt; // Invalid dropped frame label in SMPTE DF
        }
        const int64_t total_minutes = hh * 60 + mm;
        frame_num -= drop_count * (total_minutes - total_minutes / 10);
    }
    RationalTime out = from_frames(neg ? -frame_num : frame_num, rate);
    return out;
}

RationalTime RationalTime::operator-() const {
    if (num_ == std::numeric_limits<int64_t>::min()) {
        throw std::overflow_error("RationalTime unary minus overflow");
    }
    return RationalTime(-num_, den_);
}

RationalTime operator+(const RationalTime& lhs, const RationalTime& rhs) {
    const Wide g = gcd_wide(lhs.den_, rhs.den_);
    const Wide l_scale = rhs.den_ / g;
    const Wide r_scale = lhs.den_ / g;
    const Wide n = static_cast<Wide>(lhs.num_) * l_scale + static_cast<Wide>(rhs.num_) * r_scale;
    const Wide d = static_cast<Wide>(lhs.den_) * l_scale;
    return normalize_wide(n, d);
}

RationalTime operator-(const RationalTime& lhs, const RationalTime& rhs) {
    const Wide g = gcd_wide(lhs.den_, rhs.den_);
    const Wide l_scale = rhs.den_ / g;
    const Wide r_scale = lhs.den_ / g;
    const Wide n = static_cast<Wide>(lhs.num_) * l_scale - static_cast<Wide>(rhs.num_) * r_scale;
    const Wide d = static_cast<Wide>(lhs.den_) * l_scale;
    return normalize_wide(n, d);
}

RationalTime operator*(const RationalTime& lhs, int64_t scalar) {
    return normalize_wide(static_cast<Wide>(lhs.num_) * scalar, lhs.den_);
}

RationalTime operator*(int64_t scalar, const RationalTime& rhs) {
    return rhs * scalar;
}

RationalTime operator/(const RationalTime& lhs, int64_t divisor) {
    if (divisor == 0) {
        throw std::invalid_argument("RationalTime division by zero");
    }
    return normalize_wide(lhs.num_, static_cast<Wide>(lhs.den_) * divisor);
}

RationalTime scale_rational(const RationalTime& value, int64_t mul_num, int64_t mul_den) {
    if (mul_den == 0) {
        throw std::invalid_argument("scale_rational denominator cannot be zero");
    }
    return normalize_wide(static_cast<Wide>(value.num_) * mul_num,
                          static_cast<Wide>(value.den_) * mul_den);
}

std::strong_ordering operator<=>(const RationalTime& lhs, const RationalTime& rhs) noexcept {
    const Wide left = static_cast<Wide>(lhs.num_) * rhs.den_;
    const Wide right = static_cast<Wide>(rhs.num_) * lhs.den_;
    if (left < right) return std::strong_ordering::less;
    if (left > right) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

std::optional<TimeRange> TimeRange::intersection(const TimeRange& other) const {
    if (!overlaps(other)) return std::nullopt;
    const RationalTime s = std::max(start, other.start);
    const RationalTime e = std::min(end(), other.end());
    const RationalTime d = e - s;
    if (d.num() <= 0) return std::nullopt;
    return TimeRange{s, d};
}

} // namespace owncut::timeline
