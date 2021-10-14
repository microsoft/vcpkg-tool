#pragma once

#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/base/format.h>

#include <stddef.h>
#include <string.h>

#include <iterator>
#include <limits>
#include <string>

namespace vcpkg
{
    struct StringView
    {
        constexpr StringView() = default;
        StringView(const std::string& s) noexcept; // Implicit by design

        // NOTE: we do this instead of the delegating constructor since delegating ctors are a perf footgun
        template<size_t Sz>
        constexpr StringView(const char (&arr)[Sz]) noexcept : m_ptr(arr), m_size(Sz - 1)
        {
        }

        StringView(const char* ptr) noexcept : m_ptr(ptr), m_size(strlen(ptr)) { }
        constexpr StringView(const char* ptr, size_t size) noexcept : m_ptr(ptr), m_size(size) { }
        constexpr StringView(const char* b, const char* e) noexcept : m_ptr(b), m_size(static_cast<size_t>(e - b)) { }

        constexpr const char* begin() const noexcept { return m_ptr; }
        constexpr const char* end() const noexcept { return m_ptr + m_size; }

        std::reverse_iterator<const char*> rbegin() const noexcept { return std::make_reverse_iterator(end()); }
        std::reverse_iterator<const char*> rend() const noexcept { return std::make_reverse_iterator(begin()); }

        constexpr const char* data() const noexcept { return m_ptr; }
        constexpr size_t size() const noexcept { return m_size; }
        constexpr bool empty() const noexcept { return m_size == 0; }

        std::string to_string() const;
        void to_string(std::string& out) const;

        StringView substr(size_t pos, size_t count = std::numeric_limits<size_t>::max()) const noexcept;

        constexpr char byte_at_index(size_t pos) const noexcept { return m_ptr[pos]; }

    private:
        const char* m_ptr = 0;
        size_t m_size = 0;
    };

    bool operator==(StringView lhs, StringView rhs) noexcept;
    bool operator!=(StringView lhs, StringView rhs) noexcept;
    bool operator<(StringView lhs, StringView rhs) noexcept;
    bool operator>(StringView lhs, StringView rhs) noexcept;
    bool operator<=(StringView lhs, StringView rhs) noexcept;
    bool operator>=(StringView lhs, StringView rhs) noexcept;
}

template<>
struct fmt::formatter<vcpkg::StringView> : fmt::formatter<fmt::string_view>
{
    // parse is inherited from formatter<string_view>.
    template<class FormatContext>
    auto format(vcpkg::StringView sv, FormatContext& ctx)
    {
        return fmt::formatter<fmt::string_view>::format(fmt::string_view(sv.data(), sv.size()), ctx);
    }
};
