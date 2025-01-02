#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>

#include <vcpkg/commands.format-feature-baselinet.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    bool is_comment(const std::string& line)
    {
        auto index = line.find_first_not_of(" \t");
        if (index != std::string::npos)
        {
            return line[index] == '#';
        }
        return true;
    }

    // The file should only contain ascii characters. Hardcode the unicode collation order for the ascii range.
    constexpr uint8_t comparison_indices[] = {
        0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,
        22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  38,  42,  54,  64,  55,  53,  41,  43,  44,  50,  58,
        35,  34,  40,  51,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  37,  36,  59,  60,  61,  39,  49,  75,
        77,  79,  82,  84,  86,  88,  90,  92,  94,  96,  98,  100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120,
        122, 124, 126, 45,  52,  46,  57,  33,  56,  76,  78,  80,  81,  83,  85,  87,  89,  91,  93,  95,  97,  99,
        101, 103, 105, 107, 109, 111, 113, 115, 117, 119, 121, 123, 125, 47,  62,  48,  63};

    struct cmp_char
    {
        auto operator()(char l_, char r_)
        {
            auto l = static_cast<uint8_t>(l_);
            auto r = static_cast<uint8_t>(r_);
            if (l >= sizeof(comparison_indices) || r >= sizeof(comparison_indices))
            {
                return l < r;
            }
            return comparison_indices[l] < comparison_indices[r];
        }
    };

    struct cmp_str
    {
        auto operator()(const std::string& left, const std::string& right)
        {
            return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(), cmp_char{});
        }
    };
}

namespace vcpkg
{
    constexpr CommandMetadata CommandFormatFeatureBaselineMetadata = {
        "format-feature-baseline",
        msgCmdFormatFeatureBaselineSynopsis,
        {msgCmdFormatFeatureBaselineExample},
        Undocumented,
        AutocompletePriority::Public,
        1,
        1,
        {{}, {}, {}},
        nullptr,
    };

    void command_format_feature_baseline_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        auto parsed_args = args.parse_arguments(CommandFormatFeatureBaselineMetadata);

        auto lines = fs.read_lines(parsed_args.command_arguments.at(0)).value_or_exit(VCPKG_LINE_INFO);
        for (auto start = lines.begin(); start != lines.end();)
        {
            if (is_comment(*start))
            {
                ++start;
                continue;
            }
            auto end = start + 1;
            while (end != lines.end() && !is_comment(*end))
            {
                ++end;
            }
            std::sort(start, end, cmp_str{});
            start = end;
        }
        fs.write_lines(parsed_args.command_arguments.at(0), lines, VCPKG_LINE_INFO);

        msg::println(msgFeatureBaselineFormatted);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
