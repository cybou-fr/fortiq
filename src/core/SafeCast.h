#pragma once

#include <QtGlobal>
#include <cmath>
#include <limits>

namespace fortiq {

// double -> qint64 without undefined behaviour: server output and JSON numbers can be NaN, infinite or far beyond 2^63
// (found by the fuzzer, audit backlog 5). NaN becomes 0; out-of-range values saturate.
inline qint64 toInt64(double value) {
    if (std::isnan(value)) return 0;
    if (value >= 9223372036854775807.0) return std::numeric_limits<qint64>::max();
    if (value <= -9223372036854775808.0) return std::numeric_limits<qint64>::min();
    return static_cast<qint64>(value);
}

}  // namespace fortiq
