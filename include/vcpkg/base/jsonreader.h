#pragma once

#include <vcpkg/base/fwd/json.h>

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>

namespace vcpkg::Json
{
    template<class Type>
    struct IDeserializer
    {
        using type = Type;
        virtual LocalizedString type_name() const = 0;

    private:
        friend struct Reader;
        Optional<Type> visit(Reader&, const Value&) const;
        Optional<Type> visit(Reader&, const Object&) const;

    public:
        virtual Optional<Type> visit_null(Reader&) const;
        virtual Optional<Type> visit_boolean(Reader&, bool) const;
        virtual Optional<Type> visit_integer(Reader& r, int64_t i) const;
        virtual Optional<Type> visit_number(Reader&, double) const;
        virtual Optional<Type> visit_string(Reader&, StringView) const;
        virtual Optional<Type> visit_array(Reader&, const Array&) const;
        virtual Optional<Type> visit_object(Reader&, const Object&) const;
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
        explicit Reader(StringView origin);

        const std::vector<LocalizedString>& errors() const { return m_errors; }

        void add_missing_field_error(const LocalizedString& type, StringView key, const LocalizedString& key_type);
        void add_expected_type_error(const LocalizedString& expected_type);
        void add_extra_field_error(const LocalizedString& type, StringView fields, StringView suggestion = {});
        void add_generic_error(const LocalizedString& type, StringView message);

        void add_warning(LocalizedString type, StringView msg);

        const std::vector<LocalizedString>& warnings() const { return m_warnings; }

        LocalizedString join() const;

        std::string path() const noexcept;
        StringView origin() const noexcept;

    private:
        template<class Type>
        friend struct IDeserializer;

        std::vector<LocalizedString> m_errors;
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

        std::string m_origin;
        std::vector<JsonPathElement> m_path;

    public:
        // checks that an object doesn't contain any fields which both:
        // * don't start with a `$`
        // * are not in `valid_fields`
        // if known_fields.empty(), then it's treated as if all field names are valid
        void check_for_unexpected_fields(const Object& obj,
                                         View<StringView> valid_fields,
                                         const LocalizedString& type_name);

        template<class Type>
        void required_object_field(const LocalizedString& type,
                                   const Object& obj,
                                   StringView key,
                                   Type& place,
                                   const IDeserializer<Type>& visitor)
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
        void visit_in_key(const Value& value, StringView key, Type& place, const IDeserializer<Type>& visitor)
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
        void visit_at_index(const Value& value, int64_t index, Type& place, const IDeserializer<Type>& visitor)
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
        bool optional_object_field(const Object& obj, StringView key, Type& place, const IDeserializer<Type>& visitor)
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
        Optional<Type> visit(const Value& value, const IDeserializer<Type>& visitor)
        {
            return visitor.visit(*this, value);
        }
        template<class Type>
        Optional<Type> visit(const Object& value, const IDeserializer<Type>& visitor)
        {
            return visitor.visit(*this, value);
        }

        template<class Type>
        Optional<std::vector<Type>> array_elements(const Array& arr, const IDeserializer<Type>& visitor)
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
    Optional<Type> IDeserializer<Type>::visit(Reader& r, const Value& value) const
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
    Optional<Type> IDeserializer<Type>::visit(Reader& r, const Object& obj) const
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
    Optional<Type> IDeserializer<Type>::visit_null(Reader&) const
    {
        return nullopt;
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_boolean(Reader&, bool) const
    {
        return nullopt;
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_integer(Reader& r, int64_t i) const
    {
        return this->visit_number(r, static_cast<double>(i));
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_number(Reader&, double) const
    {
        return nullopt;
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_string(Reader&, StringView) const
    {
        return nullopt;
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_array(Reader&, const Array&) const
    {
        return nullopt;
    }
    template<class Type>
    Optional<Type> IDeserializer<Type>::visit_object(Reader&, const Object&) const
    {
        return nullopt;
    }

    struct StringDeserializer : IDeserializer<std::string>
    {
        virtual Optional<std::string> visit_string(Reader&, StringView sv) const override;
    };

    struct UntypedStringDeserializer final : StringDeserializer
    {
        virtual LocalizedString type_name() const override;
        static const UntypedStringDeserializer instance;
    };

    struct PathDeserializer final : IDeserializer<Path>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<Path> visit_string(Reader&, StringView sv) const override;

        static const PathDeserializer instance;
    };

    struct NaturalNumberDeserializer final : IDeserializer<int>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<int> visit_integer(Reader&, int64_t value) const override;
        static const NaturalNumberDeserializer instance;
    };

    struct BooleanDeserializer final : IDeserializer<bool>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<bool> visit_boolean(Reader&, bool b) const override;
        static const BooleanDeserializer instance;
    };

    template<class Underlying>
    struct ArrayDeserializer : IDeserializer<std::vector<typename Underlying::type>>
    {
        using type = std::vector<typename Underlying::type>;

        virtual Optional<type> visit_array(Reader& r, const Array& arr) const override
        {
            return r.array_elements(arr, Underlying::instance);
        }
    };

    struct ParagraphDeserializer final : IDeserializer<std::vector<std::string>>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<std::vector<std::string>> visit_string(Reader&, StringView sv) const override;
        virtual Optional<std::vector<std::string>> visit_array(Reader& r, const Array& arr) const override;

        static const ParagraphDeserializer instance;
    };

    struct IdentifierDeserializer final : Json::IDeserializer<std::string>
    {
        virtual LocalizedString type_name() const override;
        // [a-z0-9]+(-[a-z0-9]+)*, plus not any of {prn, aux, nul, con, lpt[1-9], com[1-9], core, default}
        static bool is_ident(StringView sv);
        virtual Optional<std::string> visit_string(Json::Reader&, StringView sv) const override;
        static const IdentifierDeserializer instance;
    };

    struct IdentifierArrayDeserializer final : Json::IDeserializer<std::vector<std::string>>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<std::vector<std::string>> visit_array(Reader& r, const Array& arr) const override;
        static const IdentifierArrayDeserializer instance;
    };

    struct PackageNameDeserializer final : Json::IDeserializer<std::string>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<std::string> visit_string(Json::Reader&, StringView sv) const override;
        static const PackageNameDeserializer instance;
    };

    struct FeatureNameDeserializer final : Json::IDeserializer<std::string>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<std::string> visit_string(Json::Reader&, StringView sv) const override;
        static const FeatureNameDeserializer instance;
    };
}
