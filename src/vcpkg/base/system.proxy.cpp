#include <vcpkg/base/system.proxy.h>

vcpkg::Optional<vcpkg::System::IEProxySetting> vcpkg::System::get_windows_ie_proxy_server()
{
#if defined(_WIN32)
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieProxy;
    if (WinHttpGetIEProxyConfigForCurrentUser(&ieProxy) && ieProxy.lpszProxy != nullptr)
    {
        vcpkg::System::IEProxySetting ieProxySetting;

        ieProxySetting.server = ieProxy.lpszProxy;

        if (ieProxy.lpszProxyBypass != nullptr) ieProxySetting.bypass = ieProxy.lpszProxyBypass;

        GlobalFree(ieProxy.lpszProxy);
        GlobalFree(ieProxy.lpszProxyBypass);
        GlobalFree(ieProxy.lpszAutoConfigUrl);

        return ieProxySetting;
    }
    return vcpkg::Optional<vcpkg::System::IEProxySetting>();
#else
    return vcpkg::Optional<vcpkg::System::IEProxySetting>();
#endif
}