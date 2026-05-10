# This port is for testing that x-script works; see test-script-asset-cache.c
vcpkg_download_distfile(
    SOURCE_PATH
    URLS https://raw.githubusercontent.com/microsoft/vcpkg-tool/1767aaee7b229c609f7ad5cf2f57b6a6cc309fb8/LICENSE.txt
    # This must stay uppercase to check that the SHA512 is properly tolower'd when it gets passed to x-script
    SHA512 65077997890F66F6041BB3284BB7B88E27631411CCBC253201CA4E00C4BCC58C0D77EDFFDA4975498797CC10772C7FD68FBEB13CC4AC493A3471A9D49E5B6F24
    FILENAME vcpkg-tool-1767aaee7b229c609f7ad5cf2f57b6a6cc309fb8-LICENSE.txt
)

file(READ "${SOURCE_PATH}" CONTENTS)
if (NOT CONTENTS STREQUAL [[Copyright (c) Microsoft Corporation

All rights reserved.

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
]])
    message(FATAL_ERROR "Downloaded file has incorrect contents: ${CONTENTS}")
endif()

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
