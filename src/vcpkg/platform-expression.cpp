#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/platform-expression.h>

#include <numeric>
#include <string>
#include <vector>

namespace vcpkg::PlatformExpression
{
    enum class Identifier
    {
        invalid = -1, // not a recognized identifier
        x86,
        x64,
        arm,
        arm32,
        arm64,
        wasm32,

        windows,
        mingw,
        linux,
        freebsd,
        openbsd,
        osx,
        uwp,
        android,
        emscripten,
        ios,

        static_link,
        static_crt,

        native, // HOST_TRIPLET == TARGET_TRIPLET
    };

    static Identifier string2identifier(StringView name)
    {
        static const std::map<StringView, Identifier> id_map = {
            {"x86", Identifier::x86},
            {"x64", Identifier::x64},
            {"arm", Identifier::arm},
            {"arm32", Identifier::arm32},
            {"arm64", Identifier::arm64},
            {"wasm32", Identifier::wasm32},
            {"windows", Identifier::windows},
            {"mingw", Identifier::mingw},
            {"linux", Identifier::linux},
            {"freebsd", Identifier::freebsd},
            {"openbsd", Identifier::openbsd},
            {"osx", Identifier::osx},
            {"uwp", Identifier::uwp},
            {"android", Identifier::android},
            {"emscripten", Identifier::emscripten},
            {"ios", Identifier::ios},
            {"static", Identifier::static_link},
            {"staticcrt", Identifier::static_crt},
            {"native", Identifier::native},
        };

        auto id_pair = id_map.find(name);

        if (id_pair == id_map.end())
        {
            return Identifier::invalid;
        }

        return id_pair->second;
    }

    namespace detail
    {
        enum class ExprKind
        {
            identifier,
            op_not,
            op_and,
            op_or,
            op_list,
            op_empty,
            op_invalid
        };

        struct ExprImpl
        {
            ExprImpl(ExprKind k, std::string i, std::vector<std::unique_ptr<ExprImpl>> es)
                : kind(k), identifier(std::move(i)), exprs(std::move(es))
            {
            }

            ExprImpl(ExprKind k, std::string i) : kind(k), identifier(std::move(i)) { }
            ExprImpl(ExprKind k, std::unique_ptr<ExprImpl> a) : kind(k) { exprs.push_back(std::move(a)); }
            ExprImpl(ExprKind k, std::vector<std::unique_ptr<ExprImpl>> es) : kind(k), exprs(std::move(es)) { }

            ExprKind kind;
            std::string identifier;
            std::vector<std::unique_ptr<ExprImpl>> exprs;

            std::unique_ptr<ExprImpl> clone() const
            {
                return std::make_unique<ExprImpl>(
                    ExprImpl{kind, identifier, Util::fmap(exprs, [](auto&& p) { return p->clone(); })});
            }
        };

        struct ExpressionParser : ParserBase
        {
            ExpressionParser(StringView str, MultipleBinaryOperators multiple_binary_operators)
                : ParserBase(str, "CONTROL"), multiple_binary_operators(multiple_binary_operators)
            {
            }

            MultipleBinaryOperators multiple_binary_operators;

            bool allow_multiple_binary_operators() const
            {
                return multiple_binary_operators == MultipleBinaryOperators::Allow;
            }

            // top-level-platform-expression = optional-whitespace, platform-expression
            PlatformExpression::Expr parse()
            {
                skip_whitespace();
                auto res = expr();

                if (!at_eof())
                {
                    add_error("invalid logic expression, unexpected character");
                }

                return Expr(std::move(res));
            }

        private:
            // identifier-character =
            // | lowercase-alpha
            // | digit ;
            static bool is_identifier_char(char32_t ch) { return is_lower_alpha(ch) || is_ascii_digit(ch); }

            // platform-expression =
            // | platform-expression-not
            // | platform-expression-and
            // | platform-expression-or
            std::unique_ptr<ExprImpl> expr()
            {
                // this is the common prefix of all the variants
                // platform-expression-not,
                auto result = expr_not();

                // the first expression must be followed by a logical operator (or nothing)
                auto oper = expr_operator();
                switch (oper)
                {
                    case ExprKind::op_and:
                        // { "&", optional-whitespace, platform-expression-not }
                        // { "and", platform-expression-binary-keyword-second-operand }
                        return expr_binary<ExprKind::op_and, ExprKind::op_or>(
                            std::make_unique<ExprImpl>(oper, std::move(result)));

                    case ExprKind::op_or:
                        // { "|", optional-whitespace, platform-expression-not }
                        return expr_binary<ExprKind::op_or, ExprKind::op_and>(
                            std::make_unique<ExprImpl>(oper, std::move(result)));

                    case ExprKind::op_list:
                        // { ",", optional-whitespace, platform-expression }
                        return expr_binary<ExprKind::op_list, ExprKind::op_invalid>(
                            std::make_unique<ExprImpl>(oper, std::move(result)));

                    case ExprKind::op_empty: return result;

                    default:
                        // op_identifier and op_invalid both indicate a syntax error, which should have
                        // already been flagged by expr_operator.
                        return result;
                }
            }

