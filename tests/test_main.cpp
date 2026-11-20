#include "test_framework.h"

#include "../app/src/main/cpp/core/cart_manager.h"
#include "../app/src/main/cpp/core/json.h"
#include "../app/src/main/cpp/core/money.h"
#include "../app/src/main/cpp/core/product_repository.h"

using namespace kiosk;

// ---------------------------------------------------------------------------
// JSON parser
// ---------------------------------------------------------------------------

TEST_CASE(json_parses_nested_objects_and_arrays) {
    auto v = json::parse(R"({"a": 1, "b": [1, 2, 3], "c": {"d": "hello"}, "e": null, "f": true})");
    CHECK(v.isObject());
    CHECK_EQ(v.at("a").asLong(), 1L);
    CHECK_EQ(v.at("b").asArray().size(), (size_t)3);
    CHECK_EQ(v.at("c").at("d").asString(), std::string("hello"));
    CHECK(v.at("e").isNull());
    CHECK_EQ(v.at("f").asBool(), true);
}

TEST_CASE(json_handles_escapes_and_unicode) {
    auto v = json::parse(R"({"s": "line1\nline2\t\"quoted\"", "u": "é"})");
    CHECK_EQ(v.at("s").asString(), std::string("line1\nline2\t\"quoted\""));
    // é in UTF-8 is 0xC3 0xA9
    const std::string& u = v.at("u").asString();
    CHECK_EQ(u.size(), (size_t)2);
    CHECK_EQ((unsigned char)u[0], (unsigned char)0xC3);
    CHECK_EQ((unsigned char)u[1], (unsigned char)0xA9);
}

TEST_CASE(json_tryParse_rejects_malformed_input) {
    json::Value v;
    CHECK(!json::tryParse("{not valid json", v));
    CHECK(!json::tryParse("", v));
    CHECK(json::tryParse("{}", v));
}

// ---------------------------------------------------------------------------
// Money / discount calculation
// ---------------------------------------------------------------------------

TEST_CASE(discounted_price_calculation) {
    // price 9.99, discount 10% -> 8.991 -> rounds to 8.99
    CHECK_EQ(money::discountedCentsFromPrice(9.99, 10.0), (money::Cents)899);

    // price 100.00, discount 12.96% -> 87.04
    CHECK_EQ(money::discountedCentsFromPrice(100.0, 12.96), (money::Cents)8704);

    // zero discount keeps original price
    CHECK_EQ(money::discountedCentsFromPrice(19.99, 0.0), (money::Cents)1999);

    // 100% discount -> free
    CHECK_EQ(money::discountedCentsFromPrice(19.99, 100.0), (money::Cents)0);
}

TEST_CASE(product_discounted_price_matches_helper) {
    Product p;
    p.price = 549.0;
    p.discountPercentage = 15.46;
    CHECK_EQ(p.discountedPriceCents(), money::discountedCentsFromPrice(549.0, 15.46));
    CHECK(p.hasDiscount());

    Product p2;
    p2.price = 10.0;
    p2.discountPercentage = 0.0;
    CHECK(!p2.hasDiscount());
}

TEST_CASE(format_usd) {
    CHECK_EQ(money::formatUsd(899), std::string("$8.99"));
    CHECK_EQ(money::formatUsd(100), std::string("$1.00"));
    CHECK_EQ(money::formatUsd(5), std::string("$0.05"));
    CHECK_EQ(money::formatUsd(0), std::string("$0.00"));
}

// ---------------------------------------------------------------------------
// ProductRepository: parsing + category filtering
// ---------------------------------------------------------------------------

