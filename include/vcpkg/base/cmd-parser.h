#pragma once

#include <vcpkg/base/fwd/cmd-parser.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/stringview.h>

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
}