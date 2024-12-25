#include <vcpkg-test/util.h>

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/setup-messages.h>

#include <vcpkg/commands.z-generate-message-map.h>

using namespace vcpkg;

TEST_CASE ("append floating list", "[LocalizedString]")
{
    const auto a = LocalizedString::from_raw("a");
    const auto b = LocalizedString::from_raw("b");
    CHECK(LocalizedString().append_floating_list(2, std::vector<LocalizedString>{}) == LocalizedString());
    CHECK(LocalizedString().append_floating_list(2, std::vector<LocalizedString>{a}) ==
          LocalizedString::from_raw(" a"));
    const auto expected = LocalizedString::from_raw("  heading\n    a\n    b");
    CHECK(LocalizedString::from_raw("  heading").append_floating_list(2, std::vector<LocalizedString>{a, b}) ==
          expected);
}

TEST_CASE ("get path to locale from LCID", "[messages]")
{
    // valid LCID; Chinese
    auto res = msg::get_locale_path(2052);
    CHECK(res == "locales/messages.zh-Hans.json");

    // invalid LCID
    CHECK(!msg::get_locale_path(0000).has_value());
}
TEST_CASE ("get message_map from LCID", "[messages]")
{
    StringView msg_name = "AddCommandFirstArg";

    // valid lcid; Spanish
    auto map = msg::get_message_map_from_lcid(3082);
    auto msg = map.value_or_exit(VCPKG_LINE_INFO).map.get(msg_name);
    CHECK(msg->string(VCPKG_LINE_INFO) ==
          "El primer par\u00e1metro que se va a agregar debe ser \"artefacto\" o \"puerto\".");
}

TEST_CASE ("generate message get_all_format_args", "[messages]")
{
    LocalizedString err;
    auto res = get_all_format_args("hey ho let's go", err);
    CHECK(err.data() == "");
    CHECK(res == std::vector<StringView>{});

    res = get_all_format_args("hey {ho} let's {go}", err);
    CHECK(err.data() == "");
    CHECK(res == std::vector<StringView>{"ho", "go"});

    res = get_all_format_args("{{ {hey} }}", err);
    CHECK(err.data() == "");
    CHECK(res == std::vector<StringView>{"hey"});

    res = get_all_format_args("{", err);
    CHECK(err.data() == "unbalanced brace in format string \"{\"");
    CHECK(res == std::vector<StringView>{});

    res = get_all_format_args("{ {blah}", err);
    CHECK(err.data() == "unbalanced brace in format string \"{ {blah}\"");
    CHECK(res == std::vector<StringView>{"blah"});

    res = get_all_format_args("{ { {blah} {bloop}", err);
    CHECK(err.data() == "unbalanced brace in format string \"{ { {blah} {bloop}\"");
    CHECK(res == std::vector<StringView>{"blah", "bloop"});
}

TEST_CASE ("generate message get_format_arg_mismatches", "[messages]")
{
    LocalizedString err;
    auto res = get_format_arg_mismatches("hey ho", "", err);
    CHECK(err.data() == "");
    CHECK(res.arguments_without_comment == std::vector<StringView>{});
    CHECK(res.comments_without_argument == std::vector<StringView>{});

    res = get_format_arg_mismatches("hey {ho} let's {go}", "{ho} {go}", err);
    CHECK(err.data() == "");
    CHECK(res.arguments_without_comment == std::vector<StringView>{});
    CHECK(res.comments_without_argument == std::vector<StringView>{});

    res = get_format_arg_mismatches("hey {ho} let's {go}", "invalid format string { {ho} {go}", err);
    CHECK(err.data() == "");
    CHECK(res.arguments_without_comment == std::vector<StringView>{});
    CHECK(res.comments_without_argument == std::vector<StringView>{});

    res = get_format_arg_mismatches("hey { {ho} let's {go}", "{blah}", err);
    CHECK(err.data() == "unbalanced brace in format string \"hey { {ho} let's {go}\"");
    CHECK(res.arguments_without_comment == std::vector<StringView>{});
    CHECK(res.comments_without_argument == std::vector<StringView>{});

    res = get_format_arg_mismatches("hey {ho} let's {go}", "{blah}", err);
    CHECK(err.data() == "");
    CHECK(res.arguments_without_comment == std::vector<StringView>{"go", "ho"});
    CHECK(res.comments_without_argument == std::vector<StringView>{"blah"});

    res = get_format_arg_mismatches("hey {ho} {go} let's {go}", "{blah} {blah}", err);
    CHECK(err.data() == "");
    CHECK(res.arguments_without_comment == std::vector<StringView>{"go", "ho"});
    CHECK(res.comments_without_argument == std::vector<StringView>{"blah"});
}

