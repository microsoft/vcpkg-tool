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

#if !defined(_WIN32)
namespace
{
    using namespace vcpkg;

    constexpr size_t MAC_BYTES_LENGTH = 6;

    std::string mac_bytes_to_string(const Span<unsigned char>& bytes)
    {
        static constexpr char capital_hexits[] = "0123456789ABCDEF";
        // 6 hex bytes, 5 dashes
        static constexpr size_t MAC_STRING_LENGTH = MAC_BYTES_LENGTH * 2 + 5;

        char mac_address[MAC_STRING_LENGTH];
        char* mac = mac_address;
        unsigned char c = static_cast<unsigned char>(bytes[0]);
        *mac++ = capital_hexits[(c & 0xf0) >> 4];
        *mac++ = capital_hexits[(c & 0x0f)];
        unsigned char non_zero_mac = c;
        for (size_t i = 1; i < MAC_BYTES_LENGTH; ++i)
        {
            c = static_cast<unsigned char>(bytes[i]);
            *mac++ = '-';
            *mac++ = capital_hexits[(c & 0xf0) >> 4];
            *mac++ = capital_hexits[(c & 0x0f)];
            non_zero_mac |= c;
        }

        return non_zero_mac ? std::string(mac_address, MAC_STRING_LENGTH) : "";
    }

    std::string preferred_interface_or_default(const std::map<StringView, std::string>& ifname_mac_map)
    {
        // search for preferred interfaces
        for (auto&& interface_name : {"eth0", "wlan0", "en0", "hn0"})
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

        return "0";
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
        for (auto&& interface_name : {"Local Area Connection", "Ethernet", "Wi-Fi"})
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
        unsigned char bytes[MAC_BYTES_LENGTH];
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

                // Convert the generic sockaddr into a specified representation
                // based on the value of sa_family, on macOS the AF_PACKET
                // family is not available so we fall back to AF_LINK.
                // AF_PACKET and sockaddr_ll: https://man7.org/linux/man-pages/man7/packet.7.html
                // AF_LINK and sockaddr_dl: https://illumos.org/man/3SOCKET/sockaddr_dl
                std::memset(bytes, 0, MAC_BYTES_LENGTH);
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
                if (maybe_mac.empty()) continue;
                ifname_mac_map.emplace(StringView{interface->ifa_name, strlen(interface->ifa_name)},
                                       Hash::get_string_hash(maybe_mac, Hash::Algorithm::Sha256));
            }
        }
        return preferred_interface_or_default(ifname_mac_map);
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

        std::map<StringView, std::string> ifname_mac_map;
        // On a successful call, ifc_req contains a pointer to an array
        // of ifreq structures filled with all currently active interface addresses.
        // Within each structure ifr_name will receive the interface name, and
        // ifr_addr the addres.
        const ifreq* const end = interfaces.ifc_req + (interfaces.ifc_len / sizeof(ifreq));
        unsigned char bytes[MAC_BYTES_LENGTH];
        for (auto it = interfaces.ifc_req; it != end; ++it)
        {
            ifreq interface;
            std::memset(&interface, 0, sizeof(ifreq));
            auto&& name = StringView(it->ifr_name, strlen(it->ifr_name));
            std::memcpy(interface.ifr_name, name.data(), name.size());

            // Retrieve interface hardware addresses (ignore loopback)
            if ((ioctl(fd, SIOCGIFFLAGS, &interface) >= 0) && !(interface.ifr_flags & IFF_LOOPBACK) &&
                (ioctl(fd, SIOCGIFHWADDR, &interface) >= 0))
            {
                std::memset(bytes, 0, MAC_BYTES_LENGTH);
                std::memcpy(bytes, interface.ifr_hwaddr.sa_data, MAC_BYTES_LENGTH);
                auto&& mac = mac_bytes_to_string(Span<unsigned char>(bytes, MAC_BYTES_LENGTH));
                ifname_mac_map.emplace(name, Hash::get_string_hash(mac, Hash::Algorithm::Sha256));
            }
        }
        return preferred_interface_or_default(ifname_mac_map);
#endif
        return "0";
    }
}