            ExprKind expr_operator()
            {
                auto oper = cur();

                // Support chains of the vcpkg operators (`&`, `|`)  to avoid breaking backwards compatibility
                switch (oper)
                {
                    case '|':
                    case '&':
                        do
                        {
                            next();
                        } while (allow_multiple_binary_operators() && cur() == oper);
                        break;
                }

                switch (oper)
                {
                    case '|':
                    {
                        // { "|", optional-whitespace, platform-expression-not }
                        return ExprKind::op_or;
                    }
                    case '&':
                    {
                        // { "&", optional-whitespace, platform-expression-not }
                        return ExprKind::op_and;
                    }
                    case ',':
                    {
                        // { ",", optional-whitespace, platform-expression-not }
                        // "," is a near-synonym of "|", with the differences that it can be combined with "&"/"and",
                        // but has lower precedence
                        next();
                        return ExprKind::op_list;
                    }
                    case 'a':
                    case 'o':
                    {
                        // { "and", optional-whitespace, platform-expression-not }
                        // { "or", platform-expression-binary-keyword-second-operand } }
                        // "and" is a synonym of "&", "or" is reserved (but not yet supported) as a synonym of "|"
                        std::string name = match_zero_or_more(is_identifier_char).to_string();
                        Checks::check_exit(VCPKG_LINE_INFO, !name.empty());

                        if (name == "and")
                        {
                            return ExprKind::op_and;
                        }
                        else if (name == "or")
                        {
                            add_error("invalid logic expression, use '|' instead of 'or'");
                            return ExprKind::op_invalid;
                        }

                        // Invalid alphanumeric strings or strings other than "and" are errors.
                        add_error("unexpected character or identifier in logic expression");
                        return ExprKind::op_invalid;
                    }
                    default:
                        // Perhaps this should be an error, but in the previous implementation, this
                        // was a do-nothing case, so let's maintain that behavior.
                        return ExprKind::op_empty;
                }
            }

            // platform-expression-simple =
            // | platform-expression-identifier
            // | platform-expression-grouped ;
            std::unique_ptr<ExprImpl> expr_simple()
            {
                // platform-expression-grouped =
                // | "(", optional-whitespace, platform-expression, ")", optional-whitespace ;
                if (cur() == '(')
                {
                    // "(",
                    next();
                    // optional-whitespace,
                    skip_whitespace();
                    // platform-expression,
                    auto result = expr();
                    if (cur() != ')')
                    {
                        add_error("missing closing )");
                        return result;
                    }
                    // ")",
                    next();
                    // optional-whitespace
                    skip_whitespace();
                    return result;
                }

                // platform-expression-identifier
                return expr_identifier();
            }

            // platform-expression-identifier =
            // | identifier-character, { identifier-character }, optional-whitespace ;
            std::unique_ptr<ExprImpl> expr_identifier()
            {
                // identifier-character, { identifier-character },
                std::string name = match_zero_or_more(is_identifier_char).to_string();

                if (name.empty())
                {
                    add_error("missing or invalid identifier");
                }

                // optional-whitespace
                skip_whitespace();

                return std::make_unique<ExprImpl>(ExprKind::identifier, std::move(name));
            }

            // platform-expression-not =
            // | platform-expression-simple
            // | "!", optional-whitespace, platform-expression-simple
            // | "not", platform-expression-unary-keyword-operand ;
            std::unique_ptr<ExprImpl> expr_not()
            {
                if (cur() == '!')
                {
                    // "!",
                    next();
                    // optional-whitespace,
                    skip_whitespace();
                    // platform-expression-simple
                    return std::make_unique<ExprImpl>(ExprKind::op_not, expr_simple());
                }
                else if (cur() == 'n')
                {
                    std::string name = match_zero_or_more(is_identifier_char).to_string();

                    // "not"
                    if (name == "not")
                    {
                        // required-whitespace, platform-expression-simple
                        // optional-whitespace, platform-expression-grouped
                        skip_whitespace();
                        return std::make_unique<ExprImpl>(ExprKind::op_not, expr_simple());
                    }

                    // optional-whitespace
                    skip_whitespace();

                    return std::make_unique<ExprImpl>(ExprKind::identifier, std::move(name));
                }

                // platform-expression-simple
                return expr_simple();
            }

