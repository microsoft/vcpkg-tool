#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.hash.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands::Hash
{
    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("hash boost_1_62_0.tar.bz2"); },
        1,
        2,
        {},
        nullptr,
    };

    void HashCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        const auto parsed = args.parse_arguments(COMMAND_STRUCTURE);

        const auto file_to_hash = (fs.current_path(VCPKG_LINE_INFO) / parsed.command_arguments[0]).lexically_normal();

        auto algorithm = vcpkg::Hash::Algorithm::Sha512;
        if (parsed.command_arguments.size() == 2)
        {
            algorithm = vcpkg::Hash::algorithm_from_string(parsed.command_arguments[1]).value_or_exit(VCPKG_LINE_INFO);
        }

        const std::string hash = vcpkg::Hash::get_file_hash(fs, file_to_hash, algorithm).value_or_exit(VCPKG_LINE_INFO);
        msg::write_unlocalized_text_to_stdout(Color::none, hash + '\n');
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
