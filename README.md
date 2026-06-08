# Vcpkg: Overview

[中文总览](https://learn.microsoft.com/zh-cn/vcpkg/get_started/overview)
[Español](https://learn.microsoft.com/es/vcpkg/get_started/overview)
[한국어](https://learn.microsoft.com/ko-kr/vcpkg/get_started/overview)
[Français](https://learn.microsoft.com/fr/vcpkg/get_started/overview)

Vcpkg helps you manage C and C++ libraries on Windows, Linux and MacOS.
This tool and ecosystem are constantly evolving, and we always appreciate contributions!

Please see the main repository https://github.com/microsoft/vcpkg for all feature discussion, issue
tracking, and edits to which libraries are available.

# Vcpkg-tool: Overview

This repository contains the contents formerly at https://github.com/microsoft/vcpkg in the
"toolsrc" tree, and build support.


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

## Windows Contributing Prerequisites

* Install Visual Studio and the C++ workload
* Install Node.JS by downloading a 16.x copy from https://nodejs.org/en/
* `npm install -g @microsoft/rush`

## Ubuntu 22.04 Contributing Prerequisites

```
curl -fsSL https://deb.nodesource.com/setup_16.x | sudo -E bash -
sudo apt update
sudo apt install nodejs cmake ninja-build gcc build-essential git zip unzip
sudo npm install -g @microsoft/rush
```

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

Read more about vcpkg telemetry at https://learn.microsoft.com/vcpkg/about/privacy
in the main repository
