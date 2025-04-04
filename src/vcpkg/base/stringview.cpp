#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>

#include <string.h>

#include <algorithm>
#include <utility>

namespace vcpkg
{
    StringView::StringView(const std::string& s) noexcept : m_ptr(s.data()), m_size(s.size()) { }

    bool StringView::ends_with(StringView pattern) const noexcept
    {
        if (m_size < pattern.size()) return false;
        auto offset = m_size - pattern.size();
        return std::equal(m_ptr + offset, m_ptr + m_size, pattern.begin(), pattern.end());
    }

    bool StringView::starts_with(StringView pattern) const noexcept
    {
        if (m_size < pattern.size()) return false;
        return std::equal(m_ptr, m_ptr + pattern.size(), pattern.begin(), pattern.end());
    }

    bool StringView::contains(StringView needle) const noexcept { return Strings::search(*this, needle) != end(); }

    bool StringView::contains(char needle) const noexcept
    {
        auto end_ = end();
        return std::find(m_ptr, end_, needle) != end_;
    }

    void StringView::remove_bom() noexcept
    {
        if (starts_with(UTF8_BOM))
        {
            m_ptr += UTF8_BOM.size();
            m_size -= UTF8_BOM.size();
        }
    }

    std::string StringView::to_string() const { return std::string(m_ptr, m_size); }
    void StringView::to_string(std::string& s) const { s.append(m_ptr, m_size); }

    bool operator==(StringView lhs, StringView rhs) noexcept
    {
        if (lhs.empty() && rhs.empty())
        {
            return true;
        }
        return lhs.size() == rhs.size() && memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
    }

    bool operator!=(StringView lhs, StringView rhs) noexcept { return !(lhs == rhs); }

    bool operator<(StringView lhs, StringView rhs) noexcept
    {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }

    bool operator>(StringView lhs, StringView rhs) noexcept { return rhs < lhs; }
    bool operator<=(StringView lhs, StringView rhs) noexcept { return !(rhs < lhs); }
    bool operator>=(StringView lhs, StringView rhs) noexcept { return !(lhs < rhs); }

    std::string operator+(std::string&& l, const StringView& r)
    {
        l.append(r.m_ptr, r.m_size);
        return std::move(l);
    }
}
