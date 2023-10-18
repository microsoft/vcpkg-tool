# vcpkg_tool_release_process

This document describes the acceptance criteria / process we use when doing a vcpkg-tool update,
such as https://github.com/microsoft/vcpkg/pull/23757

1. Update `$/vcpkg-init/vcpkg-scripts-sha.txt` to the current `master` branch SHA in the registry
  repo.
1. Verify that all tests etc. are passing in the vcpkg-tool repo's `main` branch, and that the
  contents therein are acceptable for release. (Steps after this will sign code there, so this
  review is responsible gating what has access to code signing.)
1. Check that the changes there are in fact the changes that we want in that release. (Be aware,
  you are responsible for what is about to be signed with a Microsoft code signing certificate by
  proceeding)
1. Submit a signed build from "vcpkg Signed Binaries (from GitHub)" (
  https://devdiv.visualstudio.com/DevDiv/_build?definitionId=17772&_a=summary as of this writing)
1. The signed build will automatically create a draft GitHub release at
  https://github.com/microsoft/vcpkg-tool/releases . Erase the contents filled in there and press
  the "auto generate release notes" button. Manually remove any entries created by the automated
  localization tools which will start with `* LEGO: Pull request from juno/`.
1. Publish that draft release as "pre-release".
1. Clean up a machine for the following tests:
    * Delete `VCPKG_DOWNLOADS/artifacts` (which forces artifacts to be reacquired)
    * Delete `LOCALAPPDATA/vcpkg` (which forces registries to be reacquired)
1. Smoke test the 'one liner' installer: (Where 2023-03-29 is replaced with the right release name)
    * Powershell:
        `iex (iwr https://github.com/microsoft/vcpkg-tool/releases/download/2023-03-29/vcpkg-init.ps1)`
    * Batch:
        `curl -L -o vcpkg-init.cmd https://github.com/microsoft/vcpkg-tool/releases/download/2023-03-29/vcpkg-init.ps1 && .\vcpkg-init.cmd`
    * Bash:
        `. <(curl https://github.com/microsoft/vcpkg-tool/releases/download/2023-03-29/vcpkg-init -L)`
  (and test that `vcpkg use cmake` works from each of these)
1. Create a new task in the DevDiv VS instance for this release. (PRs into VS Code and VS require an associated work
   item in order to be merged.)
1. In the vcpkg repo, run `\scripts\update-vcpkg-tool-metadata.ps1 -Date 2023-03-29`
  with the new release date, which updates SHAs as appropriate. It will also emit a code block for
  the next vscode-embedded-tools repo step. Commit these changes and submit as a PR.
1. In the DevDiv vscode-embedded-tools repo, follow the
  [update instructions](https://devdiv.visualstudio.com/DefaultCollection/DevDiv/_git/vscode-embedded-tools?path=/docs/updating-vcpkg.md&_a=preview)
  to make a VS Code update PR. Don't forget to attach the work item created in the previous step.
  Example: https://devdiv.visualstudio.com/DefaultCollection/DevDiv/_git/vscode-embedded-tools/pullrequest/498107
1. If changes in this release that might affect ports, submit a new full tree rebuild by
  microsoft.vcpkg.ci (https://dev.azure.com/vcpkg/public/_build?definitionId=29 as of this writing)
  targeting `refs/pull/NUMBER/head`
1. (Probably the next day) Check over the failures and ensure any differences with the most recent
  full rebuild using the previous tool version are understood.
1. In the DevDiv VS repo ( https://devdiv.visualstudio.com/DefaultCollection/DevDiv/_git/VS ),
   update `.corext\Configs\default.config`, and `src\ConfigData\Packages\Redist\Setup.props`
1. The first time you try to do a VS update on a machine, open a developer command prompt, go to
   `src\vc\projbld\Vcpkg\VcpkgInsertionUtility`, and run `csc Program.cs` which will write the
   VcpkgInsertionUtility as `Program.exe` in this directory.
1. Go to the root of the VS repo and run `init.cmd -CoreXTProfileName VSPartners`
1. Submit this as a change to the VS repo. Example: https://devdiv.visualstudio.com/DefaultCollection/DevDiv/_git/VS/pullrequest/498110
   Don't forget to attach the work item number from the previous step.
1. Smoke test the copy of vcpkg inserted into VS. See smoke test steps below.
1. (After all tests have passed, at the same time) Merge all 3 PRs, and change the github release
  in vcpkg-tool from "prerelease" to "release". (This automatically updates the aka.ms links)

# Release Data Flow

```mermaid
flowchart TD
    vcpkg_repo[(GitHub vcpkg Repo)]
    vcpkg_tool_repo[(GitHub vcpkg-tool Repo)]
    vcpkg_tool_releases[(GitHub vcpkg-tool Releases)]
    vcpkg_signed_binaries{"vcpkg Signed Binaries (from GitHub)"}
    vs_nuget_feed[(devdiv/VS NuGet Feed)]
    vs_corext_feed[(devdiv/VS-CoreXtFeeds NuGet Feed)]
    subgraph vcpkg_update_pr [vcpkg Update PR]
        direction LR
        update_vcpkg_tool_metadata_ps1[update-vcpkg-tool-metadata.ps1]
        vcpkg_tool_metadata_txt[vcpkg-tool-metadata.txt]
        vcpkg_repo_target[(GitHub vcpkg Repo)]
    end
    create_vcpkg_pr{Create GitHub vcpkg PR}
    run_full_tree_rebuild{Maybe Run Full Rebuild}
    release_version[(Version Date and SHAs)]
    subgraph vs_update_pr [Visual Studio Update PR]
        direction LR
        vs_repo[(devdiv VS Repo)]
        default_config[default.config]
        run_insertion{Run VcpkgInsertionUtility}
    end
    create_vs_pr{Create VS PR}
    smoke_test_vs{Smoke Test VS}
    merge{Merge All PRs, Make Not Prerelease, At Same Time}
    smoke_test_cmd{Smoke Test vcpkg-init.cmd}
    smoke_test_ps1{Smoke Test vcpkg-init.ps1}
    smoke_test_bash{Smoke Test vcpkg-init.sh}
    subgraph vs_code_update_pr [Visual Studio Code Update PR]
        direction LR
        package_json[(package.json)]
        changelog_md[(CHANGELOG.md)]
        vs_embedded_tools_repo[(vs_embedded_tools Repo)]
    end
    create_vs_code_update_pr{Create Visual Studio Code Update PR}
    symweb[(//symweb, etc.)]
    
    %% Build the Release
    vcpkg_tool_repo --> vcpkg_signed_binaries
    vcpkg_repo -- vcpkg-tool/vcpkg-scripts-sha.txt --> vcpkg_signed_binaries
    vcpkg_signed_binaries --> vcpkg_tool_releases
    vcpkg_signed_binaries --> release_version
    vcpkg_signed_binaries --> symweb
    
    %% vcpkg Update
    release_version --> update_vcpkg_tool_metadata_ps1
    update_vcpkg_tool_metadata_ps1 --> vcpkg_tool_metadata_txt
    vcpkg_tool_metadata_txt --> vcpkg_repo_target
    vcpkg_update_pr ----> create_vcpkg_pr
    create_vcpkg_pr --> run_full_tree_rebuild
    
    %% VS Update
    vcpkg_signed_binaries --> vs_nuget_feed
    vs_nuget_feed -- upstream --> vs_corext_feed
    vs_corext_feed --> default_config
    release_version --> default_config
    default_config --> run_insertion
    run_insertion --> vs_repo
    vs_update_pr --> create_vs_pr
    
    %% VS Code Update
    update_vcpkg_tool_metadata_ps1 --> package_json
    release_version --> changelog_md
    package_json --> vs_embedded_tools_repo
    changelog_md --> vs_embedded_tools_repo
    vs_code_update_pr --> create_vs_code_update_pr
    
    %% Smoke Testing
    create_vs_pr --> smoke_test_vs
    vcpkg_tool_releases --> smoke_test_cmd
    vcpkg_tool_releases --> smoke_test_ps1
    vcpkg_tool_releases --> smoke_test_bash
    
    %% Go go go!
    smoke_test_cmd --> merge
    smoke_test_ps1 --> merge
    smoke_test_bash --> merge
    smoke_test_vs --> merge
    run_full_tree_rebuild --> merge
    create_vs_code_update_pr --> merge
```

* [vcpkg Signed Binaries (from GitHub)](https://devdiv.visualstudio.com/DevDiv/_build?definitionId=17772&_a=summary) Uses the azure-pipelines/signing.yml file in the vcpkg-tool repo.
* [devdiv/VS NuGet Feed](https://devdiv.visualstudio.com/DefaultCollection/DevDiv/_artifacts/feed/VS/NuGet/VS.Redist.Vcpkg.amd64/)
* [devdiv/VS-CoreXtFeeds NuGet Feed](https://devdiv.visualstudio.com/DefaultCollection/DevDiv/_artifacts/feed/VS-CoreXtFeeds/NuGet/VS.Redist.Vcpkg.amd64/)
* [devdiv VS Repo](https://devdiv.visualstudio.com/DefaultCollection/DevDiv/_git/VS)
  * Run VcpkgInsertionUtility at `$/src/vc/projbld/Vcpkg/VcpkgInsertionUtility/run.cmd`
* [vscode-embedded-tools Repo](https://devdiv.visualstudio.com/DefaultCollection/DevDiv/_git/vscode-embedded-tools)
  * `package.json` and `CHANGELOG.md` are in `$/app/vscode-embedded-tools`
* Run Full Rebuild: Queue a [microsoft.vcpkg.ci](https://dev.azure.com/vcpkg/public/_build?definitionId=29) run for `refs/pull/NUMBER/head`
  * Example: https://dev.azure.com/vcpkg/public/_build/results?buildId=73664&view=results

# Smoke Testing VS

1. Install the prototype version of VS with the vcpkg inserted. Ensure the native desktop workload is selected, and that vcpkg and cmake bits are installed. Don't forget about preinstall.
1. Open a developer command prompt and run `vcpkg integrate install` (this step hopefully removed soon)
    * This also verifies that vcpkg installed into the developer command prompt correctly.
1. Create a new C++ console project.
1. Turn on diagnostic logging.
    * Tools -> Options: Projects and Solutions\Build and Run\\MSBuild project output verbosity
1. Build the console project, check that vcpkg isn't affecting that project:
    * Lib AdditionalLibraryDirectories doesn't contain a hypothetical vcpkg installed directory
    * Target VcpkgInstallManifestDependencies doesn't run
    * Target AppLocalFromInstalled doesn't run
1. In the developer command prompt cd to the directory with the vcxproj for the console app and run:
    ```
    vcpkg new --application
    vcpkg add port zlib
    ```
1. Rebuild the console app, and verify the manifest mode warning is printed:
    ```
    1>Target "VcpkgCheckManifestRoot" in file "C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\vcpkg\scripts\buildsystems\msbuild\vcpkg.targets":
    1>  Task "Error" skipped, due to false condition; ('$(VcpkgEnableManifest)' == 'true' and '$(_ZVcpkgManifestRoot)' == '') was evaluated as ('false' == 'true' and 'C:\Users\bion\source\repos\ConsoleApplication3\ConsoleApplication3\' == '').
    1>  Task "Message"
    1>    Task Parameter:Importance=High
    1>    Task Parameter:Text=The vcpkg manifest was disabled, but we found a manifest file in C:\Users\bion\source\repos\ConsoleApplication3\ConsoleApplication3\. You may want to enable vcpkg manifests in your properties page or pass /p:VcpkgEnableManifest=true to the msbuild invocation.
    1>    The vcpkg manifest was disabled, but we found a manifest file in C:\Users\bion\source\repos\ConsoleApplication3\ConsoleApplication3\. You may want to enable vcpkg manifests in your properties page or pass /p:VcpkgEnableManifest=true to the msbuild invocation.
    1>  Done executing task "Message".
    ```
1. Right click the console application, properties, and in the property pages change vcpkg\\Use vcpkg Manifest to "Yes"
1. Rebuild the project, observe vcpkg builds zlib.
1. Change the .cpp to:
    ```
    #include <iostream>
    #include <zlib.h>

    int main()
    {
        std::cout << "Hello World!\n" << ZLIB_VERSION;
    }
    ```
1. Run the program and verify that a reasonable zlib version is printed.
1. Close Visual Studio.
1. In the directory of that vcxproj, create a CMakeLists.txt with the following content:
    ```
    cmake_minimum_required(VERSION 3.24)
    project(console-app LANGUAGES CXX)
    message(WARNING "CMake Version is ${CMAKE_VERSION}")
    find_package(ZLIB REQUIRED)
    file(GLOB PROGRAM_SOURCES *.cpp)
    add_executable(program ${PROGRAM_SOURCES})
    target_link_libraries(program PRIVATE ZLIB::ZLIB)
    ```
1. Back in the developer command prompt, run:
    ```
    cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -S . -B build_msvc
    ninja -C build_msvc
    build_msvc\program.exe
    ```
    and check that a reasonable zlib version is printed.
1. Back in the developer command prompt, verify that the copy of CMake can be customized by running:
    ```
    vcpkg use cmake
    cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -S . -B build_artifact
    ninja -C build_artifact
    build_artifact\program.exe
    ```
    and check that the cmake version acquired by artifacts is printed during the cmake configure, and that a reasonable zlib version is printed.
