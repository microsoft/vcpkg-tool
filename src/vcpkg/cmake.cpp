
#include <vcpkg/base/fmt.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/cmake.h>

namespace vcpkg
{
    StringView find_cmake_invocation(StringView content, StringView command)
    {
        auto it = Util::search_and_skip(content.begin(), content.end(), command);
        if (it == content.end() || ParserBase::is_word_char(*it)) return {};
        ++it;
        auto it_end = std::find(it, content.end(), ')');
        if (it_end == content.end()) return {};
        return StringView{it, it_end};
    }
    StringView extract_cmake_invocation_argument(StringView command, StringView argument)
    {
        auto it = Util::search_and_skip(command.begin(), command.end(), argument);
        if (it == command.end() || ParserBase::is_alphanum(*it)) return {};
        it = std::find_if_not(it, command.end(), ParserBase::is_whitespace);
        if (it == command.end()) return {};
        if (*it == '"')
        {
            return {it + 1, std::find(it + 1, command.end(), '"')};
        }
        return {it, std::find_if(it + 1, command.end(), [](char ch) {
                    return ParserBase::is_whitespace(ch) || ch == ')';
                })};
    }
    std::string replace_cmake_var(StringView text, StringView var, StringView value)
    {
        return Strings::replace_all(text, fmt::format("${{{}}}", var), value);
    }
} // namespace vcpkg
