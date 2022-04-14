#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg
{
    Optional<StringView> find_first_nonzero_mac(StringView sv);
    std::string get_user_mac();
}