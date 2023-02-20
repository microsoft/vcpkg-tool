#include <vcpkg/base/stringview.h>

#include <string.h>

#include <algorithm>
#include <utility>

namespace vcpkg
{
    StringView::StringView(const std::string& s) noexcept : m_ptr(s.data()), m_size(s.size()) { }

    std::string StringView::to_string() const { return std::string(m_ptr, m_size); }
    void StringView::to_string(std::string& s) const { s.append(m_ptr, m_size); }

    StringView StringView::substr(size_t pos, size_t count) const noexcept
    {
        if (pos > m_size)
        {
            return StringView();
        }

        if (count > m_size - pos)
        {
            return StringView(m_ptr + pos, m_size - pos);
        }

        return StringView(m_ptr + pos, count);
    }

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

    ZStringView ZStringView::substr(size_t pos) const noexcept
    {
        if (pos < size())
        {
            return ZStringView{data() + pos, size() - pos};
        }

        return ZStringView{};
    }
}
