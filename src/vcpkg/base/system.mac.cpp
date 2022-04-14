#include <vcpkg/base/hash.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.mac.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/metrics.h>

#if defined(__linux__)
#include <unistd.h>

#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
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

    std::string get_user_mac()
    {
        if (!LockGuardPtr<Metrics>(g_metrics)->metrics_enabled())
        {
            return "{}";
        }
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
        struct locked_socket
        {
            int fd;
            ~locked_socket() { ::close(fd); }
            operator int() { return fd; }
        } fd{socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)};

        if (fd < 0)
        {
            return "0";
        }

        ifconf all_interfaces;
        auto data = std::vector<char>(1024);
        all_interfaces.ifc_len = data.size();
        all_interfaces.ifc_buf = data.data();
        if (ioctl(fd, SIOCGIFCONF, &all_interfaces) < 0) return 0;

        std::map<StringView, std::string> interface_mac_map;
        const auto end = all_interfaces.ifc_req + (all_interfaces.ifc_len / sizeof(ifreq));
        for (auto it = all_interfaces.ifc_req; it != end; ++it)
        {
            auto name = StringView(it->ifr_name);

            ifreq interface;
            std::memset(&interface, 0, sizeof(ifreq));
            std::strcpy(interface.ifr_name, name.data());

            if (ioctl(fd, SIOCGIFFLAGS, &interface) >= 0)
            {
                if (interface.ifr_flags & IFF_LOOPBACK) continue;
            }

            std::string mac_address;
            if (ioctl(fd, SIOCGIFHWADDR, &interface) >= 0)
            {
                unsigned char non_zero_mac = 0;
                const auto bytes = Span<char>(interface.ifr_hwaddr.sa_data, 6);
                for (size_t i = 0; i < bytes.size(); ++i)
                {
                    if (i != 0)
                    {
                        mac_address += "-";
                    }
                    static constexpr auto hex_digits = "0123456789ABCDEF";
                    const auto c = static_cast<unsigned char>(bytes[i]);
                    mac_address += hex_digits[(c & 0xf0) >> 4];
                    mac_address += hex_digits[(c & 0x0f)];
                    non_zero_mac |= c;
                }
                if (non_zero_mac)
                {
                    interface_mac_map[name] = Hash::get_string_hash(mac_address, Hash::Algorithm::Sha256);
                }
            }
        }

        // search for preferred interfaces
        for (auto&& interface_name : {"eth0", "wlan0"})
        {
            auto maybe_preferred = interface_mac_map.find(interface_name);
            if (maybe_preferred != interface_mac_map.end())
            {
                return maybe_preferred->second;
            }
        }

        // default to first mac address we find
        auto first = interface_mac_map.begin();
        if (first != interface_mac_map.end())
        {
            return first->second;
        }
#endif
        return "0";
    }
}