            // platform-expression-list =
            // | platform-expression {",", optional-whitespace, platform-expression};
            //
            // platform-expression-binary-keyword-first-operand =
            // | platform-expression-not, required-whitespace
            // | platform-expression-grouped ;
            //
            // platform-expression-binary-keyword-second-operand =
            // | required-whitespace, platform-expression-not
            // | platform-expression-grouped ;
            //
            // platform-expression-and =
            // | platform-expression-not, { "&", optional-whitespace, platform-expression-not }
            // | platform-expression-binary-keyword-first-operand, { "and",
            // platform-expression-binary-keyword-second-operand } ;
            //
            // platform-expression-or =
            // | platform-expression-not, { "|", optional-whitespace, platform-expression-not }
            // | platform-expression-binary-keyword-first-operand, { "or",
            // platform-expression-binary-keyword-second-operand } (* to allow for future extension *) ;
            //
            // Processing of the operator was already taken care of by the caller: continue
            // with the next platform-expression-not or platform-expression-binary-keyword-second-operand.
            template<ExprKind oper, ExprKind unmixable_oper>
            std::unique_ptr<ExprImpl> expr_binary(std::unique_ptr<ExprImpl>&& seed)
            {
                // gather consecutive instances of the same operation into a single expr node
                // e.g., parsing 'A & B & C' yields {&, vector<A,B,C>}
                ExprKind next_oper = ExprKind::op_invalid;
                do
                {
                    // optional-whitespace,
                    skip_whitespace();

                    if constexpr (oper == ExprKind::op_list)
                    {
                        // platform-expression { ",", optional-whitespace, platform-expression } ;
                        seed->exprs.push_back(expr());
                    }
                    else
                    {
                        // platform-expression-not, (go back to start of repetition)
                        seed->exprs.push_back(expr_not());
                    }
                    next_oper = expr_operator();
                } while (next_oper == oper);

                if constexpr (unmixable_oper != ExprKind::op_invalid)
                {
                    if (next_oper == unmixable_oper)
                    {
                        add_error("mixing & and | is not allowed; use () to specify order of operations");
                    }
                }

                if (next_oper == ExprKind::op_list)
                {
                    // platform-expression { ",", optional-whitespace, platform-expression } ;
                    //
                    // To handle a lower-precedence, treat the remainder of the string as a platform expression.
                    // E.g., "A & B , C | D" will be treated as "(A & B) , (C | D)", which preserves intended precedence
                    // In this case, see is the LHS at the point in which we see the ",".

                    return expr_binary<ExprKind::op_list, ExprKind::op_invalid>(
                        std::make_unique<ExprImpl>(next_oper, std::move(seed)));
                }
                else
                {
                    return std::move(seed);
                }
            }
        };
    }

    using namespace detail;

    Expr::Expr() = default;
    Expr::Expr(Expr&& other) = default;
    Expr& Expr::operator=(Expr&& other) = default;

    Expr::Expr(const Expr& other)
    {
        if (other.underlying_)
        {
            this->underlying_ = other.underlying_->clone();
        }
    }
    Expr& Expr::operator=(const Expr& other)
    {
        if (other.underlying_)
        {
            this->underlying_ = other.underlying_->clone();
        }
        else
        {
            this->underlying_.reset();
        }

        return *this;
    }

    Expr::Expr(std::unique_ptr<ExprImpl>&& e) : underlying_(std::move(e)) { }
    Expr::~Expr() = default;

    Expr Expr::Identifier(StringView id)
    {
        return Expr(std::make_unique<ExprImpl>(ExprKind::identifier, id.to_string()));
    }
    Expr Expr::Not(Expr&& e) { return Expr(std::make_unique<ExprImpl>(ExprKind::op_not, std::move(e.underlying_))); }
    Expr Expr::And(std::vector<Expr>&& exprs)
    {
        return Expr(std::make_unique<ExprImpl>(
            ExprKind::op_and, Util::fmap(exprs, [](Expr& expr) { return std::move(expr.underlying_); })));
    }
    Expr Expr::Or(std::vector<Expr>&& exprs)
    {
        return Expr(std::make_unique<ExprImpl>(
            ExprKind::op_or, Util::fmap(exprs, [](Expr& expr) { return std::move(expr.underlying_); })));
    }

