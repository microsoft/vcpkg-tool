#pragma once

#include <vcpkg/base/fwd/cmd-parser.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>

#include <cstddef>
#include <string>
#include <vector>

namespace vcpkg
{
    struct HelpTableFormatter
    {
        // Adds a table entry with a key `col1` and value `col2`
        void format(StringView col1, StringView col2);
        // Adds an example block; typically just the text with no indenting
        void example(StringView example_text);
        // Adds a header typically placed at the top of several table entries
        void header(StringView name);
        // Adds a blank line
        void blank();
        // Adds a line of text
        void text(StringView text, int indent = 0);

        std::string m_str;
    };

    std::vector<std::string> convert_argc_argv_to_arguments(int argc, const CommandLineCharType* const* const argv);
    ExpectedL<Unit> replace_response_file_parameters(std::vector<std::string>& inputs,
                                                     const ILineReader& response_file_source);

    struct CmdParser
    {
        CmdParser();
        explicit CmdParser(View<std::string> inputs);
        explicit CmdParser(std::vector<std::string>&& inputs);
        CmdParser(const CmdParser&);
        CmdParser(CmdParser&&);
        CmdParser& operator=(const CmdParser&);
        CmdParser& operator=(CmdParser&&);

        // Parses a switch from the input named switch_name, and stores the value if
        // encountered into value. Returns true if the switch was encountered.
        // Emits an error if the switch is encountered more than once.
        bool parse_switch(StringView switch_name, StabilityTag stability, bool& value);
        bool parse_switch(StringView switch_name, StabilityTag stability, Optional<bool>& value);

        // Parses a switch from the input named switch_name, assumed to default to off, returning
        // true if the switch was encountered and is true.
        bool parse_switch(StringView switch_name, StabilityTag stability);

        // And also adds a help entry:
        bool parse_switch(StringView switch_name,
                          StabilityTag stability,
                          bool& value,
                          const LocalizedString& help_text);
        bool parse_switch(StringView switch_name,
                          StabilityTag stability,
                          Optional<bool>& value,
                          const LocalizedString& help_text);
        bool parse_switch(StringView switch_name, StabilityTag stability, const LocalizedString& help_text);

        // Parses an option from the input named option_name, and stores the value if
        // encountered into value. Returns true if the option was encountered.
        // Emits an error if the switch is encountered more than once.
        bool parse_option(StringView option_name, StabilityTag stability, std::string& value);
        bool parse_option(StringView option_name, StabilityTag stability, Optional<std::string>& value);

        // And also adds a help entry:
        bool parse_option(StringView option_name,
                          StabilityTag stability,
                          std::string& value,
                          const LocalizedString& help_text);
        bool parse_option(StringView option_name,
                          StabilityTag stability,
                          Optional<std::string>& value,
                          const LocalizedString& help_text);

        // Parses an option from the input named option_name, and stores the value if encountered at least once into
        // value. Returns true if the option was encountered. Considers multiple uses of the option as additional
        // values.
        bool parse_multi_option(StringView option_name, StabilityTag stability, std::vector<std::string>& value);
        bool parse_multi_option(StringView option_name,
                                StabilityTag stability,
                                Optional<std::vector<std::string>>& value);

        // And also adds a help entry:
        bool parse_multi_option(StringView option_name,
                                StabilityTag stability,
                                std::vector<std::string>& value,
                                const LocalizedString& help_text);
        bool parse_multi_option(StringView option_name,
                                StabilityTag stability,
                                Optional<std::vector<std::string>>& value,
                                const LocalizedString& help_text);

        // Reads and consumes the first argument that:
        //   is "--version" (returning "version")
        //   does not start with "--" (returning the argument)
        // if present, converted to ASCII lowercase.
        Optional<std::string> extract_first_command_like_arg_lowercase();

        std::vector<std::string> get_remaining_args() const;

        // Emits an error if there are any remaining arguments.
        void enforce_no_remaining_args(StringView command_name);

        //
        // All remaining_arg functions assert that the remaining args don't start with --. If they do,
        // emits an error and returns the erroneous result (typically empty).
        //

        // Consumes the one remaining argument. Emits an error and returns empty string if the number of arguments left
        // is not 1.
        std::string consume_only_remaining_arg(StringView command_name);
        // Consumes the zero or one remaining argument. Emits an error and returns nullopt of the number of arguments
        // left is 2 or more.
        Optional<std::string> consume_only_remaining_arg_optional(StringView command_name);
        // Consumes the reaming arguments, no errors. See also: "get_remaining_args" for a nondestructive version.
        std::vector<std::string> consume_remaining_args();
        // Consumes the remaining arguments. Emits an error and returns an empty vector if the number of arguments left
        // is not exactly equal to arity.
        std::vector<std::string> consume_remaining_args(StringView command_name, std::size_t arity);
        // Consumes the remaining arguments. Emits an error and returns an empty vector if the number of arguments left
        // is not within the indicated range.
        std::vector<std::string> consume_remaining_args(StringView command_name,
                                                        std::size_t min_arity,
                                                        std::size_t max_arity);

        const std::vector<LocalizedString>& get_errors() const { return errors; }

        LocalizedString get_options_table() const;

        LocalizedString get_full_help_text(const LocalizedString& help_text) const;

        // If there are any errors, prints the command, help text, and help table, then terminates the program.
        void exit_with_errors(const LocalizedString& help_text);

    private:
        // Adds all arguments that aren't parsed after `idx` as errors.
        void add_unexpected_argument_errors_after(size_t idx);
        void add_unexpected_option_error(const std::string& unrecognized_option);
        bool consume_remaining_args_impl(std::vector<std::string>& result);

        std::vector<std::string> argument_strings;
        std::vector<std::string> argument_strings_lowercase;
        std::vector<char> argument_parsed; // think vector<bool>
        std::vector<LocalizedString> errors;
        HelpTableFormatter options_table;
    };

    void delistify_conjoined_multivalue(std::vector<std::string>& target);
}