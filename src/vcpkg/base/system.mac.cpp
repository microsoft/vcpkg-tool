#include <vcpkg/base/hash.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.mac.h>
#include <vcpkg/base/system.process.h>

#include <map>

#if defined(__linux__) || defined(__APPLE__)
#include <ifaddrs.h>

#include <net/if.h>

#if defined(AF_PACKET)
#include <netpacket/packet.h>
#define AF_TYPE AF_PACKET
#elif defined(AF_LINK)
#include <net/if_dl.h>
#define AF_TYPE AF_LINK
#endif
#endif

#if defined(__linux__) || defined(__APPLE__)
namespace
{
    using namespace vcpkg;

    std::string mac_bytes_to_string(const Span<unsigned char>& bytes)
    {
        static constexpr char capital_hexits[] = "0123456789ABCDEF";
        static constexpr size_t mac_size = 6 * 2 + 5; // 6 hex bytes, 5 dashes

        char mac_address[mac_size];
        char* mac = mac_address;
        unsigned char c = static_cast<unsigned char>(bytes[0]);
        *mac++ = capital_hexits[(c & 0xf0) >> 4];
        *mac++ = capital_hexits[(c & 0x0f)];
        unsigned char non_zero_mac = c;
        for (int i = 1; i < 6; ++i)
        {
            c = static_cast<unsigned char>(bytes[i]);
            *mac++ = '-';
            *mac++ = capital_hexits[(c & 0xf0) >> 4];
            *mac++ = capital_hexits[(c & 0x0f)];
            non_zero_mac |= c;
        }

        return non_zero_mac ? std::string(mac_address, mac_size) : "";
    }
}
#endif

namespace vcpkg
{
    std::string get_user_mac_hash()
    {
#if defined(_WIN32)
        // getmac /V /NH /FO CSV
        // outputs each interface on its own comma-separated line
        // "connection name","network adapter","physical address","transport name"
        static constexpr StringLiteral ZERO_MAC = "00-00-00-00-00-00";
        static constexpr size_t CONNECTION_NAME = 0;
        static constexpr size_t PHYSICAL_ADDRESS = 2;
        auto getmac = cmd_execute_and_capture_output(
            Command("getmac").string_arg("/V").string_arg("/NH").string_arg("/FO").string_arg("CSV"));
        if (getmac.exit_code != 0)
        {
            return "0";
        }

        std::map<std::string, std::string> ifname_mac_map;
        for (auto&& line : Strings::split(getmac.output, '\n'))
        {
            auto values = Strings::split(line, ',');
            if (values.size() != 4)
            {
                return "0";
            }

            auto&& name = Strings::replace_all(values[CONNECTION_NAME], "\"", "");
            auto&& mac = Strings::replace_all(values[PHYSICAL_ADDRESS], "\"", "");
            if (mac == ZERO_MAC) continue;
            ifname_mac_map.emplace(name, Hash::get_string_hash(mac, Hash::Algorithm::Sha256));
        }

        // search for preferred interfaces
        for (auto&& interface_name : {"Local Area Connection", "Wi-Fi"})
        {
            auto maybe_preferred = ifname_mac_map.find(interface_name);
            if (maybe_preferred != ifname_mac_map.end())
            {
                return maybe_preferred->second;
            }
        }

        // default to first mac address we find
        auto first = ifname_mac_map.begin();
        if (first != ifname_mac_map.end())
        {
            return first->second;
        }
#elif defined(__linux__) || defined(__APPLE__)
        // The getifaddrs(ifaddrs** ifap) function creates a linked list of structures
        // describing the network interfaces of the local system, and stores
        // the address of the first item of the list in *ifap.
        // man page: https://www.man7.org/linux/man-pages/man3/getifaddrs.3.html
        static constexpr size_t MAC_BYTES_LENGTH = 6;
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

        std::map<StringView, std::string> ifname_mac_map;
        for (auto interface = interfaces.ptr; interface; interface = interface->ifa_next)
        {
            // The ifa_addr field points to a structure containing the interface
            // address.  (The sa_family subfield should be consulted to
            // determine the format of the address structure.)  This field may
            // contain a null pointer.
            if (interface->ifa_addr && interface->ifa_addr->sa_family == AF_TYPE)
            {
                auto name = std::string(interface->ifa_name, strlen(interface->ifa_name));
                if (interface->ifa_flags & IFF_LOOPBACK) continue;

                unsigned char bytes[6];
                // Convert the generic sockaddr into a specified representation
                // based on the value of sa_family, on macOS the AF_PACKET
                // family is not available so we fall back to AF_LINK.
                // AF_SOCKET and sockaddr_ll: https://man7.org/linux/man-pages/man7/packet.7.html
                // AF_LINK and sockaddr_dl: https://illumos.org/man/3SOCKET/sockaddr_dl
#if defined(AF_PACKET)
                auto address = reinterpret_cast<sockaddr_ll*>(interface->ifa_addr);
                if (address->sll_halen != MAC_BYTES_LENGTH) continue;
                std::memcpy(bytes, address->sll_addr, MAC_BYTES_LENGTH);
#elif defined(AF_LINK)
                auto address = reinterpret_cast<sockaddr_dl*>(interface->ifa_addr);
                if (address->sdl_alen != MAC_BYTES_LENGTH) continue;
                // The macro LLADDR() returns the start of the link-layer network address.
                std::memcpy(bytes, LLADDR(address), MAC_BYTES_LENGTH);
#endif
                auto maybe_mac = mac_bytes_to_string(Span<unsigned char>(bytes, MAC_BYTES_LENGTH));
                if (maybe_mac.empty()) continue;
                ifname_mac_map.emplace(StringView{interface->ifa_name, strlen(interface->ifa_name)},
                                       Hash::get_string_hash(maybe_mac, Hash::Algorithm::Sha256));
            }
        }

        // search for preferred interfaces
        for (auto&& interface_name : {"eth0", "wlan0", "en0"})
        {
            auto maybe_preferred = ifname_mac_map.find(interface_name);
            if (maybe_preferred != ifname_mac_map.end())
            {
                return maybe_preferred->second;
            }
        }

        // default to first mac address we find
        auto first = ifname_mac_map.begin();
        if (first != ifname_mac_map.end())
        {
            return first->second;
        }
#endif
        return "0";
    }
}