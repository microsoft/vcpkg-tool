#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/optional.h>

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg::Hash
{
    enum class HashPrognosis
    {
        Success,
        FileNotFound,
        OtherError,
    };

    struct HashResult
    {
        HashPrognosis prognosis = HashPrognosis::Success;
        std::string hash;
    };

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
    // Returns a HashResult with the following outcomes:
    // HashPrognosis::Success: The entire file was read and hashed. The result hash is stored in `hash`.
    // HashPrognosis::FileNotFound: The file does not exist. `hash` is empty string.
    // HashPrognosis::OtherError: An error occurred while reading the file. `hash` is empty string.
    HashResult get_file_hash(DiagnosticContext& context,
                             const ReadOnlyFilesystem& fs,
                             const Path& path,
                             Algorithm algo);

    // Tries to open `path` for reading, and hashes the contents using the requested algorithm.
    // If the file exists and could be completely read, returns an engaged optional with the stringized hash.
    // Otherwise, returns an disengaged optional.
    // Note that the file not existing is interpreted as an error that will be reported to `context`.
    Optional<std ::string> get_file_hash_required(DiagnosticContext& context,
                                                  const ReadOnlyFilesystem& fs,
                                                  const Path& path,
                                                  Algorithm algo);

    // Tries to open `path` for reading, and hashes the contents using the requested algorithm.
    // If the file exists and could be completely read, returns an engaged optional with the stringized hash.
    // Otherwise, returns the read operation error.
    ExpectedL<std::string> get_file_hash(const ReadOnlyFilesystem& fs, const Path& path, Algorithm algo);
}
