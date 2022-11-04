#pragma once

#include <vcpkg/base/fwd/json.h>

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/view.h>

namespace vcpkg::Json
{
    struct Reader;

    template<class Type>
    struct IDeserializer
    {
        using type = Type;
        virtual StringView type_name() const = 0;

    private:
        friend struct Reader;
        Optional<Type> visit(Reader&, const Value&);
        Optional<Type> visit(Reader&, const Object&);

    public:
        virtual Optional<Type> visit_null(Reader&);
        virtual Optional<Type> visit_boolean(Reader&, bool);
        virtual Optional<Type> visit_integer(Reader& r, int64_t i);
        virtual Optional<Type> visit_number(Reader&, double);
        virtual Optional<Type> visit_string(Reader&, StringView);
        virtual Optional<Type> visit_array(Reader&, const Array&);
        virtual Optional<Type> visit_object(Reader&, const Object&);
        virtual View<StringView> valid_fields() const;

        virtual ~IDeserializer() = default;

    protected:
        IDeserializer() = default;
        IDeserializer(const IDeserializer&) = default;
        IDeserializer& operator=(const IDeserializer&) = default;
        IDeserializer(IDeserializer&&) = default;
        IDeserializer& operator=(IDeserializer&&) = default;
    };

    struct Reader
    {
        Reader();

        const std::vector<std::string>& errors() const { return m_errors; }
        std::vector<std::string>& errors() { return m_errors; }

        void add_missing_field_error(StringView type, StringView key, StringView key_type);
        void add_expected_type_error(StringView expected_type);
        void add_extra_field_error(StringView type, StringView fields, StringView suggestion = {});
        template<class... Args>
        void add_generic_error(StringView type, Args&&... args)
        {
            m_errors.push_back(Strings::concat(path(), " (", type, "): ", args...));
        }

        void add_warning(StringView type, StringView msg);

        const std::vector<LocalizedString>& warnings() const { return m_warnings; }

        std::string path() const noexcept;

    private:
        template<class Type>
        friend struct IDeserializer;

        std::vector<std::string> m_errors;
        std::vector<LocalizedString> m_warnings;
        struct JsonPathElement
        {
            constexpr JsonPathElement() = default;
            constexpr JsonPathElement(int64_t i) : index(i) { }
            constexpr JsonPathElement(StringView f) : field(f) { }

            int64_t index = -1;
            StringView field;
        };

        struct PathGuard
        {
            PathGuard(std::vector<JsonPathElement>& path) : m_path{path} { m_path.emplace_back(); }
            PathGuard(std::vector<JsonPathElement>& path, int64_t i) : m_path{path} { m_path.emplace_back(i); }
            PathGuard(std::vector<JsonPathElement>& path, StringView f) : m_path{path} { m_path.emplace_back(f); }
            PathGuard(const PathGuard&) = delete;
            PathGuard& operator=(const PathGuard&) = delete;
            ~PathGuard() { m_path.pop_back(); }

        private:
            std::vector<JsonPathElement>& m_path;
        };

        std::vector<JsonPathElement> m_path;

    public:
        // checks that an object doesn't contain any fields which both:
        // * don't start with a `$`
        // * are not in `valid_fields`
        // if known_fields.empty(), then it's treated as if all field names are valid
        void check_for_unexpected_fields(const Object& obj, View<StringView> valid_fields, StringView type_name);

        template<class Type>
        void required_object_field(
            StringView type, const Object& obj, StringView key, Type& place, IDeserializer<Type>& visitor)
        {
            if (auto value = obj.get(key))
            {
                visit_in_key(*value, key, place, visitor);
            }
            else
            {
                this->add_missing_field_error(type, key, visitor.type_name());
            }
        }

