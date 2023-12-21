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
    struct OnlyMoveOnce
    {
        bool& m_moved;

        explicit OnlyMoveOnce(bool& moved) : m_moved(moved) { }
        OnlyMoveOnce(const OnlyMoveOnce&) = delete;
        OnlyMoveOnce(OnlyMoveOnce&& other) : m_moved(other.m_moved)
        {
            REQUIRE(!m_moved);
            m_moved = true;
        }

        OnlyMoveOnce& operator=(const OnlyMoveOnce&) = delete;
        OnlyMoveOnce& operator=(OnlyMoveOnce&&) = delete;
    };

    int returns_int(DiagnosticContext&) { return 42; }
    std::unique_ptr<int> returns_unique_ptr(DiagnosticContext&) { return std::unique_ptr<int>{new int{42}}; }
    Optional<int> returns_optional_prvalue(DiagnosticContext&, int val) { return val; }
    const Optional<int> returns_optional_const_prvalue(DiagnosticContext&, int val) { return val; }
    Optional<int>&& returns_optional_xvalue(DiagnosticContext&, Optional<int>&& val) { return std::move(val); }
    const Optional<int>&& returns_optional_const_xvalue(DiagnosticContext&, Optional<int>&& val)
    {
        return std::move(val);
    }
    Optional<int> returns_optional_prvalue_fail(DiagnosticContext& context)
    {
        context.report(DiagnosticLine{DiagKind::Error, LocalizedString::from_raw("something bad happened")});
        return nullopt;
    }
    const Optional<int> returns_optional_const_prvalue_fail(DiagnosticContext& context)
    {
        context.report(DiagnosticLine{DiagKind::Error, LocalizedString::from_raw("something bad happened")});
        return nullopt;
    }
    Optional<int>&& returns_optional_xvalue_fail(DiagnosticContext& context, Optional<int>&& val)
    {
        val.clear();
        context.report(DiagnosticLine{DiagKind::Error, LocalizedString::from_raw("something bad happened")});
        return std::move(val);
    }

    const Optional<int>&& returns_optional_const_xvalue_fail(DiagnosticContext& context, Optional<int>&& val)
    {
        val.clear();
        context.report(DiagnosticLine{DiagKind::Error, LocalizedString::from_raw("something bad happened")});
        return std::move(val);
    }

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
} // unnamed namespace

TEST_CASE ("adapt DiagnosticContext to ExpectedL", "[diagnostics]")
{
    // adapt_context_to_expected(returns_int); // should not compile
    static_assert(!adapt_context_to_expected_invocable_with<decltype(returns_int)>,
                  "Callable needs to return optional");
    // adapt_context_to_expected(returns_unique_ptr); // should not compile
    static_assert(!adapt_context_to_expected_invocable_with<decltype(returns_unique_ptr)>,
                  "Callable needs to return optional");

    static_assert(adapt_context_to_expected_invocable_with<decltype(returns_optional_prvalue), int>,
                  "adapt_context_to_expected_invocable_with needs to succeed with a value that should "
                  "work");

    // test that the type of ExpectedL is determined correctly
    static_assert(std::is_same_v<ExpectedL<int>, decltype(adapt_context_to_expected(returns_optional_prvalue, 42))>,
                  "boom");
    static_assert(
        std::is_same_v<ExpectedL<int>, decltype(adapt_context_to_expected(returns_optional_const_prvalue, 42))>,
        "boom");
    static_assert(
        std::is_same_v<ExpectedL<int&&>,
                       decltype(adapt_context_to_expected(returns_optional_xvalue, std::declval<Optional<int>>()))>,
        "boom");
    static_assert(std::is_same_v<ExpectedL<const int&&>,
                                 decltype(adapt_context_to_expected(returns_optional_const_xvalue,
                                                                    std::declval<Optional<int>>()))>,
                  "boom");
    {
        auto adapted = adapt_context_to_expected(returns_optional_prvalue, 42);
        REQUIRE(adapted.value_or_exit(VCPKG_LINE_INFO) == 42);
    }
    {
        auto adapted = adapt_context_to_expected(returns_optional_prvalue_fail);
        REQUIRE(!adapted.has_value());
        REQUIRE(adapted.error() == LocalizedString::from_raw("error: something bad happened"));
    }
}
