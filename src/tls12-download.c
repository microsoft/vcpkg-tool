#pragma warning(push)
// warning C28251: Inconsistent annotation for '_setjmp': this instance has no annotations. See <no file>(0).
#pragma warning(disable : 28251)
// warning C28301: No annotations for first declaration of '__fastfail'. See <no file>(0).
#pragma warning(disable : 28301)
#include <Windows.h>
#include <process.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <Softpub.h>
#pragma warning(pop)

/*
 * This program must be as small as possible, because it is committed in binary form to the
 * vcpkg github repo to enable downloading the main vcpkg program on Windows 7, where TLS 1.2 is
 * unavailable to PowerShell.
 * To that end it avoids using C runtime functions (beyond the vcruntime ones the compiler
 * injects itself).
 * (In testing as of 2021-01-07, this version that doesn't link with the CRT is ~8kb, whereas a
 * hello world program that does link with the CRT is ~300kb)
 */

static void __declspec(noreturn) win32_abort()
{
    /*
     * Note that TerminateProcess does not return when called from the terminated process, see
     * https://github.com/MicrosoftDocs/sdk-api/pull/626
     */
    TerminateProcess(GetCurrentProcess(), 3);
}

static size_t wide_length(const wchar_t* str)
{
    size_t answer = 0;
    while (*str)
    {
        ++answer;
        ++str;
    }
    return answer;
}

static void write_message(const HANDLE std_out, const wchar_t* msg)
{
    size_t wchars_to_write = wide_length(msg);
    if (wchars_to_write == 0)
    {
        return;
    }

    if (wchars_to_write > 65535)
    {
        win32_abort();
    }

    if (WriteConsoleW(std_out, msg, (DWORD)wchars_to_write, 0, 0))
    {
        return;
    }

    // this happens if output has been redirected
    int narrow_chars = WideCharToMultiByte(CP_ACP, 0, msg, (int)wchars_to_write, 0, 0, 0, 0);
    if (narrow_chars == 0)
    {
        win32_abort();
    }

    char* narrow_buffer = HeapAlloc(GetProcessHeap(), 0, (size_t)narrow_chars);
    if (WideCharToMultiByte(CP_ACP, 0, msg, (int)wchars_to_write, narrow_buffer, narrow_chars, 0, 0) == 0)
    {
        win32_abort();
    }

    while (narrow_chars != 0)
    {
        DWORD chars_written;
        if (!WriteFile(std_out, narrow_buffer, (DWORD)narrow_chars, &chars_written, 0))
        {
            win32_abort();
        }

        narrow_chars -= (int)chars_written;
    }

    if (!HeapFree(GetProcessHeap(), 0, narrow_buffer))
    {
        win32_abort();
    }
}

static void write_number(const HANDLE std_out, DWORD number)
{
    wchar_t buffer[11]; // 4294967295\0
    wchar_t* first_digit = buffer + 11;
    *--first_digit = L'\0';
    if (number == 0)
    {
        *--first_digit = L'0';
    }
    else
    {
        do
        {
            *--first_digit = L'0' + number % 10;
            number /= 10;
        } while (number != 0);
    }

    write_message(std_out, first_digit);
}

static void write_hex(const HANDLE std_out, DWORD number)
{
    wchar_t buffer[] = L"0x00000000";
    wchar_t* first_digit = buffer + (sizeof(buffer) / sizeof(wchar_t)) - 1;
    while (number != 0)
    {
        *--first_digit = L"0123456789ABCDEF"[number % 16];
        number /= 16;
    }

    write_message(std_out, buffer);
}

