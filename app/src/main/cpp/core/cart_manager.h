#pragma once

#include <vector>

#include "money.h"
#include "product_repository.h"

namespace kiosk {

struct CartLine {
    long productId;
    int quantity;
};

// All cart state is local and keyed by product id. No network calls happen
// anywhere in this class.
class CartManager {
public:
    // Adds one unit of `product`. If already present, increments quantity.
    void add(const Product& product);

    // Removes the product entirely (regardless of quantity).
    void remove(long productId);

    void clear();

    bool isEmpty() const { return lines_.empty(); }
    const std::vector<CartLine>& lines() const { return lines_; }

    int totalItemCount() const;

    // Computes the total using discounted prices, looking products up in
    // `repo`. Lines referring to a product no longer present in the catalog
    // are skipped (defensive; shouldn't normally happen since repo loads
    // don't clear the cart).
    money::Cents totalDiscountedCents(const ProductRepository& repo) const;

    int quantityOf(long productId) const;

private:
    std::vector<CartLine> lines_;
};

} // namespace kiosk
