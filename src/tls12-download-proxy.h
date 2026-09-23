#pragma once

static int proxy_bypass_separator(wchar_t ch)
{
    return ch == L',' || ch == L';' || ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

// Keep WinHTTP's matching rules, including matching redirected hosts. Only discard entries
// that this version of WinHTTP rejects, rather than maintaining a second proxy grammar.
static BOOL set_proxy_bypass(
    HINTERNET session, const wchar_t* proxy_name, wchar_t* bypass, void (*warn)(const wchar_t*, void*), void* context)
{
    WINHTTP_PROXY_INFO proxy;
    proxy.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    proxy.lpszProxy = (wchar_t*)proxy_name;
    proxy.lpszProxyBypass = *bypass ? bypass : WINHTTP_NO_PROXY_BYPASS;
    if (WinHttpSetOption(session, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy)))
    {
        return TRUE;
    }
    if (GetLastError() != ERROR_INVALID_PARAMETER)
    {
        return FALSE;
    }

    proxy.lpszProxyBypass = WINHTTP_NO_PROXY_BYPASS;
    // Establish that an invalid parameter below refers to the bypass entry, not the proxy itself.
    if (!WinHttpSetOption(session, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy)))
    {
        return FALSE;
    }

    wchar_t* read = bypass;
    wchar_t* write = bypass;
    while (*read)
    {
        while (proxy_bypass_separator(*read))
        {
            ++read;
        }

        if (!*read)
        {
            break;
        }

        wchar_t* entry = read;
        while (*read && !proxy_bypass_separator(*read))
        {
            ++read;
        }

        if (*read)
        {
            *read++ = L'\0';
        }

        proxy.lpszProxyBypass = entry;
        if (!WinHttpSetOption(session, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy)))
        {
            if (GetLastError() != ERROR_INVALID_PARAMETER)
            {
                return FALSE;
            }

            warn(entry, context);
            continue;
        }

        if (write != bypass)
        {
            *write++ = L';';
        }

        while (*entry)
        {
            *write++ = *entry++;
        }
    }

    *write = L'\0';
    proxy.lpszProxyBypass = write == bypass ? WINHTTP_NO_PROXY_BYPASS : bypass;
    return WinHttpSetOption(session, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy));
}