static const char* kSampleCatalog = R"JSON(
{
  "products": [
    {"id": 1, "title": "Lipstick", "price": 10.0, "discountPercentage": 10.0, "category": "beauty", "thumbnail": "http://x/1.png"},
    {"id": 2, "title": "Perfume", "price": 50.0, "discountPercentage": 0.0, "category": "fragrances", "thumbnail": "http://x/2.png"},
    {"id": 3, "title": "Sofa", "price": 400.0, "discountPercentage": 5.5, "category": "furniture", "thumbnail": "http://x/3.png"},
    {"id": 4, "title": "Bread", "price": 3.5, "discountPercentage": 0.0, "category": "groceries", "thumbnail": "http://x/4.png"},
    {"id": 5, "title": "Phone", "price": 999.0, "discountPercentage": 2.0, "category": "smartphones", "thumbnail": "http://x/5.png"}
  ],
  "total": 5, "skip": 0, "limit": 5
}
)JSON";

TEST_CASE(repository_loads_products_from_json) {
    ProductRepository repo;
    std::string err;
    CHECK(repo.loadFromJson(kSampleCatalog, &err));
    CHECK_EQ(repo.size(), (size_t)5);
    const Product* p = repo.findById(1);
    CHECK(p != nullptr);
    CHECK_EQ(p->title, std::string("Lipstick"));
}

TEST_CASE(repository_category_filtering_is_local) {
    ProductRepository repo;
    repo.loadFromJson(kSampleCatalog);

    auto all = repo.filterByCategory("All");
    CHECK_EQ(all.size(), (size_t)5);

    auto beauty = repo.filterByCategory("beauty");
    CHECK_EQ(beauty.size(), (size_t)1);
    CHECK_EQ(beauty[0]->title, std::string("Lipstick"));

    auto groceries = repo.filterByCategory("groceries");
    CHECK_EQ(groceries.size(), (size_t)1);

    // "smartphones" isn't one of the 5 hardcoded category buttons, so it's
    // only reachable via "All" -- filtering for it directly should simply
    // return the matching product (repository itself doesn't restrict to the
    // fixed list; that's a UI concern), proving filtering is a pure in-memory
    // predicate over whatever category string the product actually has.
    auto phones = repo.filterByCategory("smartphones");
    CHECK_EQ(phones.size(), (size_t)1);
}

TEST_CASE(fixed_categories_has_no_duplicates_and_starts_with_all) {
    const auto& cats = fixedCategories();
    CHECK_EQ(cats.size(), (size_t)5);
    CHECK_EQ(cats[0], std::string("All"));
    for (size_t i = 0; i < cats.size(); ++i) {
        for (size_t j = i + 1; j < cats.size(); ++j) {
            CHECK(cats[i] != cats[j]);
        }
    }
}

TEST_CASE(repository_handles_malformed_product_data) {
    ProductRepository repo;
    std::string err;
    // Missing "products" key entirely.
    CHECK(!repo.loadFromJson(R"({"foo": "bar"})", &err));
    CHECK(!err.empty());

    // Not even valid JSON.
    CHECK(!repo.loadFromJson("not json at all", &err));

    // products present but entries missing required fields (id/title) are
    // skipped individually rather than failing the whole parse.
    const char* partiallyBroken = R"JSON(
    {"products": [
        {"id": 1, "title": "Valid", "price": 5.0, "discountPercentage": 0, "category": "beauty", "thumbnail": "x"},
        {"price": 5.0, "category": "beauty"},
        {"id": 3, "title": "Also valid"}
    ]}
    )JSON";
    CHECK(repo.loadFromJson(partiallyBroken, &err));
    CHECK_EQ(repo.size(), (size_t)2);

    // Entirely empty products array parses fine (legitimate empty catalog).
    CHECK(repo.loadFromJson(R"({"products": []})"));
    CHECK(repo.isEmpty());
}

TEST_CASE(repository_failed_refresh_keeps_previous_catalog) {
    ProductRepository repo;
    repo.loadFromJson(kSampleCatalog);
    CHECK_EQ(repo.size(), (size_t)5);

    std::string err;
    bool ok = repo.loadFromJson("{ broken", &err);
    CHECK(!ok);
    // Catalog from the first successful load must still be intact.
    CHECK_EQ(repo.size(), (size_t)5);
}

