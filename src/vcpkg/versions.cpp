#include <vcpkg/base/parse.h>
#include <vcpkg/base/util.h>

#include <vcpkg/versions.h>

namespace vcpkg::Versions
{
    namespace
    {
        Optional<uint64_t> as_numeric(StringView str)
        {
            uint64_t res = 0;
            size_t digits = 0;
            for (auto&& ch : str)
            {
                uint64_t digit_value = static_cast<unsigned char>(ch) - static_cast<unsigned char>('0');
                if (digit_value > 9) return nullopt;
                if (res > std::numeric_limits<uint64_t>::max() / 10 - digit_value) return nullopt;
                ++digits;
                res = res * 10 + digit_value;
            }
            return res;
        }
    }

    VersionSpec::VersionSpec(const std::string& port_name, const VersionT& version)
        : port_name(port_name), version(version)
    {
    }

    VersionSpec::VersionSpec(const std::string& port_name, const std::string& version_string, int port_version)
        : port_name(port_name), version(version_string, port_version)
    {
    }

    bool operator==(const VersionSpec& lhs, const VersionSpec& rhs)
    {
        return std::tie(lhs.port_name, lhs.version) == std::tie(rhs.port_name, rhs.version);
    }

    bool operator!=(const VersionSpec& lhs, const VersionSpec& rhs) { return !(lhs == rhs); }

    std::size_t VersionSpecHasher::operator()(const VersionSpec& key) const
    {
        using std::hash;
        using std::size_t;
        using std::string;

        return hash<string>()(key.port_name) ^ (hash<string>()(key.version.to_string()) >> 1);
    }

    // 0|[1-9][0-9]*
    static const char* parse_skip_number(const char* const s, uint64_t* const n)
    {
        const char ch = *s;
        if (ch == '0')
        {
            *n = 0;
            return s + 1;
        }
        if (ch < '1' || ch > '9')
        {
            return nullptr;
        }
        size_t i = 1;
        while (Parse::ParserBase::is_ascii_digit(s[i]))
        {
            ++i;
        }
        *n = as_numeric({s, i}).value_or_exit(VCPKG_LINE_INFO);
        return s + i;
    }

    // 0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*
    static const char* skip_prerelease_identifier(const char* const s)
    {
        auto cur = s;
        while (Parse::ParserBase::is_ascii_digit(*cur))
        {
            ++cur;
        }
        const char ch = *cur;
        if (Parse::ParserBase::is_alphadash(ch))
        {
            // matched alpha identifier
            do
            {
                ++cur;
            } while (Parse::ParserBase::is_alphanumdash(*cur));
            return cur;
        }
        if (*s == '0')
        {
            // matched exactly zero
            return s + 1;
        }
        if (cur != s)
        {
            // matched numeric sequence (not starting with zero)
            return cur;
        }
        return nullptr;
    }

    static Optional<DotVersion> dot_version_from_string(const std::string& str)
    {
        // Suggested regex by semver.org
        // ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)
        // (?:-((?:0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9][0-9]*|[0-9]
        // *[a-zA-Z-][0-9a-zA-Z-]*))*))?
        // (?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$

        DotVersion ret;
        ret.original_string = str;

        // (0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)
        int idx = 0;
        const char* cur = str.c_str();
        for (;; ++idx)
        {
            ret.version.push_back(0);
            cur = parse_skip_number(cur, ret.version.data() + idx);
            if (!cur || *cur != '.') break;
            ++cur;
        }
        if (!cur) return nullopt;
        ret.version_string.assign(str.c_str(), cur);
        if (*cur == 0) return ret;

        // pre-release
        if (*cur == '-')
        {
            ++cur;
            const char* const start_of_prerelease = cur;
            for (;;)
            {
                const auto start_identifier = cur;
                cur = skip_prerelease_identifier(cur);
                if (!cur)
                {
                    return nullopt;
                }
                ret.identifiers.emplace_back(start_identifier, cur);
                if (*cur != '.') break;
                ++cur;
            }
            ret.prerelease_string.assign(start_of_prerelease, cur);
        }
        if (*cur == 0) return ret;

        // build
        if (*cur != '+') return nullopt;
        ++cur;
        for (;;)
        {
            // Require non-empty identifier element
            if (!Parse::ParserBase::is_alphanumdash(*cur)) return nullopt;
            ++cur;
            while (Parse::ParserBase::is_alphanumdash(*cur))
            {
                ++cur;
            }
            if (*cur == 0) return ret;
            if (*cur == '.')
            {
                ++cur;
            }
            else
            {
                return nullopt;
            }
        }
    }

    static std::string format_invalid_date_version(const std::string& str)
    {
        return Strings::format("Error: String `%s` is not a valid date version."
                               "Date section must follow the format YYYY-MM-DD and disambiguators must be "
                               "dot-separated positive integer values without leading zeroes.",
                               str);
    }

