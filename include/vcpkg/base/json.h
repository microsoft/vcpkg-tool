#pragma once

#include <vcpkg/base/fwd/json.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/stringview.h>

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vcpkg::Json
{
    struct JsonStyle
    {
        enum class Newline
        {
            Lf,
            CrLf
        } newline_kind = Newline::Lf;

        constexpr JsonStyle() noexcept = default;

        static JsonStyle with_tabs() noexcept { return JsonStyle{-1}; }
        static JsonStyle with_spaces(int indent) noexcept
        {
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO, indent >= 0);
            return JsonStyle{indent};
        }

        void set_tabs() noexcept { this->indent = -1; }
        void set_spaces(int indent_) noexcept
        {
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO, indent >= 0);
            this->indent = indent_;
        }

        bool use_tabs() const noexcept { return indent == -1; }
        bool use_spaces() const noexcept { return indent >= 0; }

        int spaces() const noexcept
        {
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO, indent >= 0);
            return indent;
        }

        const char* newline() const noexcept
        {
            switch (this->newline_kind)
            {
                case Newline::Lf: return "\n";
                case Newline::CrLf: return "\r\n";
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

    private:
        constexpr explicit JsonStyle(int indent) : indent(indent) { }
        // -1 for tab, >=0 gives # of spaces
        int indent = 2;
    };

    enum class ValueKind : int
    {
        Null,
        Boolean,
        Integer,
        Number,
        String,
        Array,
        Object
    };

    namespace impl
    {
        struct ValueImpl;
    }

    struct Value
    {
        Value() noexcept; // equivalent to Value::null()
        Value(Value&&) noexcept;
        Value(const Value&);
        Value& operator=(Value&&) noexcept;
        Value& operator=(const Value&);
        ~Value();

        ValueKind kind() const noexcept;

        bool is_null() const noexcept;
        bool is_boolean() const noexcept;
        bool is_integer() const noexcept;
        // either integer _or_ number
        bool is_number() const noexcept;
        bool is_string() const noexcept;
        bool is_array() const noexcept;
        bool is_object() const noexcept;

        // a.x() asserts when !a.is_x()
        bool boolean() const noexcept;
        int64_t integer() const noexcept;
        double number() const noexcept;
        StringView string() const noexcept;

        const Array& array() const& noexcept;
        Array& array() & noexcept;
        Array&& array() && noexcept;

        const Object& object() const& noexcept;
        Object& object() & noexcept;
        Object&& object() && noexcept;

        static Value null(std::nullptr_t) noexcept;
        static Value boolean(bool) noexcept;
        static Value integer(int64_t i) noexcept;
        static Value number(double d) noexcept;
        static Value string(std::string s) noexcept;
        static Value array(Array&&) noexcept;
        static Value array(const Array&) noexcept;
        static Value object(Object&&) noexcept;
        static Value object(const Object&) noexcept;

        friend bool operator==(const Value& lhs, const Value& rhs);
        friend bool operator!=(const Value& lhs, const Value& rhs) { return !(lhs == rhs); }

    private:
        friend struct impl::ValueImpl;
        std::unique_ptr<impl::ValueImpl> underlying_;
    };

    struct Array
    {
    private:
        using underlying_t = std::vector<Value>;

    public:
        Array() = default;
        Array(Array const&) = default;
        Array(Array&&) = default;
        Array& operator=(Array const&) = default;
        Array& operator=(Array&&) = default;
        ~Array() = default;

        using iterator = underlying_t::iterator;
        using const_iterator = underlying_t::const_iterator;

        Value& push_back(Value&& value);
        Object& push_back(Object&& value);
        Array& push_back(Array&& value);
        Value& insert_before(iterator it, Value&& value);
        Object& insert_before(iterator it, Object&& value);
        Array& insert_before(iterator it, Array&& value);

        std::size_t size() const noexcept { return this->underlying_.size(); }

        // asserts idx < size
        Value& operator[](std::size_t idx) noexcept
        {
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO, idx < this->size());
            return this->underlying_[idx];
        }
        const Value& operator[](std::size_t idx) const noexcept
        {
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO, idx < this->size());
            return this->underlying_[idx];
        }

        iterator begin() { return underlying_.begin(); }
        iterator end() { return underlying_.end(); }
        const_iterator begin() const { return cbegin(); }
        const_iterator end() const { return cend(); }
        const_iterator cbegin() const { return underlying_.cbegin(); }
        const_iterator cend() const { return underlying_.cend(); }

        friend bool operator==(const Array& lhs, const Array& rhs);
        friend bool operator!=(const Array& lhs, const Array& rhs) { return !(lhs == rhs); }

    private:
        underlying_t underlying_;
    };

    struct Object
    {
    private:
        using value_type = std::pair<std::string, Value>;
        using underlying_t = std::vector<value_type>;

        underlying_t::const_iterator internal_find_key(StringView key) const noexcept;

    public:
        // these are here for better diagnostics
        Object() = default;
        Object(Object const&) = default;
        Object(Object&&) = default;
        Object& operator=(Object const&) = default;
        Object& operator=(Object&&) = default;
        ~Object() = default;

        // asserts if the key is found
        Value& insert(std::string key, Value&& value);
        Value& insert(std::string key, const Value& value);
        Object& insert(std::string key, Object&& value);
        Object& insert(std::string key, const Object& value);
        Array& insert(std::string key, Array&& value);
        Array& insert(std::string key, const Array& value);

        // replaces the value if the key is found, otherwise inserts a new
        // value.
        Value& insert_or_replace(std::string key, Value&& value);
        Value& insert_or_replace(std::string key, const Value& value);
        Object& insert_or_replace(std::string key, Object&& value);
        Object& insert_or_replace(std::string key, const Object& value);
        Array& insert_or_replace(std::string key, Array&& value);
        Array& insert_or_replace(std::string key, const Array& value);

        // returns whether the key existed
        bool remove(StringView key) noexcept;

        // asserts on lookup failure
        Value& operator[](StringView key) noexcept
        {
            auto res = this->get(key);
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO, res, "missing key: \"%s\"", key);
            return *res;
        }
        const Value& operator[](StringView key) const noexcept
        {
            auto res = this->get(key);
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO, res, "missing key: \"%s\"", key);
            return *res;
        }

        Value* get(StringView key) noexcept;
        const Value* get(StringView key) const noexcept;

        bool contains(StringView key) const noexcept { return this->get(key); }

        bool is_empty() const noexcept { return size() == 0; }
        std::size_t size() const noexcept { return this->underlying_.size(); }

        // sorts keys alphabetically
        void sort_keys();

        struct const_iterator
        {
            using value_type = std::pair<StringView, const Value&>;
            using reference = value_type;
            using iterator_category = std::forward_iterator_tag;

            value_type operator*() const noexcept { return *underlying_; }
            const_iterator& operator++() noexcept
            {
                ++underlying_;
                return *this;
            }
            const_iterator operator++(int) noexcept
            {
                auto res = *this;
                ++underlying_;
                return res;
            }

            bool operator==(const_iterator other) const noexcept { return this->underlying_ == other.underlying_; }
            bool operator!=(const_iterator other) const noexcept { return !(this->underlying_ == other.underlying_); }

        private:
            friend struct Object;
            explicit const_iterator(const underlying_t::const_iterator& it) : underlying_(it) { }
            underlying_t::const_iterator underlying_;
        };
        using iterator = const_iterator;

        const_iterator begin() const noexcept { return this->cbegin(); }
        const_iterator end() const noexcept { return this->cend(); }
        const_iterator cbegin() const noexcept { return const_iterator{this->underlying_.begin()}; }
        const_iterator cend() const noexcept { return const_iterator{this->underlying_.end()}; }

        friend bool operator==(const Object& lhs, const Object& rhs);
        friend bool operator!=(const Object& lhs, const Object& rhs) { return !(lhs == rhs); }

    private:
        underlying_t underlying_;
    };

    ExpectedT<std::pair<Value, JsonStyle>, std::unique_ptr<Parse::IParseError>> parse_file(
        const Filesystem&, const path&, std::error_code& ec) noexcept;
    ExpectedT<std::pair<Value, JsonStyle>, std::unique_ptr<Parse::IParseError>> parse(StringView text,
                                                                                      const path& filepath) noexcept;
    ExpectedT<std::pair<Value, JsonStyle>, std::unique_ptr<Parse::IParseError>> parse(StringView text,
                                                                                      StringView origin = {}) noexcept;
    std::pair<Value, JsonStyle> parse_file(vcpkg::LineInfo li, const Filesystem&, const path&) noexcept;

    std::string stringify(const Value&, JsonStyle style);
    std::string stringify(const Object&, JsonStyle style);
    std::string stringify(const Array&, JsonStyle style);
}
