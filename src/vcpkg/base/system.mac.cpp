#include <vcpkg/base/hash.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.mac.h>
#include <vcpkg/base/system.process.h>

#include <map>

#if !defined(_WIN32)
#include <net/if.h>
#if defined(__APPLE__)
#include <ifaddrs.h>

#include <net/if_dl.h>
#define AF_TYPE AF_LINK
#elif defined(__linux__)
#include <ifaddrs.h>

#include <netpacket/packet.h>
#define AF_TYPE AF_PACKET
#else
// Fallback to use ioctl calls for systems without getifaddrs()
#include <unistd.h>

#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif
#endif

namespace vcpkg
{
    static constexpr size_t MAC_BYTES_LENGTH = 6;
    // 6 hex-bytes (12 hex-digits) + five separators
    static constexpr size_t MAC_STRING_LENGTH = MAC_BYTES_LENGTH * 2 + 5;

    bool validate_mac_address_format(StringView mac)
    {
        if (mac.size() != MAC_STRING_LENGTH) return false;
        for (size_t i = 0; i < MAC_STRING_LENGTH; ++i)
        {
            // every third character is a ':' separator
            if (((i + 1) % 3) == 0)
            {
                if (mac[i] != ':') return false;
                continue;
            }

            if (!ParserBase::is_hex_digit(mac[i])) return false;
        }
        return true;
    }

    bool is_valid_mac_for_telemetry(StringView mac)
    {
        // this exclusion list is the taken from VS Code's source code
        // https://github.com/microsoft/vscode/blob/main/src/vs/base/node/macAddress.ts
        static constexpr std::array<StringLiteral, 3> invalid_macs{
            "00:00:00:00:00:00",
            "ff:ff:ff:ff:ff:ff",
            // iBridge MAC address used on some Apple devices
            "ac:de:48:00:11:22",
        };

        if (!validate_mac_address_format(mac)) return false;
        return !Util::Vectors::contains(invalid_macs, mac);
    }

    std::string mac_bytes_to_string(const Span<unsigned char>& bytes)
    {
        static constexpr char hexits[] = "0123456789abcdef";

        char mac_address[MAC_STRING_LENGTH];
        char* mac = mac_address;
        unsigned char c = static_cast<unsigned char>(bytes[0]);
        *mac++ = hexits[(c & 0xf0) >> 4];
        *mac++ = hexits[(c & 0x0f)];
        unsigned char non_zero_mac = c;
        for (size_t i = 1; i < MAC_BYTES_LENGTH; ++i)
        {
            c = static_cast<unsigned char>(bytes[i]);
            *mac++ = ':';
            *mac++ = hexits[(c & 0xf0) >> 4];
            *mac++ = hexits[(c & 0x0f)];
            non_zero_mac |= c;
        }

        return std::string(mac_address, non_zero_mac ? MAC_STRING_LENGTH : 0);
    }

    bool extract_mac_from_getmac_output_line(StringView line, std::string& out)
    {
        // getmac /V /NH /FO CSV
        // outputs each interface on its own comma-separated line
        // "connection name","network adapter","physical address","transport name"
        auto is_quote = [](auto ch) -> bool { return ch == '"'; };

        auto parser = ParserBase(line, "getmac ouptut");

        out.clear();

        // ignore "connection name"
        if (parser.require_character('"')) return false;
        parser.match_until(is_quote);
        if (parser.require_character('"')) return false;
        if (parser.require_character(',')) return false;

        // ignore "network adapter"
        if (parser.require_character('"')) return false;
        parser.match_until(is_quote);
        if (parser.require_character('"')) return false;
        if (parser.require_character(',')) return false;

        // get "physical address"
        if (parser.require_character('"')) return false;
        auto mac_address = parser.match_until(is_quote).to_string();
        if (parser.require_character('"')) return false;
        if (parser.require_character(',')) return false;

        // ignore "transport name"
        if (parser.require_character('"')) return false;
        parser.match_until(is_quote);
        if (parser.require_character('"')) return false;

        if (!parser.at_eof()) return false;

        // output line was properly formatted
        out = Strings::ascii_to_lowercase(Strings::replace_all(mac_address, "-", ":"));
        return true;
    }