    bool Expr::evaluate(const Context& context) const
    {
        if (!this->underlying_)
        {
            return true; // empty expression is always true
        }

        std::map<std::string, bool> override_ctxt;
        {
            auto override_vars = context.find("VCPKG_DEP_INFO_OVERRIDE_VARS");
            if (override_vars != context.end())
            {
                auto cmake_list = Strings::split(override_vars->second, ';');
                for (auto& override_id : cmake_list)
                {
                    if (!override_id.empty())
                    {
                        if (override_id[0] == '!')
                        {
                            override_ctxt.insert({override_id.substr(1), false});
                        }
                        else
                        {
                            override_ctxt.insert({override_id, true});
                        }
                    }
                }
            }
        }

        struct Visitor
        {
            const Context& context;
            const std::map<std::string, bool>& override_ctxt;

            bool true_if_exists_and_equal(const std::string& variable_name, const std::string& value) const
            {
                auto iter = context.find(variable_name);
                if (iter == context.end())
                {
                    return false;
                }
                return iter->second == value;
            }

            bool visit(const ExprImpl& expr) const
            {
                if (expr.kind == ExprKind::identifier)
                {
                    if (!override_ctxt.empty())
                    {
                        auto override_id = override_ctxt.find(expr.identifier);
                        if (override_id != override_ctxt.end())
                        {
                            return override_id->second;
                        }
                        // Fall through to use the cmake logic if the id does not have an override
                    }

                    auto id = string2identifier(expr.identifier);
                    switch (id)
                    {
                        case Identifier::invalid:
                            // Point out in the diagnostic that they should add to the override list because that is
                            // what most users should do, however it is also valid to update the built in identifiers to
                            // recognize the name.
                            vcpkg::printf(
                                Color::error,
                                "Error: Unrecognized identifer name %s. Add to override list in triplet file.\n",
                                expr.identifier);
                            return false;
                        case Identifier::x64: return true_if_exists_and_equal("VCPKG_TARGET_ARCHITECTURE", "x64");
                        case Identifier::x86: return true_if_exists_and_equal("VCPKG_TARGET_ARCHITECTURE", "x86");
                        case Identifier::arm:
                            // For backwards compatability arm is also true for arm64.
                            // This is because it previously was only checking for a substring.
                            return true_if_exists_and_equal("VCPKG_TARGET_ARCHITECTURE", "arm") ||
                                   true_if_exists_and_equal("VCPKG_TARGET_ARCHITECTURE", "arm64");
                        case Identifier::arm32: return true_if_exists_and_equal("VCPKG_TARGET_ARCHITECTURE", "arm");
                        case Identifier::arm64: return true_if_exists_and_equal("VCPKG_TARGET_ARCHITECTURE", "arm64");
                        case Identifier::windows:
                            return true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "") ||
                                   true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore") ||
                                   true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "MinGW");
                        case Identifier::mingw: return true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "MinGW");
                        case Identifier::linux: return true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "Linux");
                        case Identifier::freebsd: return true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "FreeBSD");
                        case Identifier::openbsd: return true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "OpenBSD");
                        case Identifier::osx: return true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "Darwin");
                        case Identifier::uwp:
                            return true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore");
                        case Identifier::android: return true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "Android");
                        case Identifier::emscripten:
                            return true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "Emscripten");
                        case Identifier::ios: return true_if_exists_and_equal("VCPKG_CMAKE_SYSTEM_NAME", "iOS");
                        case Identifier::wasm32: return true_if_exists_and_equal("VCPKG_TARGET_ARCHITECTURE", "wasm32");
                        case Identifier::static_link:
                            return true_if_exists_and_equal("VCPKG_LIBRARY_LINKAGE", "static");
                        case Identifier::static_crt: return true_if_exists_and_equal("VCPKG_CRT_LINKAGE", "static");
                        case Identifier::native:
                        {
                            auto is_native = context.find("Z_VCPKG_IS_NATIVE");
                            if (is_native == context.end())
                            {
                                Checks::unreachable(VCPKG_LINE_INFO);
                            }

                            return is_native->second == "1";
                        }
                        default: Checks::unreachable(VCPKG_LINE_INFO);
                    }
                }
                else if (expr.kind == ExprKind::op_not)
                {
                    return !visit(*expr.exprs.at(0));
                }
                else if (expr.kind == ExprKind::op_and)
                {
                    bool valid = true;

                    // we want to print errors in all expressions, so we check all of the expressions all the time
                    for (const auto& e : expr.exprs)
                    {
                        valid &= visit(*e);
                    }

                    return valid;
                }
                else if ((expr.kind == ExprKind::op_or) || (expr.kind == ExprKind::op_list))
                {
                    bool valid = false;

                    // we want to print errors in all expressions, so we check all of the expressions all the time
                    for (const auto& e : expr.exprs)
                    {
                        valid |= visit(*e);
                    }

                    return valid;
                }
                else
                {
                    Checks::unreachable(VCPKG_LINE_INFO);
                }
            }
        };

        return Visitor{context, override_ctxt}.visit(*this->underlying_);
    }

    int Expr::complexity() const
    {
        if (is_empty()) return 0;

        struct Impl
        {
            int operator()(const std::unique_ptr<detail::ExprImpl>& expr) const { return (*this)(*expr); }
            int operator()(const detail::ExprImpl& expr) const
            {
                if (expr.kind == ExprKind::identifier) return 1;

                if (expr.kind == ExprKind::op_not) return 1 + (*this)(expr.exprs.at(0));

                return 1 + std::accumulate(expr.exprs.begin(), expr.exprs.end(), 0, [](int acc, const auto& el) {
                           return acc + Impl{}(el);
                       });
            }
        };

        return Impl{}(underlying_);
    }

    ExpectedS<Expr> parse_platform_expression(StringView expression, MultipleBinaryOperators multiple_binary_operators)
    {
        ExpressionParser parser(expression, multiple_binary_operators);
        auto res = parser.parse();

        if (auto p = parser.extract_error())
        {
            return p->format();
        }
        else
        {
            return res;
        }
    }

    bool structurally_equal(const Expr& lhs, const Expr& rhs)
    {
        struct Impl
        {
            bool operator()(const std::unique_ptr<detail::ExprImpl>& lhs,
                            const std::unique_ptr<detail::ExprImpl>& rhs) const
            {
                return (*this)(*lhs, *rhs);
            }
            bool operator()(const detail::ExprImpl& lhs, const detail::ExprImpl& rhs) const
            {
                if (lhs.kind != rhs.kind) return false;

                if (lhs.kind == ExprKind::identifier)
                {
                    return lhs.identifier == rhs.identifier;
                }
                else
                {
                    const auto& exprs_l = lhs.exprs;
                    const auto& exprs_r = rhs.exprs;
                    return std::equal(exprs_l.begin(), exprs_l.end(), exprs_r.begin(), exprs_r.end(), *this);
                }
            }
        };

        if (lhs.is_empty())
        {
            return rhs.is_empty();
        }
        if (rhs.is_empty())
        {
            return false;
        }
        return Impl{}(lhs.underlying_, rhs.underlying_);
    }

    int compare(const Expr& lhs, const Expr& rhs)
    {
        auto lhs_platform_complexity = lhs.complexity();
        auto rhs_platform_complexity = rhs.complexity();

        if (lhs_platform_complexity < rhs_platform_complexity) return -1;
        if (rhs_platform_complexity < lhs_platform_complexity) return 1;

        auto lhs_platform = to_string(lhs);
        auto rhs_platform = to_string(rhs);

        if (lhs_platform.size() < rhs_platform.size()) return -1;
        if (rhs_platform.size() < lhs_platform.size()) return 1;

        auto platform_cmp = lhs_platform.compare(rhs_platform);
        if (platform_cmp < 0) return -1;
        if (platform_cmp > 0) return 1;

        return 0;
    }

    std::string to_string(const Expr& expr)
    {
        struct Impl
        {
            std::string operator()(const std::unique_ptr<detail::ExprImpl>& expr) const
            {
                return (*this)(*expr, false);
            }
            std::string operator()(const detail::ExprImpl& expr, bool outer) const
            {
                const char* join = nullptr;
                switch (expr.kind)
                {
                    case ExprKind::identifier: return expr.identifier;
                    case ExprKind::op_and: join = " & "; break;
                    case ExprKind::op_or: join = " | "; break;
                    case ExprKind::op_list: join = ", "; break;
                    case ExprKind::op_not: return Strings::format("!%s", (*this)(expr.exprs.at(0)));
                    case ExprKind::op_empty: join = ""; break;
                    case ExprKind::op_invalid: join = " invalid "; break;
                    default: Checks::unreachable(VCPKG_LINE_INFO);
                }

                if (outer)
                {
                    return Strings::join(join, expr.exprs, *this);
                }
                else
                {
                    return Strings::format("(%s)", Strings::join(join, expr.exprs, *this));
                }
            }
        };

        if (expr.is_empty())
        {
            return std::string{};
        }
        return Impl{}(*expr.underlying_, true);
    }
}
