# Writing Localizeable Code

As part of Microsoft's commitment to global-ready products, our developer tools are localized into 14 different languages.
For this effort, vcpkg has hooks that allow our localization team to translate all messages shown to the user.

All localized messages start in `include/vcpkg/base/message-data.inc.h`. After modifying those messages, you will need to regenerate `locales/messages.json` using  `vcpkg x-generate-default-message-map` and include the updated file in your PR. After merging the PR, the localization team will modify all the language-specific files under `locales/`.

## Creating a New Message

Messages are defined by calls to `DECLARE_MESSAGE` `include/vcpkg/base/message-data.inc.h`.

```cpp
DECLARE_MESSAGE(<message-name>, <parameters>, <comment>, <english-message>)
```

For example:

```cpp
DECLARE_MESSAGE(AddVersionVersionAlreadyInFile, (msg::version, msg::path), "", "version {version} is already in {path}")
```

The messages are sorted alphabetically by `<message-name>`. The `<message-name>` should be descriptive, unique, and `CamelCase`.

### Parameters

If the message contains placeholders, each one must have a corresponding parameter.
For example, `"Installing {package_name}:{triplet}"` should have the parameter list `(msg::package_name, msg::triplet)`.
This allows us to make sure that we don't have format errors, where one forgets a placeholder,
or adds an extra one.
Each parameter in the list should be used in the message.

Parameters are defined in `include/vcpkg/base/message-args.inc.h`.

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

## Using Messages

For each message `MyMessage`, `include/base/messages.h` will declare a `::vcpkg::msgMyMessage` object of some `MessageT` type. Many functions accept `MessageT` objects as arguments, however the primary method is `msg::format()`:

```cpp
LocalizedString make_my_info(const Path& p) {
    return msg::format(msgMyMessage, msg::path = p);
}
```

## Declaring Message-Forwarding Functions

There are three macros defined by `include/base/messages.h` that aid in creating perfect message forwarders: `VCPKG_DECL_MSG_TEMPLATE`, `VCPKG_DECL_MSG_ARGS`, and `VCPKG_EXPAND_MSG_ARGS`. These work similarly to `class...Args`, `Args&&...args`, and `std::forward<Args>(args)...` respectively.

For example:

```cpp
template<VCPKG_DECL_MSG_TEMPLATE>
void add_info_message(VCPKG_DECL_MSG_ARGS) {
  m_info.append(VCPKG_EXPAND_MSG_ARGS).append_raw('\n');
}
```

We discourage creating new Message-forwarding functions and encourage accepting a `LocalizedString` instead.

## Example

Let's create a vcpkg hello world command to show off how one can use the messages API.

```cpp
// include/vcpkg/base/message-data.inc.h

// note that we add additional context in the comment here
DECLARE_MESSAGE(World, (), "We will say hello to 'world' if no name is given", "world")
// here, `{value}` is a placeholder that doesn't have example text, so we need to give it ourselves
DECLARE_MESSAGE(Hello, (msg::value), "example for {value} is 'world'", "Hello, {value}!")
// here, `{triplet}` _already has_ example text, so it's fine to not give a comment
DECLARE_MESSAGE(MyTripletIs, (msg::triplet), "", "My triplet is {triplet}.")
```

```cxx
// commands.hello-world.cpp
#include <vcpkg/base/messages.h>

#include <vcpkg/commands.hello-world.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    const CommandStructure HELLO_WORLD_COMMAND_STRUCTURE = {
        create_example_string("hello-world <name>"),
        0,
        1,
        {{}, {}, {}},
        nullptr,
    };
}

namespace vcpkg
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

vcpkg chooses a language through the `VSLANG` environment variable, which should be set to a valid LCID (locale identifier, 4-byte value representing a language). If `VSLANG` is not set or is set to an invalid LCID, we default to English. That said, if you wish to change the language, then set `VSLANG` to any of the following LCIDs:

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