static void __declspec(noreturn) abort_api_failure_with_last_error(const HANDLE std_out,
                                                                   const wchar_t* api_name,
                                                                   DWORD last_error)
{
    write_message(std_out, L"While calling Windows API function ");
    write_message(std_out, api_name);
    write_message(std_out, L" got error ");
    write_hex(std_out, last_error);
    write_message(std_out, L":\r\n");
    wchar_t* message;
    if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                       GetModuleHandleW(L"winhttp.dll"),
                       last_error,
                       0,
                       (LPWSTR)&message,
                       0,
                       0))
    {
        write_message(std_out, message);
        // intentionally leaks the message buffer
    }
    else
    {
        last_error = GetLastError();
        write_message(std_out, L"(unknown error, FormatMessageW failed with ");
        write_hex(std_out, last_error);
        write_message(std_out, L")");
    }

    write_message(std_out, L"\r\n");
    FlushFileBuffers(std_out);
    win32_abort();
}
static void __declspec(noreturn) abort_api_failure(const HANDLE std_out, const wchar_t* api_name)
{
    abort_api_failure_with_last_error(std_out, api_name, GetLastError());
}

static void set_delete_on_close_flag(const HANDLE std_out, const HANDLE target, BOOLEAN setting)
{
    FILE_DISPOSITION_INFO fdi = {0};
    fdi.DeleteFile = setting;
    if (SetFileInformationByHandle(target, FileDispositionInfo, &fdi, sizeof(fdi)) == 0)
    {
        abort_api_failure(std_out, L"SetFileInformationByHandle");
    }
}

static BOOL bytes_equal(const BYTE* lhs, const BYTE* rhs, DWORD count)
{
    for (DWORD i = 0; i < count; ++i)
    {
        if (lhs[i] != rhs[i])
        {
            return FALSE;
        }
    }

    return TRUE;
}

// Allowed Microsoft Code Signing intermediate certificate SHA-256 thumbprints:
// - Microsoft Code Signing PCA 2024 (CN=Microsoft Code Signing PCA 2024, O=Microsoft Corporation, C=US)
//   3DADFAF812DD1BBAEF45834CCBD188F3CD97139E2ED1ACA69C2DD63082142F8F
// - Microsoft Code Signing PCA 2011 (CN=Microsoft Code Signing PCA 2011, O=Microsoft Corporation, L=Redmond,
//   S=Washington, C=US)
//   56DA8722AFD94066FFE1E4595473A4854892B843A0827D53FB7D8F4AEED1E18B
static const BYTE allowed_microsoft_code_signing_intermediates[][32] = {
    {0x3D, 0xAD, 0xFA, 0xF8, 0x12, 0xDD, 0x1B, 0xBA, 0xEF, 0x45, 0x83, 0x4C, 0xCB, 0xD1, 0x88, 0xF3,
     0xCD, 0x97, 0x13, 0x9E, 0x2E, 0xD1, 0xAC, 0xA6, 0x9C, 0x2D, 0xD6, 0x30, 0x82, 0x14, 0x2F, 0x8F},
    {0x56, 0xDA, 0x87, 0x22, 0xAF, 0xD9, 0x40, 0x66, 0xFF, 0xE1, 0xE4, 0x59, 0x54, 0x73, 0xA4, 0x85,
     0x48, 0x92, 0xB8, 0x43, 0xA0, 0x82, 0x7D, 0x53, 0xFB, 0x7D, 0x8F, 0x4A, 0xEE, 0xD1, 0xE1, 0x8B},
};

