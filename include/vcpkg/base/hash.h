#pragma once

#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>

#include <string>

namespace vcpkg::Hash
{
    enum class Algorithm
    {
        Sha256,
        Sha512,
    };

    const char* to_string(Algorithm algo) noexcept;
    Optional<Algorithm> algorithm_from_string(StringView sv) noexcept;

    struct Hasher
    {
        virtual void add_bytes(const void* start, const void* end) noexcept = 0;

        // one may only call this once before calling `clear()` or the dtor
        virtual std::string get_hash() noexcept = 0;
        virtual void clear() noexcept = 0;
        virtual ~Hasher() = default;
    };

    std::unique_ptr<Hasher> get_hasher_for(Algorithm algo) noexcept;

    std::string get_bytes_hash(const void* first, const void* last, Algorithm algo) noexcept;
    std::string get_string_hash(StringView s, Algorithm algo) noexcept;
    std::string get_file_hash(const Filesystem& fs, const Path& target, Algorithm algo, std::error_code& ec) noexcept;

    DECLARE_MESSAGE(HashFileFailureToRead,
                    (msg::path, msg::error),
                    "example of {error} is 'no such file or directory'",
                    "failed to read file '{path}' for hashing: {error}");
    inline std::string get_file_hash(LineInfo li, const Filesystem& fs, const Path& target, Algorithm algo) noexcept
    {
        std::error_code ec;
        const auto result = get_file_hash(fs, target, algo, ec);
        if (ec)
        {
            msg::print(Color::error, msg::msgErrorMessage);
            Checks::msg_exit_with_message(li, msgHashFileFailureToRead, msg::path = target, msg::error = ec.message());
        }

        return result;
    }
}
