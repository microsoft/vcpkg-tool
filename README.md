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

# Telemetry

vcpkg collects usage data in order to help us improve your experience.
The data collected by Microsoft is anonymous.
You can opt-out of telemetry by re-running the bootstrap-vcpkg script with -disableMetrics,
passing --disable-metrics to vcpkg on the command line,
or by setting the VCPKG_DISABLE_METRICS environment variable.

Read more about vcpkg telemetry at docs/about/privacy.md
