# Vcpkg: Overview

[中文总览](https://github.com/microsoft/vcpkg/blob/master/README_zh_CN.md)
[Español](https://github.com/microsoft/vcpkg/blob/master/README_es.md)
[한국어](https://github.com/microsoft/vcpkg/blob/master/README_ko_KR.md)
[Français](https://github.com/microsoft/vcpkg/blob/master/README_fr.md)

Vcpkg helps you manage C and C++ libraries on Windows, Linux and MacOS.
This tool and ecosystem are constantly evolving, and we always appreciate contributions!

Please see the main repository https://github.com/microsoft/vcpkg for all feature discussion, issue
tracking, and edits to which libraries are available.

# Vcpkg-tool: Overview

This repository contains the contents formerly at https://github.com/microsoft/vcpkg in the
"toolsrc" tree, and build support.

# Vcpkg-ce: "Configure Environment" / artifacts

Parts of vcpkg powered by "ce" are currently in 'preview' -- there will most certainly be changes between now
and when the tool is 'released' based on feedback. 

You can use it, but be forewarned that we may change formats, commands, etc. 

Think of it as a manifest-driven desired state configuration for C/C++ projects. 

It 
 - integrates itself into your shell (PowerShell, CMD, bash/zsh)
 - can restore artifacts according to a manifest that follows one’s code 
 - provides discoverability interfaces

## Installation

While the usage of `ce` is the same on all platforms, the installation/loading/removal is slightly different depending on the platform you're using.

`ce` doesn't persist any changes to the environment, nor does it automatically add itself to the start-up environment. If you wish to make it load in a window, you can just execute the script. Manually adding that in your profile will load it in every new window.

<hr>

## Install/Use/Remove

| OS              | Install                                             | Use                   | Remove                          |
|-----------------|-----------------------------------------------------|-----------------------|---------------------------------|
| **PowerShell/Pwsh** |`iex (iwr -useb https://aka.ms/vcpkg-init.ps1)`              |` . ~/.vcpkg/vcpkg-init.ps1`          | `rmdir -recurse ~/.vcpkg`          |
| **Linux/OSX**       |`. <(curl https://aka.ms/vcpkg-init.sh -L)`                  |` . ~/.vcpkg/vcpkg-init.sh`          | `rm -rf ~/.ce`                  |
| **CMD Shell**       |`curl -LO https://aka.ms/vcpkg-init.cmd && .\vcpkg-init.cmd` |`%USERPROFILE%\.vcpkg\vcpkg-init.cmd` | `rmdir /s /q %USERPROFILE%\.vcpkg` |

## Glossary

| Term       | Description                                         |
|------------|-----------------------------------------------------|
| `artifact` | An archive (.zip or .tar.gz-like), package (.nupkg, .vsix) binary inside which build tools or components thereof are stored. |
| `artifact metadata` | A description of the locations one or more artifacts describing rules for which ones are deployed given selection of a host architecture, target architecture, or other properties|
| `artifact identity` | A short string that uniquely describes a moniker that a given artifact (and its metadata) can be referenced by. They can have one of the following forms:<br> `full/identity/path` - the full identity of an artifact that is in the built-in artifact source<br>`sourcename:full/identity/path` - the full identity of an artifact that is in the artifact source specified by the sourcename prefix<br>`shortname` - the shortened unique name of an artifact that is in the built-in artifact source<br>`sourcename:shortname` - the shortened unique name of an artifact that is in the artifact source specified by the sourcename prefix<br>Shortened names are generated based off the shortest unique identity path in the given source. |
| `artifact source` | Also known as a “feed”. An Artifact Source is a location that hosts metadata to locate artifacts. (_There is only one source currently_) |
| `project profile` | The per-project configuration file (`environment.yaml` or `environment.json`) 
| `AMF`&nbsp;or&nbsp;`Metadata`&nbsp;`Format` | The schema / format of the YAML/JSON files for project profiles, global settings, and artifacts metadata. |
| `activation` | The process by which a particular set of artifacts are acquired and enabled for use in a calling command program.|
| `versions` | Version numbers are specified using the Semver format. If a version for a particular operation isn't specified, a range for the latest version ( `*` ) is assumed. A version or version range can be specified using the npm semver matching syntax. When a version is stored, it can be stored using the version range specified, a space and then the version found. (ie, the first version is what was asked for, the second is what was installed. No need for a separate lock file.) |


# Contributing

Please refer to the "contributing" section of the
[main `README.md`](https://github.com/microsoft/vcpkg/blob/master/README.md).

This project has adopted the [Microsoft Open Source Code of Conduct][contributing:coc].
For more information see the [Code of Conduct FAQ][contributing:coc-faq]
or email [opencode@microsoft.com](mailto:opencode@microsoft.com)
with any additional questions or comments.

[contributing:submit-issue]: https://github.com/microsoft/vcpkg/issues/new/choose
[contributing:submit-pr]: https://github.com/microsoft/vcpkg/pulls
[contributing:coc]: https://opensource.microsoft.com/codeofconduct/
[contributing:coc-faq]: https://opensource.microsoft.com/codeofconduct/

# License

The product code in this repository is licensed under the [MIT License](LICENSE.txt). The tests
contain 3rd party code as documented in `NOTICE.txt`.

# Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft 
trademarks or logos is subject to and must follow 
[Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general).
Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship.
Any use of third-party trademarks or logos are subject to those third-party's policies.

# Telemetry

vcpkg collects usage data in order to help us improve your experience.
The data collected by Microsoft is anonymous.
You can opt-out of telemetry by re-running the bootstrap-vcpkg script with -disableMetrics,
passing --disable-metrics to vcpkg on the command line,
or by setting the VCPKG_DISABLE_METRICS environment variable.

Read more about vcpkg telemetry at [docs/about/privacy.md](https://github.com/microsoft/vcpkg/blob/master/docs/about/privacy.md)
in the main repository
