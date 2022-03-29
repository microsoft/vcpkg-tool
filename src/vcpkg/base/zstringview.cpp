#include <vcpkg/base/zstringview.h>

#include <string.h>

#include <algorithm>

namespace vcpkg
{
    ZStringView::ZStringView(const std::string& s) noexcept : m_ptr(s.data()), m_size(s.size()) { }

    std::string ZStringView::to_string() const { return std::string(m_ptr, m_size); }
    void ZStringView::to_string(std::string& s) const { s.append(m_ptr, m_size); }

    ZStringView ZStringView::substr(size_t pos) const noexcept
    {
        if (pos < m_size)
        {
            return ZStringView{m_ptr + pos, m_size - pos};
        }

        return ZStringView{};
    }

    bool operator==(ZStringView lhs, ZStringView rhs) noexcept
    {
        return lhs.size() == rhs.size() && memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
    }

    bool operator!=(ZStringView lhs, ZStringView rhs) noexcept { return !(lhs == rhs); }

    bool operator<(ZStringView lhs, ZStringView rhs) noexcept
    {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }

    bool operator>(ZStringView lhs, ZStringView rhs) noexcept { return rhs < lhs; }
    bool operator<=(ZStringView lhs, ZStringView rhs) noexcept { return !(rhs < lhs); }
    bool operator>=(ZStringView lhs, ZStringView rhs) noexcept { return !(lhs < rhs); }

    std::string operator+(std::string&& l, const ZStringView& r)
    {
        l.append(r.m_ptr, r.m_size);
        return std::move(l);
    }
}
