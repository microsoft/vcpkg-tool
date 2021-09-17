#include <vcpkg/messages.h>
#include <map>

namespace vcpkg
{
    struct MessageContext::MessageContextImpl
    {
        std::map<std::string, std::string, std::less<>> localized_strings;
    };

    Optional<fmt::string_view> MessageContext::get_dynamic_format_string(StringView name) const
    {
        auto itr = impl->localized_strings.find(name);
        if (itr == impl->localized_strings.end())
        {
            return nullopt;
        }
        else
        {
            return fmt::string_view(itr->second);
        }
    }
}