namespace
{
    template<class Void, class Test, class... Args>
    constexpr bool adapt_context_to_expected_invocable_with_impl = false;

    template<class Test, class... Args>
    constexpr bool adapt_context_to_expected_invocable_with_impl<
        std::void_t<decltype(adapt_context_to_expected(std::declval<Test>(), std::declval<Args>()...))>,
        Test,
        Args...> = true;

    template<class Test, class... Args>
    constexpr bool adapt_context_to_expected_invocable_with =
        adapt_context_to_expected_invocable_with_impl<void, Test, Args...>;

    static_assert(!adapt_context_to_expected_invocable_with<int (*)(DiagnosticContext&)>,
                  "Callable needs to return optional or unique_ptr");

    // The following tests are the cross product of:
    // {prvalue, lvalue, xvalue} X {non-const, const} X {non-ref, ref}

    Optional<int> returns_optional_prvalue(DiagnosticContext&, int val) { return val; }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_prvalue), int>, "boom");
    static_assert(std::is_same_v<ExpectedL<int>, decltype(adapt_context_to_expected(returns_optional_prvalue, 42))>,
                  "boom");

    const Optional<int> returns_optional_const_prvalue(DiagnosticContext&, int val) { return val; }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_const_prvalue), int>, "boom");
    static_assert(
        std::is_same_v<ExpectedL<int>, decltype(adapt_context_to_expected(returns_optional_const_prvalue, 42))>,
        "boom");

    Optional<int>& returns_optional_lvalue(DiagnosticContext&, Optional<int>& val) { return val; }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_lvalue), Optional<int>&>, "boom");
    static_assert(
        std::is_same_v<ExpectedL<int&>,
                       decltype(adapt_context_to_expected(returns_optional_lvalue, std::declval<Optional<int>&>()))>,
        "boom");

    const Optional<int>& returns_optional_const_lvalue(DiagnosticContext&, const Optional<int>& val) { return val; }
    static_assert(
        adapt_context_to_expected_invocable_with<decltype(returns_optional_const_lvalue), const Optional<int>&>,
        "boom");
    static_assert(std::is_same_v<ExpectedL<const int&>,
                                 decltype(adapt_context_to_expected(returns_optional_const_lvalue,
                                                                    std::declval<const Optional<int>&>()))>,
                  "boom");

    Optional<int>&& returns_optional_xvalue(DiagnosticContext&, Optional<int>&& val) { return std::move(val); }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_xvalue), Optional<int>>, "boom");
    static_assert(
        std::is_same_v<ExpectedL<int>,
                       decltype(adapt_context_to_expected(returns_optional_xvalue, std::declval<Optional<int>>()))>,
        "boom");

    const Optional<int>&& returns_optional_const_xvalue(DiagnosticContext&, Optional<int>&& val)
    {
        return std::move(val);
    }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_const_xvalue), Optional<int>>,
                  "boom");
    static_assert(std::is_same_v<ExpectedL<int>,
                                 decltype(adapt_context_to_expected(returns_optional_const_xvalue,
                                                                    std::declval<Optional<int>>()))>,
                  "boom");

    Optional<int&> returns_optional_ref_prvalue(DiagnosticContext&, int& val) { return val; }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_ref_prvalue), int&>, "boom");
    static_assert(
        std::is_same_v<ExpectedL<int&>,
                       decltype(adapt_context_to_expected(returns_optional_ref_prvalue, std::declval<int&>()))>,
        "boom");

    const Optional<int&> returns_optional_ref_const_prvalue(DiagnosticContext&, int& val) { return val; }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_ref_const_prvalue), int&>, "boom");
    static_assert(
        std::is_same_v<ExpectedL<int&>,
                       decltype(adapt_context_to_expected(returns_optional_ref_const_prvalue, std::declval<int&>()))>,
        "boom");

    Optional<int&>& returns_optional_ref_lvalue(DiagnosticContext&, Optional<int&>& val) { return val; }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_ref_lvalue), Optional<int&>&>,
                  "boom");
    static_assert(std::is_same_v<ExpectedL<int&>,
                                 decltype(adapt_context_to_expected(returns_optional_ref_lvalue,
                                                                    std::declval<Optional<int&>&>()))>,
                  "boom");

    const Optional<int&>& returns_optional_ref_const_lvalue(DiagnosticContext&, const Optional<int&>& val)
    {
        return val;
    }
    static_assert(
        adapt_context_to_expected_invocable_with<decltype(returns_optional_ref_const_lvalue), const Optional<int&>&>,
        "boom");
    static_assert(std::is_same_v<ExpectedL<int&>,
                                 decltype(adapt_context_to_expected(returns_optional_ref_const_lvalue,
                                                                    std::declval<const Optional<int&>&>()))>,
                  "boom");

    Optional<int&>&& returns_optional_ref_xvalue(DiagnosticContext&, Optional<int&>&& val) { return std::move(val); }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_ref_xvalue), Optional<int&>>,
                  "boom");
    static_assert(std::is_same_v<ExpectedL<int&>,
                                 decltype(adapt_context_to_expected(returns_optional_ref_xvalue,
                                                                    std::declval<Optional<int&>>()))>,
                  "boom");

    const Optional<int&>&& returns_optional_ref_const_xvalue(DiagnosticContext&, Optional<int&>&& val)
    {
        return std::move(val);
    }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_ref_const_xvalue), Optional<int&>>,
                  "boom");
    static_assert(std::is_same_v<ExpectedL<int&>,
                                 decltype(adapt_context_to_expected(returns_optional_ref_const_xvalue,
                                                                    std::declval<Optional<int&>>()))>,
                  "boom");

    Optional<int> returns_optional_fail(DiagnosticContext& context)
    {
        context.report({DiagKind::Error, LocalizedString::from_raw("something bad happened")});
        return nullopt;
    }

    // The following tests are the cross product of:
    // {prvalue, lvalue, xvalue} X {non-const, const}
    std::unique_ptr<int> returns_unique_ptr_prvalue(DiagnosticContext&, int val)
    {
        return std::unique_ptr<int>{new int{val}};
    }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_unique_ptr_prvalue), int>, "boom");
    static_assert(std::is_same_v<ExpectedL<std::unique_ptr<int>>,
                                 decltype(adapt_context_to_expected(returns_unique_ptr_prvalue, 42))>,
                  "boom");

    using returns_unique_ptr_const_prvalue_t = const std::unique_ptr<int> (*)(DiagnosticContext&, int);
    static_assert(!adapt_context_to_expected_invocable_with<returns_unique_ptr_const_prvalue_t, int>, "boom");

    std::unique_ptr<int>& returns_unique_ptr_lvalue(DiagnosticContext&, std::unique_ptr<int>& ret) { return ret; }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_unique_ptr_lvalue), std::unique_ptr<int>&>,
                  "boom");
    static_assert(std::is_same_v<ExpectedL<std::unique_ptr<int>&>,
                                 decltype(adapt_context_to_expected(returns_unique_ptr_lvalue,
                                                                    std::declval<std::unique_ptr<int>&>()))>,
                  "boom");

    const std::unique_ptr<int>& returns_unique_ptr_const_lvalue(DiagnosticContext&, const std::unique_ptr<int>& ret)
    {
        return ret;
    }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_unique_ptr_const_lvalue),
                                                           const std::unique_ptr<int>&>,
                  "boom");
    static_assert(std::is_same_v<ExpectedL<const std::unique_ptr<int>&>,
                                 decltype(adapt_context_to_expected(returns_unique_ptr_const_lvalue,
                                                                    std::declval<const std::unique_ptr<int>&>()))>,
                  "boom");

    std::unique_ptr<int>&& returns_unique_ptr_xvalue(DiagnosticContext&, std::unique_ptr<int>&& val)
    {
        return std::move(val);
    }
    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_unique_ptr_xvalue), std::unique_ptr<int>&&>,
                  "boom");
    static_assert(std::is_same_v<ExpectedL<std::unique_ptr<int>>,
                                 decltype(adapt_context_to_expected(returns_unique_ptr_xvalue,
                                                                    std::declval<std::unique_ptr<int>>()))>,
                  "boom");

    using returns_unique_ptr_const_xvalue_t = const std::unique_ptr<int> &&
                                              (*)(DiagnosticContext&, const std::unique_ptr<int>&&);

    static_assert(
        !adapt_context_to_expected_invocable_with<returns_unique_ptr_const_xvalue_t, const std::unique_ptr<int>&&>,
        "boom");

    std::unique_ptr<int> returns_unique_ptr_fail(DiagnosticContext& context)
    {
        context.report({DiagKind::Error, LocalizedString::from_raw("something bad happened")});
        return nullptr;
    }

    struct CopyOnce
    {
        bool& m_copied;

        explicit CopyOnce(bool& copied) : m_copied(copied) { }
        CopyOnce(const CopyOnce& other) : m_copied(other.m_copied)
        {
            REQUIRE(!m_copied);
            m_copied = true;
        }

        CopyOnce& operator=(const CopyOnce&) = delete;
    };

    struct MoveCounter
    {
        int& m_move_limit;

        explicit MoveCounter(int& move_limit) : m_move_limit(move_limit) { }
        MoveCounter(const MoveCounter&) = delete;
        MoveCounter(MoveCounter&& other) : m_move_limit(other.m_move_limit)
        {
            REQUIRE(m_move_limit > 0);
            --m_move_limit;
        }

        MoveCounter& operator=(const MoveCounter&) = delete;
        MoveCounter& operator=(MoveCounter&&) = delete;
    };
} // unnamed namespace

