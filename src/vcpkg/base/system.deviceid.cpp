#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.deviceid.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/uuid.h>

namespace vcpkg
{
    // To ensure consistency, the uuid must follow the format specified below.
    // - The value follows the 8-4-4-4-12 format(xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)
    // - The value shall be all lowercase and only contain hyphens. No braces or brackets.
    bool validate_device_id(StringView uuid)
    {
        static constexpr size_t UUID_LENGTH = 36;
        static constexpr char format[] = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
        if (uuid.size() != UUID_LENGTH) return false;
        for (size_t i = 0; i < UUID_LENGTH; ++i)
        {
            if (format[i] == '-' && uuid[i] != '-') return false;
            if (format[i] == 'x' && !ParserBase::is_hex_digit_lower(uuid[i])) return false;
        }
        return true;
    }

#if defined(_WIN32)
    // Returns a shared DevDeviceID for telemetry.
    std::string get_device_id()
    {
        // The value is cached in the 64-bit Windows Registry under HKeyCurrentUser\SOFTWARE\Microsoft\DeveloperTools.
        //  The key should be named 'deviceid' and should be of type REG_SZ(String value).
        // The value should be stored in plain text.
        auto maybe_registry_value =
            get_registry_string(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\DeveloperTools", "deviceid");
        if (auto registry_value = maybe_registry_value.get())
        {
            auto device_id = *registry_value;
            return validate_device_id(device_id) ? device_id : std::string{};
        }

        // vcpkg::generate_random_UUID() generates a compliant UUID
        auto new_device_id = Strings::ascii_to_lowercase(vcpkg::generate_random_UUID());
        const auto as_utf16 = Strings::to_utf16(new_device_id);

        const auto status = RegSetKeyValueW(HKEY_CURRENT_USER,
                                            L"SOFTWARE\\Microsoft\\DeveloperTools",
                                            L"deviceid",
                                            REG_SZ,
                                            as_utf16.c_str(),
                                            static_cast<DWORD>((1 + as_utf16.size()) * sizeof(wchar_t)));
        return (status != ERROR_SUCCESS) ? std::string{} : new_device_id;
    }
#else
    std::string get_device_id(const vcpkg::Filesystem& fs)
    {
        /* On Linux:
         * - The folder subpath will be /Microsoft/DeveloperTools
         * - Use $XDG_CACHE_HOME if it is set and not empty, else use $HOME/.cache.
         * - The file will be called 'deviceid'. The value should be stored in plain text, UTF-8.
         *
         * On MacOS:
         * - The folder path will be $HOME\Library\Application Support\Microsoft\DeveloperTools where $HOME is the
         * user's home directory.
         * - The file will be called 'deviceid'.
         * - The value should be stored in plain text, UTF-8.
         */
        const auto maybe_home_path = vcpkg::get_platform_cache_root();
        if (!maybe_home_path)
        {
            return {};
        }

        auto home_path = maybe_home_path.get();
        const auto container_path =
#if defined(__APPLE__)
            *home_path / "Library/Application Support/Microsoft/DeveloperTools"
#else
            *home_path / "Microsoft/DeveloperTools"
#endif
            ;
        const auto id_file_path = container_path / "deviceid";

        std::error_code ec;
        auto maybe_file = fs.exists(id_file_path, ec);
        if (ec)
        {
            return {};
        }

        if (maybe_file)
        {
            auto contents = fs.read_contents(id_file_path, ec);
            if (ec || !validate_device_id(contents))
            {
                return {};
            }
            return contents;
        }

        // vcpkg::generate_random_UUID() generates a compliant UUID
        auto new_device_id = Strings::ascii_to_lowercase(vcpkg::generate_random_UUID());
        fs.create_directories(container_path, ec);
        if (ec)
        {
            return {};
        }

        fs.write_contents(id_file_path, new_device_id, ec);
        if (ec)
        {
            return {};
        }

        return new_device_id;
    }
#endif
}