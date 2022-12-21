1. Build `vcpkg-tool`. Set `VCPKG_COMMAND` to the built bits. For example:
$env:VCPKG_COMMAND="C:\Dev\vcpkg-tool\out\build\x64-Release\vcpkg.exe"

2. Set `VCPKG_ROOT` to a vcpkg root. For example:
$env:VCPKG_ROOT="C:\Dev\vcpkg"

3. `cd ce && rush update && rush rebuild`

4. Go to your normal dev directory or similar and `git clone https://github.com/raspberrypi/pico-examples`

5. `cd` to that clone and copy this `vcpkg-configuration.json` there:

```
{
  "registries": [
    {
      "name": "test",
      "location": "https://github.com/microsoft/vcpkg-ce-catalog/archive/refs/heads/metadata-changes.zip",
      "kind": "artifact"
    }
  ],
  "requires": {
    "test:compilers/arm/gcc": "2020.10.0",
    "test:raspberrypi/pico-sdk": "* 1.3.0",
    "test:tools/kitware/cmake": "3.20.1",
    "test:tools/microsoft/openocd": "* 0.11.0"
  }
}
```

6. Run `C:\Path\To\vcpkg-tool\build\vcpkg.ps1 activate`
