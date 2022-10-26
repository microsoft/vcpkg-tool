# Writing Localizeable Code

As part of Microsoft's commitment to global-ready products, our developer tools are localized into 14 different languages.
For this effort, vcpkg has hooks that allow our localization team to translate all messages shown to the user.
As a developer on vcpkg, the only thing you have to consider is the messages in the C++ source files,
and `locales/messages.json` -- everything else will be generated and modified by the localization team.

## Declaring a Message

The process of writing a user-visible message starts with declaring it.
Most user-facing messages can be declared in [`messages.h`] and registered in [`messages.cpp`]:

```messages.h
DECLARE_MESSAGE(<message-name>, <parameters>, <comment>, <english-message>);
```
```messages.cpp
REGISTER_MESSAGE(<message-name>);
```

If you need to declare a message in a header file
(for example, if a templated function uses it),
then you need to first declare it in the header:

```cxx
DECLARE_MESSAGE(<message-name>, <parameters>, <comment>, <english-message>);
```

and then register it in the corresponding source file:

```cxx
REGISTER_MESSAGE(<message-name>);
```

An example of this lies in [`graphs.h`] and [`graphs.cpp`].

[`sourceparagraph.cpp`]: https://github.com/microsoft/vcpkg-tool/blob/13a09ef0359e259627d46560a22a6e182730da7b/src/vcpkg/sourceparagraph.cpp#L24-L28
[`graphs.h`]: https://github.com/microsoft/vcpkg-tool/blob/ca8099607bfa71adac301b56c601fd71d8ccab9b/include/vcpkg/base/graphs.h#L13
[`graphs.cpp`]: https://github.com/microsoft/vcpkg-tool/blob/ca8099607bfa71adac301b56c601fd71d8ccab9b/src/vcpkg/base/graphs.cpp#L5

### Message Names

Message names should be descriptive and unique, and must be `CamelCase`.
When referring to a message in `msg::format`, you must add `msg` to the front of it; for example,
`DECLARE_MESSAGE(MyMessage, ...)` can be referred to as `msgMyMessage`.

### Parameters

If the message contains placeholders, each one must have a corresponding parameter.
For example, `"Installing {package_name}:{triplet}"` should have the parameter list `(msg::package_name, msg::triplet)`.
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

### Message

Messages in vcpkg are written in American English. They should not contain:

* formatting:
  - indentation should be added with the `append_indent()` function;
    if you need to add more than one indentation, you can use `append_indent(N)`
  - Any other interesting characters (like `- ` for lists, for example) should use `append_raw(...)`
* or for the prefixes:
  - `"warning: "`, instead use `msg::format(msg::msgWarningMessage).append(msgMyWarning)`
  - `"error: "`, instead use `msg::msgErrorMessage`
  - `"internal error: "`, instead use `msg::msgInternalErrorMessage`.

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
    DECLARE_MESSAGE(World, (), "We will say hello to 'world' if no name is given", "world");
    // here, `{value}` is a placeholder that doesn't have example text, so we need to give it ourselves
    DECLARE_MESSAGE(Hello, (msg::value), "example for {value} is 'world'", "Hello, {value}!");
    // here, `{triplet}` _already has_ example text, so it's fine to not give a comment
    DECLARE_MESSAGE(MyTripletIs, (msg::triplet), "", "My triplet is {triplet}.");
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
## Selecting a Language

Vcpkg chooses a language through the VSLANG environment variable, which should be set to a valid LCID (locale identifier, 4-byte value representing a language). Users can set VSLANG to any of the following LCIDs:

1029: Czech                 \
1031: German                \
1033: English               \
3082: Spanish (Spain)       \
1036: French                \
1040: Italian               \
1041: Japanese              \
1042: Korean                \
1045: Polish                \
1046: Portuguese (Brazil)   \
1049: Russian               \
1055: Turkish               \
2052: Chinese (Simplified)  \
1028: Chinese (Traditional)