        // value should be the value at key of the currently visited object
        template<class Type>
        void visit_in_key(const Value& value, StringView key, Type& place, IDeserializer<Type>& visitor)
        {
            PathGuard guard{m_path, key};
            auto opt = visitor.visit(*this, value);
            if (auto p_opt = opt.get())
            {
                place = std::move(*p_opt);
            }
            else
            {
                add_expected_type_error(visitor.type_name());
            }
        }

        // value should be the value at key of the currently visited object
        template<class Type>
        void visit_at_index(const Value& value, int64_t index, Type& place, IDeserializer<Type>& visitor)
        {
            PathGuard guard{m_path, index};
            auto opt = visitor.visit(*this, value);
            if (auto p_opt = opt.get())
            {
                place = std::move(*p_opt);
            }
            else
            {
                add_expected_type_error(visitor.type_name());
            }
        }

        // returns whether key \in obj
        template<class Type>
        bool optional_object_field(const Object& obj, StringView key, Type& place, IDeserializer<Type>& visitor)
        {
            if (auto value = obj.get(key))
            {
                visit_in_key(*value, key, place, visitor);
                return true;
            }
            else
            {
                return false;
            }
        }

        template<class Type>
        Optional<Type> visit(const Value& value, IDeserializer<Type>& visitor)
        {
            return visitor.visit(*this, value);
        }
        template<class Type>
        Optional<Type> visit(const Object& value, IDeserializer<Type>& visitor)
        {
            return visitor.visit(*this, value);
        }

        template<class Type>
        Optional<std::vector<Type>> array_elements(const Array& arr, IDeserializer<Type>& visitor)
        {
            Optional<std::vector<Type>> result{std::vector<Type>()};
            auto& result_vec = *result.get();
            bool success = true;
            PathGuard guard{m_path};
            for (size_t i = 0; i < arr.size(); ++i)
            {
                m_path.back().index = static_cast<int64_t>(i);
                auto opt = visitor.visit(*this, arr[i]);
                if (auto parsed = opt.get())
                {
                    if (success)
                    {
                        result_vec.push_back(std::move(*parsed));
                    }
                }
                else
                {
                    this->add_expected_type_error(visitor.type_name());
                    result_vec.clear();
                    success = false;
                }
            }

            return result;
        }

        static uint64_t get_reader_stats();

    private:
        StatsTimer m_stat_timer;
    };