static BOOL is_chained_to_microsoft_code_signing_cert(HANDLE state_data)
{
    CRYPT_PROVIDER_DATA* prov_data = WTHelperProvDataFromStateData(state_data);
    if (!prov_data)
    {
        return FALSE;
    }

    CRYPT_PROVIDER_SGNR* signer = WTHelperGetProvSignerFromChain(prov_data, 0, FALSE, 0);
    if (!signer || !signer->pChainContext)
    {
        return FALSE;
    }

    CERT_CHAIN_POLICY_PARA policy_para = {0};
    policy_para.cbSize = sizeof(policy_para);
    policy_para.dwFlags = MICROSOFT_ROOT_CERT_CHAIN_POLICY_CHECK_APPLICATION_ROOT_FLAG;

    CERT_CHAIN_POLICY_STATUS policy_status = {0};
    policy_status.cbSize = sizeof(policy_status);

    if (!CertVerifyCertificateChainPolicy(
            CERT_CHAIN_POLICY_MICROSOFT_ROOT, signer->pChainContext, &policy_para, &policy_status) ||
        policy_status.dwError != 0)
    {
        return FALSE;
    }

    for (DWORD i = 1; i < signer->csCertChain; ++i)
    {
        CRYPT_PROVIDER_CERT* prov_cert = WTHelperGetProvCertFromChain(signer, i);
        if (!prov_cert || !prov_cert->pCert)
        {
            continue;
        }

        BYTE sha256_hash[32];
        DWORD hash_size = sizeof(sha256_hash);
        if (!CryptHashCertificate(0,
                                  CALG_SHA_256,
                                  0,
                                  prov_cert->pCert->pbCertEncoded,
                                  prov_cert->pCert->cbCertEncoded,
                                  sha256_hash,
                                  &hash_size) ||
            hash_size != sizeof(sha256_hash))
        {
            continue;
        }

        for (size_t j = 0; j < sizeof(allowed_microsoft_code_signing_intermediates) /
                                   sizeof(allowed_microsoft_code_signing_intermediates[0]);
             ++j)
        {
            if (bytes_equal(sha256_hash, allowed_microsoft_code_signing_intermediates[j], sizeof(sha256_hash)))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

// these are sucked out to avoid
// warning C6262: Function uses '98624' bytes of stack.  Consider moving some data to heap.
static wchar_t https_proxy_env[32768];
static wchar_t no_proxy_env[32768];
static char buffer[32768];

#ifndef NDEBUG
int main()
#else  // ^^^ debug // !debug vvv
int __stdcall entry()
#endif // ^^^ !debug
{
#ifdef NDEBUG
    __security_init_cookie();
#endif // ^^^ release

    const HANDLE std_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (std_out == INVALID_HANDLE_VALUE)
    {
        win32_abort();
    }

    int argc;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc); // intentionally leaks argv
    if (argv == 0)
    {
        win32_abort();
    }

    if (argc != 4)
    {
        write_message(std_out, L"Usage: tls12-download.exe DOMAIN RELATIVE-PATH OUT-FILE\r\n");
        return 1;
    }

    const wchar_t* const domain = argv[1];
    const wchar_t* const relative_path = argv[2];
    const wchar_t* const out_file_path = argv[3];
    write_message(std_out, L"Downloading https://");
    write_message(std_out, domain);
    write_message(std_out, relative_path);
    write_message(std_out, L" -> ");
    write_message(std_out, out_file_path);

    DWORD access_type;
    const wchar_t* proxy_setting;
    SetLastError(ERROR_SUCCESS);
    DWORD get_result =
        GetEnvironmentVariableW(L"HTTPS_PROXY", https_proxy_env, sizeof(https_proxy_env) / sizeof(wchar_t));
    DWORD last_error = GetLastError();
    if (get_result)
    {
        access_type = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        proxy_setting = https_proxy_env;
        write_message(std_out, L" (using proxy: ");
        write_message(std_out, proxy_setting);
        write_message(std_out, L")");
    }
    else if (last_error == ERROR_ENVVAR_NOT_FOUND || last_error == ERROR_SUCCESS)
    {
        access_type = WINHTTP_ACCESS_TYPE_NO_PROXY;
        proxy_setting = WINHTTP_NO_PROXY_NAME;
    }
    else
    {
        abort_api_failure_with_last_error(std_out, L"GetEnvironmentVariableW", last_error);
    }

    const wchar_t* proxy_bypass_setting;
    SetLastError(ERROR_SUCCESS);
    get_result = GetEnvironmentVariableW(L"NO_PROXY", no_proxy_env, sizeof(no_proxy_env) / sizeof(wchar_t));
    last_error = GetLastError();
    if (get_result)
    {
        proxy_bypass_setting = no_proxy_env;
        write_message(std_out, L" (using proxy bypass: ");
        write_message(std_out, no_proxy_env);
        write_message(std_out, L")");
    }
    else if (last_error == ERROR_ENVVAR_NOT_FOUND || last_error == ERROR_SUCCESS)
    {
        proxy_bypass_setting = WINHTTP_NO_PROXY_BYPASS;
    }
    else
    {
        abort_api_failure_with_last_error(std_out, L"GetEnvironmentVariableW", last_error);
    }

    const HANDLE out_file = CreateFileW(out_file_path,                             // lpFileName
                                        FILE_WRITE_DATA | FILE_READ_DATA | DELETE, // dwDesiredAccess
                                        0,                                         // dwShareMode
                                        0,                                         // lpSecurityAttributes
                                        CREATE_ALWAYS,                             // dwCreationDisposition
                                        FILE_ATTRIBUTE_NORMAL,                     // dwFlagsAndAttributes
                                        0                                          // hTemplateFile
    );

    if (out_file == INVALID_HANDLE_VALUE)
    {
        abort_api_failure(std_out, L"CreateFileW");
    }

    // Setting delete on close before we do anything means the file will get deleted for us if we crash
    set_delete_on_close_flag(std_out, out_file, TRUE);

    HINTERNET session = WinHttpOpen(L"tls12-download/1.0", access_type, proxy_setting, proxy_bypass_setting, 0);
    if (!session)
    {
        last_error = GetLastError();
        if (last_error != ERROR_INVALID_PARAMETER)
        {
            abort_api_failure_with_last_error(std_out, L"WinHttpOpen", last_error);
        }

        write_message(std_out,
                      L"\r\nwarning: WinHttpOpen rejected HTTPS_PROXY/NO_PROXY (error 87). "
                      L"Falling back to Windows/IE proxy settings, or a direct connection if no explicit proxy is "
                      L"configured.\r\n");
        access_type = WINHTTP_ACCESS_TYPE_NO_PROXY;
        session = WinHttpOpen(L"tls12-download/1.0", access_type, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }

    if (!session)
    {
        abort_api_failure(std_out, L"WinHttpOpen");
    }

    // If HTTPS_PROXY not found, try use IE Proxy. This works on Windows 10 20H2, so there's no need to determine
    // the OS version >= 8.1.
    if (access_type == WINHTTP_ACCESS_TYPE_NO_PROXY)
    {
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieProxy;
        if (WinHttpGetIEProxyConfigForCurrentUser(&ieProxy) && ieProxy.lpszProxy != NULL)
        {
            WINHTTP_PROXY_INFO proxy;
            proxy.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
            proxy.lpszProxy = ieProxy.lpszProxy;
            proxy.lpszProxyBypass = ieProxy.lpszProxyBypass;
            WinHttpSetOption(session, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy));

            write_message(std_out, L" (using IE proxy: ");
            write_message(std_out, proxy.lpszProxy);
            write_message(std_out, L")");

            GlobalFree(ieProxy.lpszProxy);
            GlobalFree(ieProxy.lpszProxyBypass);
            GlobalFree(ieProxy.lpszAutoConfigUrl);
        }
    }

    write_message(std_out, L"...");

    unsigned long secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &secure_protocols, sizeof(DWORD)))
    {
        abort_api_failure(std_out, L"WinHttpSetOption");
    }

    const HINTERNET connect = WinHttpConnect(session, domain, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect)
    {
        abort_api_failure(std_out, L"WinHttpConnect");
    }

    const HINTERNET request = WinHttpOpenRequest(
        connect, L"GET", relative_path, 0, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request)
    {
        abort_api_failure(std_out, L"WinHttpOpenRequest");
    }

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        abort_api_failure(std_out, L"WinHttpSendRequest");
    }

    if (!WinHttpReceiveResponse(request, 0))
    {
        abort_api_failure(std_out, L"WinHttpReceiveResponse");
    }

    DWORD http_code = 0;
    DWORD query_headers_buffer_size = sizeof(http_code);
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &http_code,
                             &query_headers_buffer_size,
                             WINHTTP_NO_HEADER_INDEX))
    {
        abort_api_failure(std_out, L"WinHttpQueryHeaders");
    }

    if (http_code != 200)
    {
        write_message(std_out, L"Download failed, server returned HTTP status: ");
        write_number(std_out, http_code);
        write_message(std_out, L"\r\n");
        FlushFileBuffers(std_out);
        TerminateProcess(GetCurrentProcess(), 2);
    }

    for (;;)
    {
        DWORD received_bytes;
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &received_bytes))
        {
            abort_api_failure(std_out, L"WinHttpReadData");
        }

        if (received_bytes == 0)
        {
            break; // end of response
        }

        do
        {
            DWORD written_bytes;
            if (!WriteFile(out_file, buffer, received_bytes, &written_bytes, 0))
            {
                abort_api_failure(std_out, L"WriteFile");
            }

            received_bytes -= written_bytes;
        } while (received_bytes != 0);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    write_message(std_out, L" done.\r\nValidating signature...");

    if (SetFilePointer(out_file, 0, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    {
        abort_api_failure(std_out, L"SetFilePointer");
    }

    DWORD exit_code = 0;

    WINTRUST_FILE_INFO wtfi = {0};
    wtfi.cbStruct = sizeof(wtfi);
    wtfi.pcwszFilePath = out_file_path;
    wtfi.hFile = out_file;
    wtfi.pgKnownSubject = 0;

    // CERT_STRONG_SIGN_PARA + WINTRUST_SIGNATURE_SETTINGS tell WinVerifyTrust that only SHA-2 certificates are
    // acceptable.
    CERT_STRONG_SIGN_PARA cssp = {0};
    cssp.cbSize = sizeof(cssp);
    cssp.dwInfoChoice = CERT_STRONG_SIGN_OID_INFO_CHOICE;
    cssp.pszOID = szOID_CERT_STRONG_SIGN_OS_1;

    WINTRUST_SIGNATURE_SETTINGS wtss = {0};
    wtss.cbStruct = sizeof(wtss);
    wtss.pCryptoPolicy = &cssp;

    WINTRUST_DATA wtd = {0};
    wtd.cbStruct = sizeof(wtd);
    wtd.dwUIChoice = WTD_UI_NONE;
    wtd.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    wtd.dwUnionChoice = WTD_CHOICE_FILE;
    wtd.pFile = &wtfi;
    wtd.dwStateAction = WTD_STATEACTION_VERIFY;
    wtd.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN;
    wtd.pSignatureSettings = &wtss;

    GUID wt_policy_guid = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    LONG trust_validation_result = WinVerifyTrust(INVALID_HANDLE_VALUE, &wt_policy_guid, &wtd);
    BOOL is_microsoft_signed = FALSE;
    if (trust_validation_result == 0)
    {
        is_microsoft_signed = is_chained_to_microsoft_code_signing_cert(wtd.hWVTStateData);
    }

    wtd.dwStateAction = WTD_STATEACTION_CLOSE;
    (void)WinVerifyTrust(INVALID_HANDLE_VALUE, &wt_policy_guid, &wtd);
    if (trust_validation_result == 0 && is_microsoft_signed)
    {
        set_delete_on_close_flag(std_out, out_file, FALSE);
        write_message(std_out, L" done.\r\n");
    }
    else
    {
        exit_code = 1;
        write_message(std_out, L" failed! ");
        if (trust_validation_result != 0)
        {
            write_hex(std_out, (DWORD)trust_validation_result);
        }
        else
        {
            write_message(std_out, L"Not signed by a Microsoft code signing certificate.");
        }

        write_message(std_out, L" Deleted!\r\n");
    }

    CloseHandle(out_file);
    FlushFileBuffers(std_out);
    TerminateProcess(GetCurrentProcess(), exit_code);
    return (int)exit_code;
}
