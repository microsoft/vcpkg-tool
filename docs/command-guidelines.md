A command name is what the user types to run the command, without any x- prefix. (Note, z- prefixes
are kept)

Examples:
* search => search
* x-download => download
* z-applocal => z-applocal

This reflects that x- commands are eXperimental commands that one day will be promoted and lose
the x-, while z- commands are intended for internal use only that can be changed or removed at any
time.

Each "subcommand" added to vcpkg needs the following.

- [ ] Commands are named with dashes, not underscores.
- [ ] A `.cpp` containing the command named "`commands.command-name.cpp`".
- [ ] An entry in `commands.cpp` dispatching to that subcommand and registering for help.
- [ ] An entry point function named `vcpkg::command_command_name_and_exit` corresponding with one of 
      the CommandFn prototypes in `commands.h`. Note that the dashes are replaced with underscores
      to make this a valid C++ identifier.
- [ ] A localization message named CmdCommandNameSynopsis with a short synopsis.
- [ ] One or more examples named CmdCommandNameExample1, CmdCommandNameExample2, .... If possible,
      some examples should use `<placeholders>` and some should use real data. Example:  
      `vcpkg install <package name>`  
      `vcpkg install zlib`
- [ ] An `extern const CommandMetadata CommandCommandNameMetadata` describing the command connected
      to `vcpkg help`, unless the command is not intended for users to use.
- [ ] An entry in print_command_list_usage(), if appropriate.
- [ ] A documentation page on vcpkg learn.microsoft.com, unless the command is not intended for 
      users to use.