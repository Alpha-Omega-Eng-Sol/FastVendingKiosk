#pragma once

#include <string>

#include "money.h"

namespace kiosk {

struct Product {
    long id = 0;
    std::string title;
    double price = 0.0;              // raw dollars, as returned by the API
    double discountPercentage = 0.0; // 0..100
    std::string category;
    std::string thumbnailUrl;

    money::Cents priceCents() const { return money::fromDollars(price); }
    money::Cents discountedPriceCents() const {
        return money::discountedCentsFromPrice(price, discountPercentage);
    }
    bool hasDiscount() const { return discountPercentage > 0.01; }
};

} // namespace kiosk
