#include <vcpkg/base/hash.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.mac.h>
#include <vcpkg/base/system.process.h>

#include <map>

#if defined(__linux__) || defined(__APPLE__)
#include <ifaddrs.h>

#include <sys/socket.h>

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
    Optional<StringView> find_first_nonzero_mac(StringView sv)
    {
        static constexpr StringLiteral ZERO_MAC = "00-00-00-00-00-00";

        auto first = sv.begin();
        const auto last = sv.end();

        while (first != last)
        {
            // XX-XX-XX-XX-XX-XX
            // 1  2  3  4  5  6
            // size = 6 * 2 + 5 = 17
            first = std::find_if(first, last, ParserBase::is_hex_digit);
            if (last - first < 17)
            {
                break;
            }

            bool is_first = true;
            bool is_valid = true;
            auto end_of_mac = first;
            for (int i = 0; is_valid && i < 6; ++i)
            {
                if (!is_first)
                {
                    if (*end_of_mac != '-')
                    {
                        is_valid = false;
                        break;
                    }
                    ++end_of_mac;
                }
                is_first = false;

                if (!ParserBase::is_hex_digit(*end_of_mac))
                {
                    is_valid = false;
                    break;
                }
                ++end_of_mac;

                if (!ParserBase::is_hex_digit(*end_of_mac))
                {
                    is_valid = false;
                    break;
                }
                ++end_of_mac;
            }
            if (is_valid && StringView{first, end_of_mac} != ZERO_MAC)
            {
                return StringView{first, end_of_mac};
            }
            else
            {
                first = end_of_mac;
            }
        }

        return nullopt;
    }

    std::string get_user_mac_hash()
    {
#if defined(_WIN32)
        auto getmac = cmd_execute_and_capture_output(Command("getmac"));
        if (getmac.exit_code != 0)
        {
            return "0";
        }

        auto found_mac = find_first_nonzero_mac(getmac.output);
        if (auto p = found_mac.get())
        {
            return Hash::get_string_hash(*p, Hash::Algorithm::Sha256);
        }
#elif defined(__linux__) || defined(__APPLE__)
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
            if (interface->ifa_addr && interface->ifa_addr->sa_family == AF_TYPE)
            {
                std::vector<unsigned char> bytes;
#if defined(AF_PACKET)
                auto address = reinterpret_cast<sockaddr_ll*>(interface->ifa_addr);
                auto begin = address->ssl_addr;
#elif defined(AF_LINK)
                auto address = reinterpret_cast<sockaddr_dl*>(interface->ifa_addr);
                auto begin = reinterpret_cast<unsigned char*>(LLADDR(address));
#endif
                bytes.assign(begin, begin + 6);
                auto maybe_mac = mac_bytes_to_string(bytes);
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