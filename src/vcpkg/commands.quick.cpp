#include <vcpkg/base/system.h>
#include <vcpkg/commands.quick.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <unordered_map>
#include <fstream>
#include <sstream>

namespace vcpkg::Commands::Quick 
{
    struct QuickCommand {
        std::string name;
        std::string command;
        std::string description;
    };

    class QuickCommandManager 
    {
    private:
        std::unordered_map<std::string, QuickCommand> commands;
        Path config_path;
        
    public:
        QuickCommandManager(const VcpkgPaths& paths) {
            config_path = paths.root / "config" / "quick_commands.json";
            load_commands();
        }

        void add_command(const std::string& name, const std::string& command, const std::string& description) {
            commands[name] = QuickCommand{name, command, description};
            save_commands();
        }

        void remove_command(const std::string& name) {
            commands.erase(name);
            save_commands();
        }

        bool execute_command(const std::string& name) {
            auto it = commands.find(name);
            if (it != commands.end()) {
                std::vector<std::string> command_parts = split_command(it->second.command);
                return System::cmd_execute(command_parts) == 0;
            }
            return false;
        }

        void list_commands() {
            if (commands.empty()) {
                System::print2("No quick commands defined.\n");
                return;
            }

            System::print2("Available Quick Commands:\n\n");
            
            Table output;
            output.format()
                .line_format("| {0,-15} | {1,-40} | {2,-30} |")
                .header_format("| {0,-15} | {1,-40} | {2,-30} |")
                .format_cells()
                .column("Name")
                .column("Command")
                .column("Description");

            for (const auto& cmd : commands) {
                output.format()
                    .line_format("| {0,-15} | {1,-40} | {2,-30} |")
                    .add_row({
                        cmd.second.name,
                        cmd.second.command,
                        cmd.second.description
                    });
            }

            System::print2(output.to_string());
        }

    private:
        void load_commands() {
            try {
                auto raw_config = Json::parse_file(config_path, VCPKG_LINE_INFO);
                auto obj = raw_config.value.object(VCPKG_LINE_INFO);
                
                for (const auto& cmd : obj) {
                    auto command_obj = cmd.second.object(VCPKG_LINE_INFO);
                    commands[cmd.first] = QuickCommand{
                        cmd.first,
                        command_obj.get_string("command", VCPKG_LINE_INFO),
                        command_obj.get_string("description", VCPKG_LINE_INFO)
                    };
                }
            } catch (const std::exception&) {
                commands.clear();
            }
        }

        void save_commands() {
            Json::Object obj;
            for (const auto& cmd : commands) {
                Json::Object command_obj;
                command_obj.insert("command", Json::Value::string(cmd.second.command));
                command_obj.insert("description", Json::Value::string(cmd.second.description));
                obj.insert(cmd.second.name, std::move(command_obj));
            }

            auto config_dir = config_path.parent_path();
            if (!fs.exists(config_dir)) {
                fs.create_directories(config_dir, VCPKG_LINE_INFO);
            }

            fs.write_contents(config_path, Json::stringify(obj), VCPKG_LINE_INFO);
        }

        std::vector<std::string> split_command(const std::string& command) {
            std::vector<std::string> parts;
            std::stringstream ss(command);
            std::string part;
            
            while (ss >> std::quoted(part)) {
                parts.push_back(part);
            }
            
            return parts;
        }
    };

    static constexpr StringLiteral OPTION_ADD = "add";
    static constexpr StringLiteral OPTION_REMOVE = "remove";
    static constexpr StringLiteral OPTION_LIST = "list";
    
    static const CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"(quick
    quick --add <name> <command> <description>
    quick --remove <name>
    quick --list
    quick <name>)"),
        0,
        1,
        {
            OPTION_ADD,
            OPTION_REMOVE,
            OPTION_LIST
        },
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        
        QuickCommandManager manager(paths);

        if (options.exists(OPTION_ADD)) {
            if (options.command_arguments.size() < 3) {
                System::print2(Color::error, "Error: --add requires <name> <command> <description>\n");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
            manager.add_command(
                options.command_arguments[0],
                options.command_arguments[1],
                options.command_arguments[2]
            );
            System::print2(Color::success, "Quick command added successfully.\n");
        }
        else if (options.exists(OPTION_REMOVE)) {
            if (options.command_arguments.empty()) {
                System::print2(Color::error, "Error: --remove requires <name>\n");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
            manager.remove_command(options.command_arguments[0]);
            System::print2(Color::success, "Quick command removed successfully.\n");
        }
        else if (options.exists(OPTION_LIST)) {
            manager.list_commands();
        }
        else if (!options.command_arguments.empty()) {
            if (!manager.execute_command(options.command_arguments[0])) {
                System::print2(Color::error, "Error: Quick command not found or failed to execute.\n");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
        else {
            System::print2(COMMAND_STRUCTURE.example_text);
        }
    }
} 