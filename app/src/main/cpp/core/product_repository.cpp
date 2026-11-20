#include "product_repository.h"

#include "json.h"

namespace kiosk {

bool ProductRepository::loadFromJson(const std::string& jsonText, std::string* errorOut) {
    json::Value root;
    if (!json::tryParse(jsonText, root)) {
        if (errorOut) *errorOut = "Malformed JSON response";
        return false;
    }

    const json::Value& productsArr = root.at("products");
    if (!productsArr.isArray()) {
        if (errorOut) *errorOut = "Response missing \"products\" array";
        return false;
    }

    std::vector<Product> parsed;
    parsed.reserve(productsArr.asArray().size());
    for (const auto& item : productsArr.asArray()) {
        if (!item.isObject()) continue;
        // id and title are the only fields we treat as required; everything
        // else degrades gracefully (0 / empty) for malformed entries rather
        // than discarding the whole product.
        if (!item.has("id") || !item.has("title")) continue;

        Product p;
        p.id = item.at("id").asLong(0);
        p.title = item.at("title").asString("");
        p.price = item.at("price").asDouble(0.0);
        p.discountPercentage = item.at("discountPercentage").asDouble(0.0);
        p.category = item.at("category").asString("");
        p.thumbnailUrl = item.at("thumbnail").asString("");

        if (p.price < 0.0) p.price = 0.0;
        if (p.discountPercentage < 0.0) p.discountPercentage = 0.0;
        if (p.discountPercentage > 100.0) p.discountPercentage = 100.0;

        parsed.push_back(std::move(p));
    }

    if (parsed.empty() && !productsArr.asArray().empty()) {
        // Every entry in a non-empty array failed to parse -- treat as an error
        // rather than silently showing an empty catalog.
        if (errorOut) *errorOut = "No valid products found in response";
        return false;
    }

    products_ = std::move(parsed);
    return true;
}

std::vector<const Product*> ProductRepository::filterByCategory(const std::string& category) const {
    std::vector<const Product*> result;
    result.reserve(products_.size());
    bool wantAll = category.empty() || category == "All";
    for (const auto& p : products_) {
        if (wantAll || p.category == category) {
            result.push_back(&p);
        }
    }
    return result;
}

const Product* ProductRepository::findById(long id) const {
    for (const auto& p : products_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

} // namespace kiosk
