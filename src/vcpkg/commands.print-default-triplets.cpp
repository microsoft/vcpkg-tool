#include <vcpkg/base/json.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.print-default-triplets.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands
{
    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"(x-print-default-triplets)"),
        0,
        0,
        {},
        nullptr,
    };

    void PrintDefaultTriplets::perform_and_exit(const VcpkgCmdArguments& args,
                                                const VcpkgPaths&,
                                                Triplet default_triplet,
                                                Triplet host_triplet)
    {
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        if (args.json.value_or(false))
        {
            Json::Object obj;
            obj.insert("target", Json::Value::string(default_triplet.to_string()));
            obj.insert("host", Json::Value::string(host_triplet.to_string()));
            System::print2(Json::stringify(obj, {}));
        }
        else
        {
            System::print2("target: \"", default_triplet.to_string(), "\"\n");
            System::print2("host: \"", host_triplet.to_string(), "\"\n");
        }
    }

    void PrintDefaultTriplets::PrintDefaultTripletsCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                                                             const VcpkgPaths& paths,
                                                                             Triplet default_triplet,
                                                                             Triplet host_triplet) const
    {
        return PrintDefaultTriplets::perform_and_exit(args, paths, default_triplet, host_triplet);
    }
}
