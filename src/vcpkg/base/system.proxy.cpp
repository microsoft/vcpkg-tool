#include <vcpkg/base/system.proxy.h>
std::optional<std::string> vcpkg::System::get_windows_proxy_server()
{
#if defined(_WIN32)
    HKEY proxy_reg;
    DWORD buf_len = 0;

    auto ret =
        RegOpenKey(HKEY_CURRENT_USER, R"(Software\Microsoft\Windows\CurrentVersion\Internet Settings)", &proxy_reg);
    if (ret)
        return nullptr;

    ret = RegGetValue(proxy_reg, nullptr, "ProxyServer", RRF_RT_REG_SZ, nullptr, nullptr, &buf_len);
    if (ret)
        return nullptr;

    std::string srv(buf_len, '\0');
    ret = RegGetValue(proxy_reg, nullptr, "ProxyServer", RRF_RT_REG_SZ, nullptr, srv.data(), &buf_len);
    if (ret)
        return nullptr;

    return srv;
#else
    return nullptr;
#endif
}
bool vcpkg::System::get_windows_proxy_enabled()
{
#if defined(_WIN32)
    HKEY proxy_reg;
    DWORD enable = 0;
    DWORD len = 4;

    auto ret =
        RegOpenKey(HKEY_CURRENT_USER, R"(Software\Microsoft\Windows\CurrentVersion\Internet Settings)", &proxy_reg);

    ret = RegGetValue(proxy_reg, nullptr, "ProxyEnable", RRF_RT_REG_DWORD, nullptr, &enable, &len);

    return enable;
#else
    return false;
#endif
}