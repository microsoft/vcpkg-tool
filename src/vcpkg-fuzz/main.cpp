#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/unicode.h>

#include <vcpkg/platform-expression.h>

#include <string.h>

#include <iostream>
#include <sstream>
#include <utility>

using namespace vcpkg;

namespace
{
    enum class FuzzKind
    {
        None,
        Utf8Decoder,
        JsonParser,
        PlatformExpr,
    };

    struct FuzzArgs
    {
        FuzzArgs(int argc, char** argv)
        {
            if (argc <= 1)
            {
                print_help_and_exit();
            }

            char** it = argv + 1; // skip the name of the program
            char** last = argv + argc;

            for (; it != last; ++it)
            {
                auto arg = StringView(*it, strlen(*it));
                if (arg == "/?")
                {
                    print_help_and_exit();
                }

                auto pr = split_arg(arg);
                auto key = pr.first;
                auto value = pr.second;
                if (key == "h" || key == "help")
                {
                    print_help_and_exit();
                }

                if (key == "kind")
                {
                    if (value == "json")
                    {
                        kind = FuzzKind::JsonParser;
                    }
                    else if (value == "utf-8")
                    {
                        kind = FuzzKind::Utf8Decoder;
                    }
                    else if (value == "platform-expr")
                    {
                        kind = FuzzKind::PlatformExpr;
                    }
                    else
                    {
                        msg::write_unlocalized_text_to_stdout(
                            Color::error,
                            fmt::format("error: invalid kind: '{}'\n"
                                        "expected one of: utf-8, json, platform-expr\n",
                                        value));
                        print_help_and_exit(true);
                    }
                }
                else
                {
                    msg::write_unlocalized_text_to_stdout(Color::error, "error: unknown option: --" + key + "\n");
                    print_help_and_exit(true);
                }
            }
        }

        // returns {arg, ""} when there isn't an `=`
        // skips preceding `-`s
        std::pair<StringView, StringView> split_arg(StringView arg) const
        {
            auto first = std::find_if(arg.begin(), arg.end(), [](char c) { return c != '-'; });
            auto division = std::find(first, arg.end(), '=');
            if (division == arg.end())
            {
                return {StringView(first, arg.end()), StringView(arg.end(), arg.end())};
            }
            else
            {
                return {StringView(first, division), StringView(division + 1, arg.end())};
            }
        }

        [[noreturn]] void print_help_and_exit(bool invalid = false) const
        {
            auto color = invalid ? Color::error : Color::none;

            StringLiteral message = "usage: vcpkg-fuzz --kind=<kind>\n\n"
                                    "accepts input on stdin.\n\n"
                                    "options:\n"
                                    "  --kind=...            one of {{utf-8, json, platform-expr}}\n";

            msg::write_unlocalized_text_to_stdout(color, message);
            if (invalid)
            {
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
            else
            {
                Checks::exit_success(VCPKG_LINE_INFO);
            }
        }

        FuzzKind kind = FuzzKind::None;
    };

    std::string read_all_of_stdin()
    {
        std::stringstream ss;
        ss << std::cin.rdbuf();
        return std::move(ss).str();
    }

    [[noreturn]] void fuzz_json_and_exit(StringView text)
    {
        auto res = Json::parse(text);
        if (!res)
        {
            Checks::exit_with_message(VCPKG_LINE_INFO, res.error()->to_string());
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    [[noreturn]] void fuzz_utf8_and_exit(StringView text)
    {
        auto res = Unicode::Utf8Decoder(text.begin(), text.end());
        for (auto ch : res)
        {
            (void)ch;
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    [[noreturn]] void fuzz_platform_expr_and_exit(StringView text)
    {
        auto res1 =
            PlatformExpression::parse_platform_expression(text, PlatformExpression::MultipleBinaryOperators::Deny);
        auto res2 =
            PlatformExpression::parse_platform_expression(text, PlatformExpression::MultipleBinaryOperators::Allow);

        if (!res1)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, res1.error());
        }
        if (!res2)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, res2.error());
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}

namespace vcpkg::Checks
{
    void on_final_cleanup_and_exit() { }
}

int main(int argc, char** argv)
{
    auto args = FuzzArgs(argc, argv);

    if (args.kind == FuzzKind::None)
    {
        args.print_help_and_exit(true);
    }

    auto text = read_all_of_stdin();
    switch (args.kind)
    {
        case FuzzKind::JsonParser: fuzz_json_and_exit(text);
        case FuzzKind::Utf8Decoder: fuzz_utf8_and_exit(text);
        case FuzzKind::PlatformExpr: fuzz_platform_expr_and_exit(text);
        default: Checks::unreachable(VCPKG_LINE_INFO);
    }
}
