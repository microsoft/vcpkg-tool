#pragma once

#include <vcpkg/base/fwd/format.h>
#include <vcpkg/base/fwd/stringview.h>

#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <string>

namespace vcpkg
{
    struct StringView
    {
        constexpr StringView() = default;
        StringView(const std::string& s) noexcept; // Implicit by design
        StringView(const char* ptr) noexcept : m_ptr(ptr), m_size(strlen(ptr)) { }
        constexpr StringView(const char* ptr, size_t size) noexcept : m_ptr(ptr), m_size(size) { }
        constexpr StringView(const char* b, const char* e) noexcept : m_ptr(b), m_size(static_cast<size_t>(e - b)) { }

        constexpr const char* begin() const noexcept { return m_ptr; }
        constexpr const char* end() const noexcept { return m_ptr + m_size; }

        const char& front() const noexcept { return *m_ptr; }
        const char& back() const noexcept { return m_ptr[m_size - 1]; }

        std::reverse_iterator<const char*> rbegin() const noexcept { return std::make_reverse_iterator(end()); }
        std::reverse_iterator<const char*> rend() const noexcept { return std::make_reverse_iterator(begin()); }

        constexpr const char* data() const noexcept { return m_ptr; }
        constexpr size_t size() const noexcept { return m_size; }
        constexpr bool empty() const noexcept { return m_size == 0; }

        // intentionally not provided because this may not be null terminated
        // constexpr const char* c_str() const

        std::string to_string() const;
        void to_string(std::string& out) const;
        explicit operator std::string() const { return to_string(); }

        StringView substr(size_t pos, size_t count = std::numeric_limits<size_t>::max()) const noexcept;

        constexpr char operator[](size_t pos) const noexcept { return m_ptr[pos]; }
        friend std::string operator+(std::string&& l, const StringView& r);

    private:
        const char* m_ptr = 0;
        size_t m_size = 0;
    };

    // Intentionally not hidden friends to allow comparison of Path, for example
    bool operator==(StringView lhs, StringView rhs) noexcept;
    bool operator!=(StringView lhs, StringView rhs) noexcept;
    bool operator<(StringView lhs, StringView rhs) noexcept;
    bool operator>(StringView lhs, StringView rhs) noexcept;
    bool operator<=(StringView lhs, StringView rhs) noexcept;
    bool operator>=(StringView lhs, StringView rhs) noexcept;

    // A counted view of a null-terminated string
    struct ZStringView : StringView
    {
        constexpr ZStringView() : StringView("", size_t{}) { }
        ZStringView(const std::string& s) : StringView(s) { }
        constexpr ZStringView(const char* ptr, size_t size) noexcept : StringView(ptr, size) { }
        // intentionally not provided to discourage non-null-termination:
        // constexpr ZStringView(const char* b, const char* e)
        ZStringView(const char* ptr) noexcept : StringView(ptr) { }

        constexpr const char* c_str() const noexcept { return data(); }

        // Note that only the 1 parameter version of substr is provided to preserve null termination
        // (The name from the base class is intentionally hidden)
        ZStringView substr(size_t pos) const noexcept;
    };

    struct StringLiteral : ZStringView
    {
        template<int N>
        constexpr StringLiteral(const char (&str)[N]) : ZStringView(str, N - 1)
        {
        }
    };
}

VCPKG_FORMAT_AS(vcpkg::ZStringView, vcpkg::StringView);
VCPKG_FORMAT_AS(vcpkg::StringLiteral, vcpkg::StringView);

template<::size_t N, typename std::enable_if<(N > 0), bool>::type = true>
struct StringArray
{
    constexpr StringArray() noexcept : m_array{} { }
    constexpr StringArray(const char (&str)[N]) noexcept : m_array{}
    {
        for (size_t i = 0; i < N; i++)
        {
            m_array.at(i) = str[i];
        }
    }

    constexpr auto begin() noexcept { return m_array.begin(); }
    constexpr auto end() noexcept { return m_array.end() - 1; }

    constexpr auto begin() const noexcept { return m_array.begin(); }
    constexpr auto end() const noexcept { return m_array.end() - 1; }

    constexpr ::size_t size() const noexcept { return m_array.size() - 1; }
    constexpr bool empty() const noexcept { return size() == 0; }

    template<::size_t U>
    constexpr bool operator==(const StringArray<U>& other) const noexcept
    {
        return m_array == other.m_array;
    }
    template<::size_t U>
    constexpr bool operator!=(const StringArray<U>& other) const noexcept { return m_array != other.m_array; }

    constexpr char operator[](::size_t pos) const noexcept
    {
        // static_assert(pos < N - 1);
        return m_array[pos];
    }
    constexpr char& operator[](::size_t pos) noexcept
    {
        // static_assert(pos < size(), "array subscript out of range");
        return m_array[pos];
    }

    template<::size_t L, ::size_t R>
    friend constexpr StringArray<L + R - 1> operator+(const StringArray<L> lhs, const StringArray<R> rhs) noexcept;

private:
    template<class InIt, class OutIt>
    constexpr OutIt constexpr_copy(InIt first, InIt last, OutIt dest)
    {
        while (first != last)
        {
            *dest = *first;
            ++dest;
            ++first;
        }

        return dest;
    }

private:
    std::array<char, N> m_array;
};

template<::size_t L, ::size_t R>
constexpr StringArray<L + R - 1> operator+(const StringArray<L> lhs, const StringArray<R> rhs) noexcept
{
    if constexpr (lhs.empty())
        return rhs;
    else if constexpr (rhs.empty())
        return lhs;
    else
    {
        StringArray<L + R - 1> out;

        for (size_t i = 0; i < lhs.size(); i++)
        {
            out[i] = lhs[i];
        }

        for (size_t i = L - 1; i < lhs.size() + rhs.size(); i++)
        {
            out[i] = rhs[i - lhs.size()];
        }
        return out;
    }
}
