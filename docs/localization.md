# Writing Localizeable Code

As part of Microsoft's commitment to global-ready products, our developer tools are localized into 14 different languages.
For this effort, vcpkg has hooks in order to allow our localization team to translate all messages that are shown to the user by the vcpkg tool.
As a developer on vcpkg, the only thing you have to consider is the messages in the C++ source files,
and `locales/messages.json` -- everything else will be generated and modified by the localization team.

## Declaring a Message

The process of writing a message to be displayed to the user starts with the message; if it needs to be in a header,
use the `DECLARE_MESSAGE(<message-name>, <parameters>, <comment>, <english-message>` in that header,
followed by `REGISTER_MESSAGE(<message-name>)` in a source file; if it can just be in a source file,
use `DECLARE_AND_REGISTER_MESSAGE()`.

### Message Names

Message names should be descriptive and unique, and must be `CamelCase`.
When referring to a message in `msg::format`, you must add `msg` to the front of it; for example,
`DECLARE_MESSAGE(MyMessage, ...)` can be referred to as `msgMyMessage`.

### Parameters

Each placeholder in a localized format string must have a corresponding parameter in this list.
It should look something like `(msg::arg1, msg::arg2)`.
This allows us to make sure that we don't have format errors, where one forgets a placeholder,
or adds an extra one.
Each parameter in the list should be used in the message.

### Comment

The comment is used to give context to the translator as to what a placeholder will be replaced with.
For example, in `BuildResultSummaryHeader` (`"SUMMARY FOR {triplet}"`),
it will note that an example of `{triplet}` is `'x64-windows'`.
Most message placeholders (see `messages.h` for the full list - search for `DECLARE_MSG_ARG`) have comments associated with them already.
Only general placeholders like `{value}`, `{expected}`, and `{actual}` don't have comments associated with them.
If you use any of these placeholders, write `example of {<name>} is '<expected-value>'` for each of those placeholders,
separated by `\n`. You can also add context if you feel it's necessary as the first line.

An additional, special value is `"{Locked}"` -- this is a note to the localizers that they should not modify the message.
However, you should not use this value, and we will remove existing uses of it over time.

### Message

Messages in vcpkg are written in American English. They should not contain:

* formatting:
  - indentation should be added with the `append_indent()` function
  - newlines should be added with the `append_newline()` function
  - Any other interesting characters (like `- ` for lists, for example) should use `append(LocalizedString::from_raw(...))`
* the kind of message:
  - warnings should be printed as `msg::format(msg::msgWarningMessage).append(msgMyWarning)`
  - errors the same, with `msg::msgErrorMessage`
  - and internal errors the same, with `msg::msgInternalErrorMessage`.

They should also not be simple, locale-invariant messages -- something like, for example,
`{file}:{line}:{column}: ` should be done with `LocalizedString::from_raw(fmt::format("{}:{}:{}", file, line, column))`.

Other than that, all messages that are printed to the user must be localizeable.
However, that's as far as you need to go -- once you've done the writing of the message, and the `x-generate-default-message-map` stuff,
you can rest easy not modifying anything else.

### Putting it All Together

Let's create a vcpkg hello world command to show off how one can use the messages API.

```cxx
// commands.hello-world.cpp
#include <vcpkg/base/messages.h>

#include <vcpkg/commands.hello-world.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    // boilerplate
    const CommandStructure HELLO_WORLD_COMMAND_STRUCTURE = {
        create_example_string("hello-world <name>"),
        0,
        1,
        {{}, {}, {}},
        nullptr,
    };

    // note that we add additional context in the comment here
    DECLARE_AND_REGISTER_MESSAGE(World, "We will say hello to 'world' if no name is given", (), "world");
    // here, `{value}` is a placeholder that doesn't have example text, so we need to give it ourselves
    DECLARE_AND_REGISTER_MESSAGE(Hello, "example for {value} is 'world'", (msg::value), "Hello, {value}!");
    // here, `{triplet}` _already has_ example text, so it's fine to not give a comment
    DECLARE_AND_REGISTER_MESSAGE(MyTripletIs, "", (msg::triplet), "My triplet is {triplet}.");
}

namespace vcpkg::Commands
{
    void HelloWorldCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                             const VcpkgPaths& paths
                                             Triplet default_triplet,
                                             Triplet) const
    {
        (void)args.parse_arguments(HELLO_WORLD_COMMAND_STRUCTURE); 

        LocalizedString name;
        if (args.command_arguments.size() == 0)
        {
            name = msg::format(msgWorld);
        }
        else
        {
            name = LocalizedString::from_raw(args.command_arguments[0]);
        }

        msg::println(msgHello, msg::value = name);
        msg::println(msgMyTripletIs, msg::triplet = default_triplet);
    }
}
```
