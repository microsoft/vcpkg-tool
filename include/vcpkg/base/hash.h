#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/optional.h>

#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg::Hash
{
    enum class Algorithm
    {
        Sha256,
        Sha512,
    };

    Optional<Algorithm> algorithm_from_string(StringView sv) noexcept;

    struct Hasher
    {
        virtual void add_bytes(const void* start, const void* end) noexcept = 0;

        // one may only call this once before calling `clear()` or the dtor
        virtual std::string get_hash() = 0;
        virtual void clear() noexcept = 0;
        virtual ~Hasher() = default;
    };

    std::unique_ptr<Hasher> get_hasher_for(Algorithm algo);

    std::string get_bytes_hash(const void* first, const void* last, Algorithm algo);
    std::string get_string_hash(StringView s, Algorithm algo);
    std::string get_string_sha256(StringView s);
    // Tries to open `path` for reading, and hashes the contents using the requested algorithm.
    // If the file exists and could be completely read, returns an engaged optional with the stringized hash.
    // Otherwise, returns the read operation error.
    ExpectedL<std::string> get_file_hash(const ReadOnlyFilesystem& fs, const Path& path, Algorithm algo);
    // Tries to open `path` for reading, and hashes the contents using the requested algorithm.
    // If the file exists and could be completely read, returns an engaged optional with the stringized hash.
    // Otherwise, if the file does not exist, returns a disengaged optional.
    // Otherwise, returns the read operation error.
    ExpectedL<Optional<std::string>> get_maybe_file_hash(const ReadOnlyFilesystem& fs,
                                                         const Path& path,
                                                         Algorithm algo);
}
