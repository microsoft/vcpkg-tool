#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

namespace vcpkg::details
{
    void print(StringView message) { msg::write_unlocalized_text_to_stdout(Color::none, message); }

    void print(const Color c, StringView message) { msg::write_unlocalized_text_to_stdout(c, message); }
}
