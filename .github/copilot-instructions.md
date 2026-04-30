# Copilot Instructions for vcpkg-tool

## Build, test, and lint commands

### CMake presets (matches CI)
- Configure: `cmake --preset windows-ci` (or `linux-ci`, `linux-arm64-ci`, `macos-ci`)
- Build: `cmake --build --preset windows-ci -- -k0`
- Run tests: `ctest --preset windows-ci --output-on-failure`

### Run a single test
- Run one CTest target: `ctest --preset windows-ci -R "^vcpkg-test$" --output-on-failure`
- Run specific Catch2 tests directly: `.\out\build\windows-ci\vcpkg-test.exe [tag-or-filter]`
  - Tags follow the source filename convention (for example `[arguments]`).
- Run one e2e suite: `pwsh azure-pipelines/end-to-end-tests.ps1 -Filter "<suite-file-name-without-.ps1>"`

### Local Linux e2e notes
- `vcpkg-tool` is a tool-only repo; local Linux e2e runs need a separate full `microsoft/vcpkg` checkout as `VCPKG_ROOT` for built-in triplets, scripts, and registry history.
- In this dev container environment, `/vcpkg` is likely to already contain that full `microsoft/vcpkg` clone and should be preferred if present.
- Linux e2e runs may also need the PowerShell `Pester` module and `mono-complete` for NuGet export scenarios.
- Some registry tests require full git history in the `VCPKG_ROOT` checkout; shallow clones can fail baseline lookups.

### Formatting / checks
- C++ format check path used in PR workflow: `pwsh .\azure-pipelines\Format-CxxCode.ps1`
- Regenerate message map: `cmake --build --preset windows-ci --target generate-message-map -- -k0`
- Verify message usage: `cmake --build --preset windows-ci --target verify-messages -- -k0`

### vcpkg-artifacts (TypeScript) checks
- Install deps: `npm --prefix .\vcpkg-artifacts ci`
- Lint: `npm --prefix .\vcpkg-artifacts run eslint`
- Unit tests: `npm --prefix .\vcpkg-artifacts test`

## High-level architecture

- `vcpkg` is the CLI executable (`src/vcpkg.cpp`) and dispatches subcommands registered in `src/vcpkg/commands.cpp`.
- Command dispatch is tiered: `basic_commands` run without `VcpkgPaths`, `paths_commands` require initialized paths, and `triplet_commands` additionally resolve default/host triplets before executing.
- Core implementation lives in the `vcpkglib` object library built from `src/vcpkg/*.cpp` and `src/vcpkg/base/*.cpp`, with public headers in `include/vcpkg/**`.
- Tests are built into `vcpkg-test` from `src/vcpkg-test/*.cpp` (Catch2), plus small helper executables (`reads-stdin`, `test-editor`, etc.) used by tests.
- The `vcpkg-artifacts` TypeScript code is bundled into `vcpkg-artifacts.mjs` during CMake builds only when `VCPKG_ARTIFACTS_DEVELOPMENT=ON` (enabled by CI presets).
- Localization is a first-class pipeline: message declarations in `include/vcpkg/base/message-data.inc.h`, generated maps in `locales/messages.json`, and enforcement via `generate-message-map` + `verify-messages`.

## Key conventions

- New commands follow `docs/command-guidelines.md`:
  - file name pattern `commands.<command-name>.cpp` (dashes, not underscores),
  - register command metadata/dispatch in `commands.cpp`,
  - implement the entrypoint as `command_<command_name>_and_exit` (dashes become underscores),
  - add localized synopsis/examples (`CmdCommandNameSynopsis`, `CmdCommandNameExampleN`).
- Command naming semantics: `x-` commands are user-facing experimental commands (invoked without `x-`), while `z-` commands are internal and keep the `z-` prefix.
- Tests in `src/vcpkg-test` use Catch2 tags, and the expected tag convention is `[filename-without-extension]`; use this for targeted runs.
- For localizable output, prefer the message system (`msg::format(...)`, message declarations, and declared message args) over hardcoded English strings or raw warning/error prefixes.
