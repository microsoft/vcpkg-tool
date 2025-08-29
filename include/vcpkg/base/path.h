#pragma once

#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg
{
    struct Path
    {
        Path() = default;
        Path(const StringView sv);
        Path(const std::string& s);
        Path(std::string&& s);
        Path(const char* s);
        Path(const char* first, size_t size);

        const std::string& native() const& noexcept;
        std::string&& native() && noexcept;
        operator StringView() const noexcept;

        const char* c_str() const noexcept;

        std::string generic_u8string() const;

        bool empty() const noexcept;

        Path operator/(StringView sv) const&;
        Path operator/(StringView sv) &&;
        Path operator+(StringView sv) const&;
        Path operator+(StringView sv) &&;

        Path& operator/=(StringView sv);
        Path& operator+=(StringView sv);

        void replace_filename(StringView sv);
        void remove_filename();
        void make_preferred();
        void make_generic();
        void clear();
        Path lexically_normal() const;

        // Sets *this to parent_path, returns whether anything was removed
        bool make_parent_path();

        StringView parent_path() const;
        StringView filename() const;
        StringView extension() const;
        StringView stem() const;

        bool is_absolute() const;
        bool is_relative() const;

        friend const char* to_printf_arg(const Path& p) noexcept;

    private:
        std::string m_str;
    };

    // attempt to parse str as a path and return the filename if it exists; otherwise, an empty view
    StringView parse_filename(StringView str) noexcept;
}
