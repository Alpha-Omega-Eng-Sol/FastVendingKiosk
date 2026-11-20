#include "cart_manager.h"

#include <algorithm>

namespace kiosk {

void CartManager::add(const Product& product) {
    for (auto& line : lines_) {
        if (line.productId == product.id) {
            line.quantity += 1;
            return;
        }
    }
    lines_.push_back(CartLine{product.id, 1});
}

void CartManager::remove(long productId) {
    lines_.erase(
        std::remove_if(lines_.begin(), lines_.end(),
                        [productId](const CartLine& l) { return l.productId == productId; }),
        lines_.end());
}

void CartManager::clear() {
    lines_.clear();
}

int CartManager::totalItemCount() const {
    int total = 0;
    for (const auto& line : lines_) total += line.quantity;
    return total;
}

money::Cents CartManager::totalDiscountedCents(const ProductRepository& repo) const {
    money::Cents total = 0;
    for (const auto& line : lines_) {
        const Product* p = repo.findById(line.productId);
        if (!p) continue;
        total += p->discountedPriceCents() * line.quantity;
    }
    return total;
}

int CartManager::quantityOf(long productId) const {
    for (const auto& line : lines_) {
        if (line.productId == productId) return line.quantity;
    }
    return 0;
}

} // namespace kiosk
