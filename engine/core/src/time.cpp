#include "editor/time.hpp"

#include <limits>
#include <numeric>

namespace editor {
namespace {
using Wide = __int128_t;

std::int64_t narrow(Wide value) {
    if (value < std::numeric_limits<std::int64_t>::min() ||
        value > std::numeric_limits<std::int64_t>::max()) {
        throw std::overflow_error("timestamp conversion overflow");
    }
    return static_cast<std::int64_t>(value);
}

Wide scaled_ticks(const Timestamp& value, const TimeBase& target) noexcept {
    return static_cast<Wide>(value.ticks) * value.time_base.numerator * target.denominator;
}
Wide denominator(const TimeBase& source, const TimeBase& target) noexcept {
    return static_cast<Wide>(source.denominator) * target.numerator;
}
} // namespace

TimeBase::TimeBase(std::int64_t num, std::int64_t den) {
    constexpr std::int64_t max_term = 1'000'000'000;
    if (num <= 0 || den <= 0 || num > max_term || den > max_term)
        throw std::invalid_argument("timebase terms must be in [1, 1000000000]");
    const auto divisor = std::gcd(num, den);
    numerator = num / divisor;
    denominator = den / divisor;
}

Timestamp Timestamp::rescaled(TimeBase destination) const {
    const Wide numerator_value = scaled_ticks(*this, destination);
    const Wide denominator_value = denominator(time_base, destination);
    // Round to nearest tick; exact half-ticks round away from zero.
    const Wide magnitude = numerator_value < 0 ? -numerator_value : numerator_value;
    const Wide rounded = (magnitude + denominator_value / 2) / denominator_value;
    return {narrow(numerator_value < 0 ? -rounded : rounded), destination};
}

bool operator==(const Timestamp& lhs, const Timestamp& rhs) noexcept {
    return static_cast<Wide>(lhs.ticks) * lhs.time_base.numerator * rhs.time_base.denominator ==
           static_cast<Wide>(rhs.ticks) * rhs.time_base.numerator * lhs.time_base.denominator;
}
bool operator<(const Timestamp& lhs, const Timestamp& rhs) noexcept {
    return static_cast<Wide>(lhs.ticks) * lhs.time_base.numerator * rhs.time_base.denominator <
           static_cast<Wide>(rhs.ticks) * rhs.time_base.numerator * lhs.time_base.denominator;
}
Timestamp operator+(Timestamp lhs, Timestamp rhs) {
    rhs = rhs.rescaled(lhs.time_base);
    return {narrow(static_cast<Wide>(lhs.ticks) + rhs.ticks), lhs.time_base};
}
Timestamp operator-(Timestamp lhs, Timestamp rhs) {
    rhs = rhs.rescaled(lhs.time_base);
    return {narrow(static_cast<Wide>(lhs.ticks) - rhs.ticks), lhs.time_base};
}
} // namespace editor