    template<class Type>
    Optional<Type> IDeserializer<Type>::visit(Reader& r, const Value& value)
    {
        switch (value.kind())
        {
            case ValueKind::Null: return visit_null(r);
            case ValueKind::Boolean: return visit_boolean(r, value.boolean(VCPKG_LINE_INFO));
            case ValueKind::Integer: return visit_integer(r, value.integer(VCPKG_LINE_INFO));
            case ValueKind::Number: return visit_number(r, value.number(VCPKG_LINE_INFO));
            case ValueKind::String: return visit_string(r, value.string(VCPKG_LINE_INFO));
            case ValueKind::Array: return visit_array(r, value.array(VCPKG_LINE_INFO));
            case ValueKind::Object:
                return visit(r, value.object(VCPKG_LINE_INFO)); // Call `visit` to get unexpected fields checking
            default: vcpkg::Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    template<class Type>
    Optional<Type> IDeserializer<Type>::visit(Reader& r, const Object& obj)
    {
        r.check_for_unexpected_fields(obj, valid_fields(), type_name());
        return visit_object(r, obj);
    }

    template<class Type>
    View<StringView> IDeserializer<Type>::valid_fields() const
    {
        return {};
    }

    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_null(Reader&)
    {
        return nullopt;
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_boolean(Reader&, bool)
    {
        return nullopt;
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_integer(Reader& r, int64_t i)
    {
        return this->visit_number(r, static_cast<double>(i));
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_number(Reader&, double)
    {
        return nullopt;
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_string(Reader&, StringView)
    {
        return nullopt;
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_array(Reader&, const Array&)
    {
        return nullopt;
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_object(Reader&, const Object&)
    {
        return nullopt;
    }

    struct StringDeserializer final : IDeserializer<std::string>
    {
        virtual StringView type_name() const override { return type_name_; }
        virtual Optional<std::string> visit_string(Reader&, StringView sv) override { return sv.to_string(); }

        constexpr explicit StringDeserializer(StringLiteral type_name_) : type_name_(type_name_) { }

    private:
        StringLiteral type_name_;
    };

    struct PathDeserializer final : IDeserializer<Path>
    {
        virtual StringView type_name() const override { return "a path"; }
        virtual Optional<Path> visit_string(Reader&, StringView sv) override { return sv; }

        static PathDeserializer instance;
    };

    struct NaturalNumberDeserializer final : IDeserializer<int>
    {
        virtual StringView type_name() const override { return "a nonnegative integer"; }

        virtual Optional<int> visit_integer(Reader&, int64_t value) override
        {
            if (value > std::numeric_limits<int>::max() || value < 0)
            {
                return nullopt;
            }
            return static_cast<int>(value);
        }

        static NaturalNumberDeserializer instance;
    };

    struct BooleanDeserializer final : IDeserializer<bool>
    {
        virtual StringView type_name() const override { return "a boolean"; }

        virtual Optional<bool> visit_boolean(Reader&, bool b) override { return b; }

        static BooleanDeserializer instance;
    };

    template<class Underlying>
    struct ArrayDeserializer final : IDeserializer<std::vector<typename Underlying::type>>
    {
        using type = std::vector<typename Underlying::type>;

        virtual StringView type_name() const override { return m_type_name; }

        constexpr ArrayDeserializer(StringLiteral type_name_, Underlying&& t = {})
            : m_type_name(type_name_), m_underlying_visitor(static_cast<Underlying&&>(t))
        {
        }

        virtual Optional<type> visit_array(Reader& r, const Array& arr) override
        {
            return r.array_elements(arr, m_underlying_visitor);
        }

    private:
        StringLiteral m_type_name;
        Underlying m_underlying_visitor;
    };

    struct ParagraphDeserializer final : IDeserializer<std::vector<std::string>>
    {
        virtual StringView type_name() const override { return "a string or array of strings"; }

        virtual Optional<std::vector<std::string>> visit_string(Reader&, StringView sv) override;
        virtual Optional<std::vector<std::string>> visit_array(Reader& r, const Array& arr) override;

        static ParagraphDeserializer instance;
    };

    struct IdentifierDeserializer final : Json::IDeserializer<std::string>
    {
        virtual StringView type_name() const override { return "an identifier"; }

        // [a-z0-9]+(-[a-z0-9]+)*, plus not any of {prn, aux, nul, con, lpt[1-9], com[1-9], core, default}
        static bool is_ident(StringView sv);

        virtual Optional<std::string> visit_string(Json::Reader&, StringView sv) override;

        static IdentifierDeserializer instance;
    };

    struct IdentifierArrayDeserializer final : Json::IDeserializer<std::vector<std::string>>
    {
        virtual StringView type_name() const override { return "an array of identifiers"; }

        virtual Optional<std::vector<std::string>> visit_array(Reader& r, const Array& arr) override;

        static IdentifierArrayDeserializer instance;
    };

    struct PackageNameDeserializer final : Json::IDeserializer<std::string>
    {
        virtual StringView type_name() const override { return "a package name"; }

        static bool is_package_name(StringView sv);

        virtual Optional<std::string> visit_string(Json::Reader&, StringView sv) override;

        static PackageNameDeserializer instance;
    };

    struct PackagePatternDeserializer final : Json::IDeserializer<std::string>
    {
        virtual StringView type_name() const override { return "a package pattern"; }

        static bool is_package_pattern(StringView sv);

        virtual Optional<std::string> visit_string(Json::Reader&, StringView sv) override;

        static PackagePatternDeserializer instance;
    };
}
