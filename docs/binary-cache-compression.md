# Binary cache compression

Set `--binary-cache-compression-level=<0-9>` or the environment variable
`VCPKG_BINARY_CACHE_COMPRESSION_LEVEL=<0-9>` to control ZIP creation for binary
caching. The command-line option takes precedence over the environment variable.
Values must be a single digit from `0` to `9`; other values, including an empty
value, are rejected when initializing the binary cache.

```sh
vcpkg install zlib --binary-cache-compression-level=1
VCPKG_BINARY_CACHE_COMPRESSION_LEVEL=0 vcpkg install zlib
```

Level `0` stores files without compression, `1` requests the fastest compression,
and `9` requests maximum compression. Intermediate levels trade compression time
for archive size. Exact compression behavior depends on the archive tool.

When neither setting is supplied, vcpkg preserves the archive tool's existing
default. Windows passes configured levels to 7-Zip as `-tzip -mx=<level>`;
Linux and macOS pass them to `zip` as `-<level>`.

The setting applies to all binary-cache providers that use vcpkg's ZIP archive
creation, including files, HTTP uploads, and object storage. NuGet packages are
created by NuGet and are not affected. Restoration and package ABI hashes are
unchanged; changing the level does not invalidate existing cache entries.
