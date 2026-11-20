#pragma once

#include <string>
#include <vector>

#include "product.h"

namespace kiosk {

// Fixed, hardcoded category list (already de-duplicated). "All" is not a
// real product category -- it means "no filter".
inline const std::vector<std::string>& fixedCategories() {
    static const std::vector<std::string> kCategories = {
        "All", "beauty", "fragrances", "furniture", "groceries"
    };
    return kCategories;
}

// Owns the in-memory product catalog. Loading is the only operation that
// touches "the network" (in the sense of accepting a freshly-fetched JSON
// payload) -- category filtering is always a local, in-memory operation over
// whatever was last successfully loaded.
class ProductRepository {
public:
    // Parses `jsonText` (the raw dummyjson /products response body) and, on
    // success, replaces the catalog and returns true. On malformed input the
    // existing catalog is left untouched and false is returned, so a failed
    // refresh never destroys previously-loaded data.
    bool loadFromJson(const std::string& jsonText, std::string* errorOut = nullptr);

    const std::vector<Product>& all() const { return products_; }
    bool isEmpty() const { return products_.empty(); }
    size_t size() const { return products_.size(); }

    // category == "All" (or empty) returns the full catalog.
    std::vector<const Product*> filterByCategory(const std::string& category) const;

    const Product* findById(long id) const;

private:
    std::vector<Product> products_;
};

} // namespace kiosk