template<class T, class U>
bool same_object(T& lhs, U& rhs)
{
    return &lhs == &rhs;
}

TEST_CASE ("adapt DiagnosticContext to ExpectedL", "[diagnostics]")
{
    // returns_optional_prvalue
    {
        auto adapted = adapt_context_to_expected(returns_optional_prvalue, 42);
        REQUIRE(adapted.value_or_exit(VCPKG_LINE_INFO) == 42);
    }
    {
        int move_limit = 1;
        auto adapted = adapt_context_to_expected(
            [](DiagnosticContext&, int& move_limit) {
                Optional<MoveCounter> ret;
                ret.emplace(move_limit);
                return ret;
            },
            move_limit);

        REQUIRE(move_limit == 0);
    }
    // returns_optional_const_prvalue
    {
        auto adapted = adapt_context_to_expected(returns_optional_const_prvalue, 42);
        REQUIRE(adapted.value_or_exit(VCPKG_LINE_INFO) == 42);
    }
    // returns_optional_lvalue
    {
        Optional<int> the_lvalue = 42;
        auto adapted = adapt_context_to_expected(returns_optional_lvalue, the_lvalue);
        REQUIRE(same_object(adapted.value_or_exit(VCPKG_LINE_INFO), the_lvalue.value_or_exit(VCPKG_LINE_INFO)));
    }
    // returns_optional_const_lvalue
    {
        Optional<int> the_lvalue = 42;
        auto adapted = adapt_context_to_expected(returns_optional_const_lvalue, the_lvalue);
        REQUIRE(same_object(adapted.value_or_exit(VCPKG_LINE_INFO), the_lvalue.value_or_exit(VCPKG_LINE_INFO)));
    }
    // returns_optional_xvalue
    {
        Optional<int> an_lvalue = 42;
        REQUIRE(
            adapt_context_to_expected(returns_optional_xvalue, std::move(an_lvalue)).value_or_exit(VCPKG_LINE_INFO) ==
            42);
    }
    {
        int move_limit = 1;
        Optional<MoveCounter> an_lvalue;
        an_lvalue.emplace(move_limit);
        auto adapted = adapt_context_to_expected(
            [](DiagnosticContext&, Optional<MoveCounter>&& ret) -> Optional<MoveCounter>&& { return std::move(ret); },
            std::move(an_lvalue));

        REQUIRE(move_limit == 0);
    }
    // returns_optional_const_xvalue
    {
        Optional<int> an_lvalue = 42;
        REQUIRE(adapt_context_to_expected(returns_optional_const_xvalue, std::move(an_lvalue))
                    .value_or_exit(VCPKG_LINE_INFO) == 42);
    }
    {
        bool copied = false;
        Optional<CopyOnce> an_lvalue;
        an_lvalue.emplace(copied);
        auto adapted = adapt_context_to_expected(
            [](DiagnosticContext&, const Optional<CopyOnce>&& ret) -> const Optional<CopyOnce>&& {
                return std::move(ret);
            },
            std::move(an_lvalue));

        REQUIRE(copied);
    }
    // returns_optional_ref_prvalue
    {
        int the_lvalue = 42;
        auto adapted = adapt_context_to_expected(returns_optional_ref_prvalue, the_lvalue);
        REQUIRE(adapted.get() == &the_lvalue);
    }
    // returns_optional_ref_const_prvalue
    {
        int the_lvalue = 42;
        auto adapted = adapt_context_to_expected(returns_optional_ref_const_prvalue, the_lvalue);
        REQUIRE(adapted.get() == &the_lvalue);
    }
    // returns_optional_ref_lvalue
    {
        int the_inside_lvalue = 42;
        Optional<int&> the_lvalue{the_inside_lvalue};
        auto adapted = adapt_context_to_expected(returns_optional_ref_lvalue, the_lvalue);
        REQUIRE(adapted.get() == &the_inside_lvalue);
    }
    {
        int move_limit = 0;
        MoveCounter the_inside_lvalue{move_limit};
        Optional<MoveCounter&> an_lvalue;
        an_lvalue.emplace(the_inside_lvalue);
        auto adapted = adapt_context_to_expected(
            [](DiagnosticContext&, Optional<MoveCounter&>&& ret) -> Optional<MoveCounter&> { return std::move(ret); },
            std::move(an_lvalue));
        REQUIRE(adapted.get() == &the_inside_lvalue);
        REQUIRE(move_limit == 0);
    }
    // returns_optional_ref_const_lvalue
    {
        int the_inside_lvalue = 42;
        Optional<int&> the_lvalue{the_inside_lvalue};
        auto adapted = adapt_context_to_expected(returns_optional_ref_const_lvalue, the_lvalue);
        REQUIRE(adapted.get() == &the_inside_lvalue);
    }
    // returns_optional_ref_xvalue
    {
        int the_inside_lvalue = 42;
        Optional<int&> the_lvalue{the_inside_lvalue};
        auto adapted = adapt_context_to_expected(returns_optional_ref_xvalue, std::move(the_lvalue));
        REQUIRE(adapted.get() == &the_inside_lvalue);
    }
    // returns_optional_ref_const_xvalue
    {
        int the_inside_lvalue = 42;
        Optional<int&> the_lvalue{the_inside_lvalue};
        auto adapted = adapt_context_to_expected(returns_optional_ref_const_xvalue, std::move(the_lvalue));
        REQUIRE(adapted.get() == &the_inside_lvalue);
    }

    // returns_optional_prvalue_fail
    {
        auto adapted = adapt_context_to_expected(returns_optional_fail);
        REQUIRE(!adapted.has_value());
        REQUIRE(adapted.error() == LocalizedString::from_raw("error: something bad happened"));
    }

    // returns_unique_ptr_prvalue
    {
        auto adapted = adapt_context_to_expected(returns_unique_ptr_prvalue, 42);
        REQUIRE(*(adapted.value_or_exit(VCPKG_LINE_INFO)) == 42);
    }
    // returns_unique_ptr_lvalue
    {
        auto an_lvalue = std::make_unique<int>(42);
        auto adapted = adapt_context_to_expected(returns_unique_ptr_lvalue, an_lvalue);
        REQUIRE(same_object(adapted.value_or_exit(VCPKG_LINE_INFO), an_lvalue));
    }
    // returns_unique_ptr_const_lvalue
    {
        auto an_lvalue = std::make_unique<int>(42);
        auto adapted = adapt_context_to_expected(returns_unique_ptr_const_lvalue, an_lvalue);
        REQUIRE(same_object(adapted.value_or_exit(VCPKG_LINE_INFO), an_lvalue));
    }
    // returns_unique_ptr_xvalue
    {
        auto an_lvalue = std::make_unique<int>(42);
        auto the_pointer = an_lvalue.get();
        auto adapted = adapt_context_to_expected(returns_unique_ptr_xvalue, std::move(an_lvalue));
        REQUIRE(!an_lvalue); // was moved into the adapted
        REQUIRE(adapted.value_or_exit(VCPKG_LINE_INFO).get() == the_pointer);
    }

    // returns_unique_ptr_fail
    {
        auto adapted = adapt_context_to_expected(returns_unique_ptr_fail);
        REQUIRE(!adapted.has_value());
        REQUIRE(adapted.error() == LocalizedString::from_raw("error: something bad happened"));
    }
}
