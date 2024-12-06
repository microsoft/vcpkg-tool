#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>

#include <vcpkg/cgroup-parser.h>

namespace vcpkg
{
    ControlGroup::ControlGroup(long id, StringView s, StringView c)
        : hierarchy_id(id), subsystems(s.data(), s.size()), control_group(c.data(), c.size())
    {
    }

    // parses /proc/[pid]/cgroup file as specified in https://linux.die.net/man/5/proc
    // The file describes control groups to which the process/tasks belongs.
    // For each cgroup hierarchy there is one entry
    // containing colon-separated fields of the form:
    //    5:cpuacct,cpu,cpuset:/daemos
    //
    // The colon separated fields are, from left to right:
    //
    // 1. hierarchy ID number
    // 2. set of subsystems bound to the hierarchy
    // 3. control group in the hierarchy to which the process belongs
    std::vector<ControlGroup> parse_cgroup_file(StringView text, StringView origin)
    {
        using P = ParserBase;
        constexpr auto is_separator_or_lineend = [](auto ch) { return ch == ':' || P::is_lineend(ch); };

        ParserBase parser{text, origin};
        parser.skip_whitespace();

        std::vector<ControlGroup> ret;
        while (!parser.at_eof())
        {
            auto id = parser.match_until(is_separator_or_lineend);
            auto maybe_numeric_id = Strings::strto<long>(id);
            if (!maybe_numeric_id || P::is_lineend(parser.cur()))
            {
                ret.clear();
                break;
            }

            parser.next();
            auto subsystems = parser.match_until(is_separator_or_lineend);
            if (P::is_lineend(parser.cur()))
            {
                ret.clear();
                break;
            }

            parser.next();
            auto control_group = parser.match_until(P::is_lineend);
            parser.skip_whitespace();

            ret.emplace_back(*maybe_numeric_id.get(), subsystems, control_group);
        }

        return ret;
    }

    bool detect_docker_in_cgroup_file(StringView text, StringView origin)
    {
        return Util::any_of(parse_cgroup_file(text, origin), [](auto&& cgroup) {
            return Strings::starts_with(cgroup.control_group, "/docker") ||
                   Strings::starts_with(cgroup.control_group, "/lxc");
        });
    }
}
