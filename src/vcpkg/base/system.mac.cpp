#include <vcpkg/base/hash.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.mac.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/metrics.h>

#if defined(__linux__)
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
#elif defined(__linux__)
        auto fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (fd == -1)
        {
            return "0";
        }

        auto data = std::vector<char>(1024);

        ifconf ifc;
        ifc.ifc_len = data.size();
        ifc.ifc_buf = data.data();
        if (ioctl(fd, SIOCGIFCONF, &ifc) == -1)
        {
            return "0";
        }

        const auto end = ifc.ifc_req + (ifc.ifc_len / sizeof(ifreq));
        for (auto it = ifc.ifc_req; it != end; ++it)
        {
            ifreq request;
            std::strcpy(request.ifr_name, it->ifr_name);
            if (ioctl(fd, SIOCGIFFLAGS, &request) != -1)
            {
                if (!(request.ifr_flags & IFF_LOOPBACK))
                {
                    if (ioctl(fd, SIOCGIFHWADDR, &request) != -1)
                    {
                        const auto bytes = request.ifr_hwaddr.sa_data;
                        std::string mac_address;
                        for (int i = 0; i < 6; ++i)
                        {
                            if (i != 0)
                            {
                                mac_address += "-";
                            }
                            static char digits[] = "0123456789ABCDEF";
                            const auto c = static_cast<unsigned char>(bytes[i]);
                            mac_address += digits[(c & 0xf0) >> 4];
                            mac_address += digits[(c & 0x0f)];
                        }
                        return Hash::get_string_hash(mac_address, Hash::Algorithm::Sha256);
                    }
                }
            }
        }
#endif
        return "0";
    }
}