// ---------------------------------------------------------------------------
// CartManager
// ---------------------------------------------------------------------------

static Product makeProduct(long id, double price, double discount, const char* category = "beauty") {
    Product p;
    p.id = id;
    p.title = "Product " + std::to_string(id);
    p.price = price;
    p.discountPercentage = discount;
    p.category = category;
    p.thumbnailUrl = "http://x/" + std::to_string(id) + ".png";
    return p;
}

TEST_CASE(cart_add_increments_quantity) {
    CartManager cart;
    Product p = makeProduct(1, 10.0, 0.0);
    CHECK(cart.isEmpty());

    cart.add(p);
    CHECK_EQ(cart.quantityOf(1), 1);
    CHECK(!cart.isEmpty());

    cart.add(p);
    cart.add(p);
    CHECK_EQ(cart.quantityOf(1), 3);
    CHECK_EQ(cart.totalItemCount(), 3);
    CHECK_EQ(cart.lines().size(), (size_t)1); // still one line, quantity 3
}

TEST_CASE(cart_add_multiple_products_tracks_each) {
    CartManager cart;
    cart.add(makeProduct(1, 10.0, 0.0));
    cart.add(makeProduct(2, 20.0, 0.0));
    cart.add(makeProduct(1, 10.0, 0.0));

    CHECK_EQ(cart.lines().size(), (size_t)2);
    CHECK_EQ(cart.quantityOf(1), 2);
    CHECK_EQ(cart.quantityOf(2), 1);
    CHECK_EQ(cart.totalItemCount(), 3);
}

TEST_CASE(cart_remove_deletes_entire_line) {
    CartManager cart;
    cart.add(makeProduct(1, 10.0, 0.0));
    cart.add(makeProduct(1, 10.0, 0.0));
    cart.add(makeProduct(2, 20.0, 0.0));
    CHECK_EQ(cart.quantityOf(1), 2);

    cart.remove(1);
    CHECK_EQ(cart.quantityOf(1), 0);
    CHECK_EQ(cart.lines().size(), (size_t)1);
    CHECK(!cart.isEmpty());

    cart.remove(2);
    CHECK(cart.isEmpty());
}

TEST_CASE(cart_clear_empties_everything) {
    CartManager cart;
    cart.add(makeProduct(1, 10.0, 0.0));
    cart.add(makeProduct(2, 20.0, 0.0));
    CHECK(!cart.isEmpty());

    cart.clear();
    CHECK(cart.isEmpty());
    CHECK_EQ(cart.totalItemCount(), 0);
    CHECK_EQ(cart.lines().size(), (size_t)0);
}

TEST_CASE(cart_total_uses_discounted_prices) {
    ProductRepository repo;
    repo.loadFromJson(kSampleCatalog);
    CartManager cart;

    const Product* lipstick = repo.findById(1); // 10.0, 10% off -> 9.00
    const Product* perfume = repo.findById(2);  // 50.0, 0% off -> 50.00
    cart.add(*lipstick);
    cart.add(*lipstick);  // qty 2 -> 18.00
    cart.add(*perfume);   // qty 1 -> 50.00

    money::Cents total = cart.totalDiscountedCents(repo);
    CHECK_EQ(total, (money::Cents)(900 * 2 + 5000));
}

TEST_CASE(cart_total_skips_lines_for_missing_products) {
    ProductRepository repo;
    repo.loadFromJson(kSampleCatalog);
    CartManager cart;
    cart.add(*repo.findById(1));
    cart.remove(1);
    cart.add(makeProduct(999, 42.0, 0.0)); // id not in repo
    money::Cents total = cart.totalDiscountedCents(repo);
    CHECK_EQ(total, (money::Cents)0);
}

int main() {
    return testing::runAll();
}
