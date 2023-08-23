#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/json.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/stringview.h>

#include <limits.h>
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

        static JsonStyle with_tabs() noexcept { return JsonStyle{SIZE_MAX}; }
        static JsonStyle with_spaces(size_t indent) noexcept
        {
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO, indent != SIZE_MAX);
            return JsonStyle{indent};
        }

        void set_tabs() noexcept { this->indent = SIZE_MAX; }
        void set_spaces(size_t indent_) noexcept
        {
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO, indent != SIZE_MAX);
            this->indent = indent_;
        }

        bool use_tabs() const noexcept { return indent == SIZE_MAX; }
        bool use_spaces() const noexcept { return indent != SIZE_MAX; }

        size_t spaces() const noexcept
        {
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO, indent != SIZE_MAX);
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
        constexpr explicit JsonStyle(size_t indent) : indent(indent) { }
        // SIZE_MAX for tab, otherwise # of spaces
        size_t indent = 2;
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
        bool boolean(LineInfo li) const noexcept;
        int64_t integer(LineInfo li) const noexcept;
        double number(LineInfo li) const noexcept;
        StringView string(LineInfo li) const noexcept;

        const Array& array(LineInfo li) const& noexcept;
        Array& array(LineInfo li) & noexcept;
        Array&& array(LineInfo li) && noexcept;

        const Object& object(LineInfo li) const& noexcept;
        Object& object(LineInfo li) & noexcept;
        Object&& object(LineInfo li) && noexcept;

        static Value null(std::nullptr_t) noexcept;
        static Value boolean(bool) noexcept;
        static Value integer(int64_t i) noexcept;
        static Value number(double d) noexcept;
        static Value string(std::string&& s) noexcept;
        template<class StringLike, std::enable_if_t<std::is_constructible_v<StringView, const StringLike&>, int> = 0>
        static Value string(const StringLike& s) noexcept
        {
            return string(StringView(s).to_string());
        }

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

        Value& push_back(std::string&& value);
        template<class StringLike, std::enable_if_t<std::is_constructible_v<StringView, const StringLike&>, int> = 0>
        Value& push_back(const StringLike& value)
        {
            return this->push_back(StringView(value).to_string());
        }
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
        template<class StringLike, std::enable_if_t<std::is_constructible_v<StringView, const StringLike&>, int> = 0>
        Value& insert(StringView key, const StringLike& value)
        {
            return this->insert(key, StringView(value).to_string());
        }
        Value& insert(StringView key, std::string&& value);
        Value& insert(StringView key, Value&& value);
        Value& insert(StringView key, const Value& value);
        Object& insert(StringView key, Object&& value);
        Object& insert(StringView key, const Object& value);
        Array& insert(StringView key, Array&& value);
        Array& insert(StringView key, const Array& value);

        // replaces the value if the key is found, otherwise inserts a new
        // value.
        Value& insert_or_replace(StringView key, std::string&& value);
        template<class StringLike, std::enable_if_t<std::is_constructible_v<StringView, const StringLike&>, int> = 0>
        Value& insert_or_replace(StringView key, const StringLike& value)
        {
            return this->insert_or_replace(key, StringView(value).to_string());
        }
        Value& insert_or_replace(StringView key, Value&& value);
        Value& insert_or_replace(StringView key, const Value& value);
        Object& insert_or_replace(StringView key, Object&& value);
        Object& insert_or_replace(StringView key, const Object& value);
        Array& insert_or_replace(StringView key, Array&& value);
        Array& insert_or_replace(StringView key, const Array& value);

        // returns whether the key existed
        bool remove(StringView key) noexcept;

        // asserts on lookup failure
        Value& operator[](StringView key) noexcept
        {
            auto res = this->get(key);
            if (res == nullptr)
            {
                Checks::unreachable(VCPKG_LINE_INFO, fmt::format("JSON object missing key {}", key));
            }

            return *res;
        }
        const Value& operator[](StringView key) const noexcept
        {
            auto res = this->get(key);
            if (res == nullptr)
            {
                Checks::unreachable(VCPKG_LINE_INFO, fmt::format("JSON object missing key {}", key));
            }

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
            friend Object;
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

    struct ParsedJson
    {
        Value value;
        JsonStyle style;
    };

    ExpectedT<ParsedJson, std::unique_ptr<ParseError>> parse_file(const ReadOnlyFilesystem&,
                                                                  const Path&,
                                                                  std::error_code& ec);
    ExpectedT<ParsedJson, std::unique_ptr<ParseError>> parse(StringView text, StringView origin = {});
    ParsedJson parse_file(LineInfo li, const ReadOnlyFilesystem&, const Path&);
    ExpectedL<Json::Object> parse_object(StringView text, StringView origin = {});

    std::string stringify(const Value&);
    std::string stringify(const Value&, JsonStyle style);
    std::string stringify(const Object&);
    std::string stringify(const Object&, JsonStyle style);
    std::string stringify(const Array&);
    std::string stringify(const Array&, JsonStyle style);

    uint64_t get_json_parsing_stats();
}