    std::string get_user_mac_hash()
    {
#if defined(_WIN32)
        // getmac /V /NH /FO CSV
        // outputs each interface on its own comma-separated line
        // "connection name","network adapter","physical address","transport name"
        auto getmac = cmd_execute_and_capture_output(
            Command("getmac").string_arg("/V").string_arg("/NH").string_arg("/FO").string_arg("CSV"));
        if (getmac.exit_code == 0)
        {
            for (auto&& line : Strings::split(getmac.output, '\n'))
            {
                std::string mac;
                if (extract_mac_from_getmac_output_line(line, mac) && is_valid_mac_for_telemetry(mac))
                {
                    return Hash::get_string_hash(mac, Hash::Algorithm::Sha256);
                }
            }
        }
#elif defined(__linux__) || defined(__APPLE__)
        // The getifaddrs(ifaddrs** ifap) function creates a linked list of structures
        // describing the network interfaces of the local system, and stores
        // the address of the first item of the list in *ifap.
        // man page: https://www.man7.org/linux/man-pages/man3/getifaddrs.3.html
        struct ifaddrs_guard
        {
            ifaddrs* ptr = nullptr;
            ~ifaddrs_guard()
            {
                if (ptr) freeifaddrs(ptr);
            }
        } interfaces;

        if (getifaddrs(&interfaces.ptr) < 0)
        {
            return "0";
        }

        unsigned char bytes[MAC_BYTES_LENGTH];
        for (auto interface = interfaces.ptr; interface; interface = interface->ifa_next)
        {
            // The ifa_addr field points to a structure containing the interface
            // address.  (The sa_family subfield should be consulted to
            // determine the format of the address structure.)  This field may
            // contain a null pointer.
            if (interface->ifa_addr && interface->ifa_addr->sa_family == AF_TYPE)
            {
                auto name = std::string(interface->ifa_name);
                if (interface->ifa_flags & IFF_LOOPBACK) continue;

                // Convert the generic sockaddr into a specified representation
                // based on the value of sa_family, on macOS the AF_PACKET
                // family is not available so we fall back to AF_LINK.
                // AF_PACKET and sockaddr_ll: https://man7.org/linux/man-pages/man7/packet.7.html
                // AF_LINK and sockaddr_dl: https://illumos.org/man/3SOCKET/sockaddr_dl
#if defined(__linux__)
                auto address = reinterpret_cast<sockaddr_ll*>(interface->ifa_addr);
                if (address->sll_halen != MAC_BYTES_LENGTH) continue;
                std::memcpy(bytes, address->sll_addr, MAC_BYTES_LENGTH);
#elif defined(__APPLE__)
                auto address = reinterpret_cast<sockaddr_dl*>(interface->ifa_addr);
                if (address->sdl_alen != MAC_BYTES_LENGTH) continue;
                // The macro LLADDR() returns the start of the link-layer network address.
                std::memcpy(bytes, LLADDR(address), MAC_BYTES_LENGTH);
#endif
                auto maybe_mac = mac_bytes_to_string(Span<unsigned char>(bytes, MAC_BYTES_LENGTH));
                if (is_valid_mac_for_telemetry(maybe_mac))
                {
                    return Hash::get_string_hash(maybe_mac, Hash::Algorithm::Sha256);
                }
            }
        }
#else
        // fallback when getifaddrs() is not available
        struct socket_guard
        {
            int fd = -1;
            ~socket_guard()
            {
                if (fd > 0) close(fd);
            }
            inline operator int() { return fd; }
        } fd{socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)};

        if (fd == -1) return "0";

        ifconf interfaces;
        std::memset(&interfaces, 0, sizeof(ifconf));
        // Calling ioctil with ifconf.ifc_req set to null returns
        // the size of buffer needed to contain all interfaces
        // in ifconf.ifc_len.
        // https://www.man7.org/linux/man-pages/man7/netdevice.7.html
        if (ioctl(fd, SIOCGIFCONF, &interfaces) < 0) return "0";

        auto data = std::vector<char>(interfaces.ifc_len);
        interfaces.ifc_len = data.size();
        interfaces.ifc_buf = data.data();
        if (ioctl(fd, SIOCGIFCONF, &interfaces) < 0) return "0";

        // On a successful call, ifc_req contains a pointer to an array
        // of ifreq structures filled with all currently active interface addresses.
        // Within each structure ifr_name will receive the interface name, and
        // ifr_addr the addres.
        const ifreq* const end = interfaces.ifc_req + (interfaces.ifc_len / sizeof(ifreq));
        unsigned char bytes[MAC_BYTES_LENGTH];
        for (auto it = interfaces.ifc_req; it != end; ++it)
        {
            ifreq& interface = *it;

            // Retrieve interface hardware addresses (ignore loopback)
            if ((ioctl(fd, SIOCGIFFLAGS, &interface) >= 0) && !(interface.ifr_flags & IFF_LOOPBACK) &&
                (ioctl(fd, SIOCGIFHWADDR, &interface) >= 0))
            {
                std::memcpy(bytes, interface.ifr_hwaddr.sa_data, MAC_BYTES_LENGTH);
                auto maybe_mac = mac_bytes_to_string(Span<unsigned char>(bytes, MAC_BYTES_LENGTH));
                if (is_valid_mac_for_telemetry(maybe_mac))
                {
                    return Hash::get_string_hash(maybe_mac, Hash::Algorithm::Sha256);
                }
            }
        }
#endif
        return "0";
    }
}