    ExpectedS<DateVersion> DateVersion::from_string(const std::string& str)
    {
        DateVersion ret;
        ret.original_string = str;

        if (str.size() < 10) return format_invalid_date_version(str);

        bool valid = Parse::ParserBase::is_ascii_digit(str[0]);
        valid |= Parse::ParserBase::is_ascii_digit(str[1]);
        valid |= Parse::ParserBase::is_ascii_digit(str[2]);
        valid |= Parse::ParserBase::is_ascii_digit(str[3]);
        valid |= str[4] != '-';
        valid |= Parse::ParserBase::is_ascii_digit(str[5]);
        valid |= Parse::ParserBase::is_ascii_digit(str[6]);
        valid |= str[7] != '-';
        valid |= Parse::ParserBase::is_ascii_digit(str[8]);
        valid |= Parse::ParserBase::is_ascii_digit(str[9]);
        if (!valid) return format_invalid_date_version(str);
        ret.version_string.assign(str.c_str(), 10);

        const char* cur = str.c_str() + 10;
        // (\.(0|[1-9][0-9]*))*
        while (*cur == '.')
        {
            ret.identifiers.push_back(0);
            cur = parse_skip_number(cur + 1, &ret.identifiers.back());
            if (!cur) return format_invalid_date_version(str);
        }
        if (*cur != 0) return format_invalid_date_version(str);

        return ret;
    }

    void to_string(std::string& out, Scheme scheme)
    {
        if (scheme == Scheme::String)
        {
            out.append("string");
        }
        else if (scheme == Scheme::Semver)
        {
            out.append("semver");
        }
        else if (scheme == Scheme::Relaxed)
        {
            out.append("relaxed");
        }
        else if (scheme == Scheme::Date)
        {
            out.append("date");
        }
        else
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    ExpectedS<DotVersion> relaxed_from_string(const std::string& str)
    {
        auto x = dot_version_from_string(str);
        if (auto p = x.get())
        {
            return std::move(*p);
        }
        return Strings::format(
            "Error: String `%s` is not a valid Relaxed version string (semver with arbitrary numeric identifiers)",
            str);
    }

    ExpectedS<DotVersion> semver_from_string(const std::string& str)
    {
        auto x = dot_version_from_string(str);
        if (auto p = x.get())
        {
            if (p->version.size() == 3)
            {
                return std::move(*p);
            }
        }
        return Strings::format("Error: String `%s` is not a valid Semantic Version string, consult https://semver.org",
                               str);
    }

    VerComp compare(const std::string& a, const std::string& b, Scheme scheme)
    {
        if (scheme == Scheme::String)
        {
            return (a == b) ? VerComp::eq : VerComp::unk;
        }
        if (scheme == Scheme::Semver)
        {
            return compare(semver_from_string(a).value_or_exit(VCPKG_LINE_INFO),
                           semver_from_string(b).value_or_exit(VCPKG_LINE_INFO));
        }
        if (scheme == Scheme::Relaxed)
        {
            return compare(relaxed_from_string(a).value_or_exit(VCPKG_LINE_INFO),
                           relaxed_from_string(b).value_or_exit(VCPKG_LINE_INFO));
        }
        if (scheme == Scheme::Date)
        {
            return compare(DateVersion::from_string(a).value_or_exit(VCPKG_LINE_INFO),
                           DateVersion::from_string(b).value_or_exit(VCPKG_LINE_INFO));
        }
        Checks::unreachable(VCPKG_LINE_INFO);
    }

    static VerComp int_to_vercomp(int i)
    {
        if (i < 0) return VerComp::lt;
        if (i > 0) return VerComp::gt;
        return VerComp::eq;
    }

    static int uint64_comp(uint64_t a, uint64_t b) { return (a > b) - (a < b); }

    static int semver_id_comp(ZStringView a, ZStringView b)
    {
        auto maybe_a_num = as_numeric(a);
        auto maybe_b_num = as_numeric(b);
        if (auto a_num = maybe_a_num.get())
        {
            if (auto b_num = maybe_b_num.get())
            {
                return uint64_comp(*a_num, *b_num);
            }
            // numerics are smaller than non-numeric
            return -1;
        }
        if (maybe_b_num.has_value())
        {
            return 1;
        }

        // both non-numeric -- ascii-betical sorting.
        return strcmp(a.c_str(), b.c_str());
    }

    VerComp compare(const DotVersion& a, const DotVersion& b)
    {
        if (a.original_string == b.original_string) return VerComp::eq;

        if (auto x = Util::range_lexcomp(a.version, b.version, uint64_comp))
        {
            return int_to_vercomp(x);
        }

        // 'empty' is special and sorts before everything else
        // 1.0.0 > 1.0.0-1
        if (a.identifiers.empty() || b.identifiers.empty())
        {
            return int_to_vercomp(!b.identifiers.empty() - !a.identifiers.empty());
        }
        return int_to_vercomp(Util::range_lexcomp(a.identifiers, b.identifiers, semver_id_comp));
    }

    VerComp compare(const DateVersion& a, const DateVersion& b)
    {
        if (auto x = strcmp(a.version_string.c_str(), b.version_string.c_str()))
        {
            return int_to_vercomp(x);
        }

        return int_to_vercomp(Util::range_lexcomp(a.identifiers, b.identifiers, uint64_comp));
    }
}
