#pragma once

#include <vcpkg/base/fwd/format.h>

#include <vcpkg/base/stringview.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>

namespace vcpkg
{
    // A counted view of a null-terminated string
    struct ZStringView
    {
        constexpr ZStringView() = default;
        ZStringView(const std::string& s) noexcept;
        constexpr ZStringView(const char* ptr, size_t size) noexcept : m_ptr(ptr), m_size(size) { }
        // intentionally not provided to discourage non-null-termination:
        // constexpr ZStringView(const char* b, const char* e)

        template<size_t Sz>
        constexpr ZStringView(const char (&arr)[Sz]) noexcept : m_ptr(arr), m_size(Sz - 1)
        {
        }

        // This constructor is a "template" for const char* to avoid outcompeting the array constructor above.
        template<class Ptr,
                 std::enable_if_t<std::is_convertible<Ptr, const char*>::value && !std::is_array<Ptr>::value, int> = 0>
        ZStringView(Ptr ptr) noexcept : m_ptr(ptr), m_size(strlen(m_ptr))
        {
        }

        constexpr const char* begin() const noexcept { return m_ptr; }
        constexpr const char* end() const noexcept { return m_ptr + m_size; }

        const char& front() const noexcept { return *m_ptr; }
        const char& back() const noexcept { return m_ptr[m_size - 1]; }

        std::reverse_iterator<const char*> rbegin() const noexcept { return std::make_reverse_iterator(end()); }
        std::reverse_iterator<const char*> rend() const noexcept { return std::make_reverse_iterator(begin()); }

        constexpr const char* data() const noexcept { return m_ptr; }
        constexpr size_t size() const noexcept { return m_size; }
        constexpr bool empty() const noexcept { return m_size == 0; }
        constexpr char operator[](ptrdiff_t off) const noexcept { return m_ptr[off]; }

        constexpr const char* c_str() const noexcept { return m_ptr; }

        std::string to_string() const;
        void to_string(std::string& out) const;
        explicit operator std::string() const { return to_string(); }

        constexpr operator StringView() const noexcept { return StringView(m_ptr, m_size); }

        // Note that only the 1 parameter version of substr is provided to preserve null termination
        ZStringView substr(size_t pos) const noexcept;

        friend bool operator==(ZStringView lhs, ZStringView rhs) noexcept;
        friend bool operator!=(ZStringView lhs, ZStringView rhs) noexcept;
        friend bool operator<(ZStringView lhs, ZStringView rhs) noexcept;
        friend bool operator>(ZStringView lhs, ZStringView rhs) noexcept;
        friend bool operator<=(ZStringView lhs, ZStringView rhs) noexcept;
        friend bool operator>=(ZStringView lhs, ZStringView rhs) noexcept;

        friend std::string operator+(std::string&& l, const ZStringView& r);

    private:
        const char* m_ptr = "";
        size_t m_size = 0;
    };
}

VCPKG_FORMAT_AS(vcpkg::ZStringView, vcpkg::StringView);
