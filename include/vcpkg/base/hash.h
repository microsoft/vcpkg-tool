#pragma once

#include <vcpkg/base/fwd/expected.h>

#include <vcpkg/base/files.h>
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
        void add_bytes(StringView data);
        virtual void add_bytes(const void* start, const void* end) noexcept = 0;

        // one may only call this once before calling `clear()` or the dtor
        virtual std::string get_hash() = 0;
        virtual void clear() noexcept = 0;
        virtual ~Hasher() = default;
    };

    std::unique_ptr<Hasher> get_hasher_for(Algorithm algo);

    std::string get_bytes_hash(const void* first, const void* last, Algorithm algo);
    std::string get_string_hash(StringView s, Algorithm algo);
    ExpectedL<std::string> get_file_hash(const Filesystem& fs, const Path& target, Algorithm algo);
}
