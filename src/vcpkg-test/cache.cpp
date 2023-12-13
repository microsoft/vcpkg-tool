#include <vcpkg-test/util.h>

#include <vcpkg/base/cache.h>
#include <vcpkg/base/stringview.h>

#include <string>

using namespace vcpkg;

struct Just
{
    int result;
    int operator()() const { return result; }
};

template<class StringLiteralType>
void test_case_cache()
{
    Cache<std::string, int> cache;
    const StringLiteralType apple{"apple"};
    const StringLiteralType durian{"durian"};
    const StringLiteralType melon{"melon"};
    // check that things can be put into the cache and are cached
    const auto first_addr = &(cache.get_lazy(durian, Just{42}));
    CHECK(*first_addr == 42);
    const auto cache_hit_addr = &(cache.get_lazy(durian, Just{42}));
    CHECK(first_addr == cache_hit_addr);

    // also check that inserting an element "before an element" works
    const auto miss_below_addr = &(cache.get_lazy(apple, Just{1729}));
    CHECK(*miss_below_addr == 1729);
    CHECK(miss_below_addr != first_addr);
    const auto hit_below_addr = &(cache.get_lazy(apple, Just{1729}));
    CHECK(hit_below_addr == miss_below_addr);

    // also check that inserting an element "at the end" works
    const auto miss_above_addr = &(cache.get_lazy(melon, Just{1234}));
    CHECK(*miss_above_addr == 1234);
    CHECK(miss_above_addr != first_addr);
    const auto hit_above_addr = &(cache.get_lazy(melon, Just{1234}));
    CHECK(hit_above_addr == miss_above_addr);
}

TEST_CASE ("cache non-transparent", "[cache]")
{
    test_case_cache<std::string>();
}

TEST_CASE ("cache transparent", "[cache]")
{
    test_case_cache<StringLiteral>();
}
