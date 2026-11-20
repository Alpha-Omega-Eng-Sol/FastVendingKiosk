// Integer-cents money helpers. We never accumulate floating point currency
// values: every price is rounded to the nearest cent once, at the point it
// enters the system, and all later arithmetic (cart totals, etc.) is done in
// integers.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace kiosk::money {

using Cents = int64_t;

inline Cents fromDollars(double dollars) {
    return static_cast<Cents>(std::llround(dollars * 100.0));
}

// price and discountPercentage both come straight off the API as doubles.
// Round once, at the boundary, to the nearest cent.
inline Cents discountedCentsFromPrice(double price, double discountPercentage) {
    double discounted = price * (100.0 - discountPercentage) / 100.0;
    return static_cast<Cents>(std::llround(discounted * 100.0));
}

inline std::string formatUsd(Cents cents) {
    bool negative = cents < 0;
    Cents abs = negative ? -cents : cents;
    Cents dollars = abs / 100;
    Cents remainder = abs % 100;
    std::string out = "$";
    if (negative) out = "-$";
    out += std::to_string(dollars);
    out += ".";
    if (remainder < 10) out += "0";
    out += std::to_string(remainder);
    return out;
}

} // namespace kiosk::money
