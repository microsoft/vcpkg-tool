#include <vcpkg/base/system.proxy.h>

std::optional<std::string> vcpkg::System::get_windows_ie_proxy_server()
{
#if defined(_WIN32)
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieProxy;
    if (WinHttpGetIEProxyConfigForCurrentUser(&ieProxy) && ieProxy.lpszProxy != nullptr)
    {
        // Cast wstring to string, as proxy typically doesn't contain non-ascii characters.
        // So just mute C4244 for once.
        std::wstring w_proxy(ieProxy.lpszProxy);
#pragma warning(disable : 4244)
        std::string proxy = std::string(w_proxy.begin(), w_proxy.end());
#pragma warning(default : 4244)

        GlobalFree(ieProxy.lpszProxy);
        GlobalFree(ieProxy.lpszProxyBypass);
        GlobalFree(ieProxy.lpszAutoConfigUrl);

        return proxy;
    }
    return nullptr;
#else
    return nullptr;
#endif
}

bool vcpkg::System::get_windows_ie_proxy_enabled()
{
#if defined(_WIN32)
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieProxy;
    if (WinHttpGetIEProxyConfigForCurrentUser(&ieProxy) && ieProxy.lpszProxy != nullptr)
    {
        GlobalFree(ieProxy.lpszProxy);
        GlobalFree(ieProxy.lpszProxyBypass);
        GlobalFree(ieProxy.lpszAutoConfigUrl);
        return true;
    }
    return false;
#else
    return false;
#endif
}