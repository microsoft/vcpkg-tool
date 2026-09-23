#if defined(_WIN32)
#include <vcpkg/base/system-headers.h>

#include <catch2/catch.hpp>

#include <winhttp.h>
#include <winsock2.h>

#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
    unsigned int proxy_option_calls = 0;

    BOOL count_proxy_option(HINTERNET handle, DWORD option, LPVOID buffer, DWORD size)
    {
        ++proxy_option_calls;
        return WinHttpSetOption(handle, option, buffer, size);
    }
}

#define WinHttpSetOption count_proxy_option
#include "../tls12-download-proxy.h"
#undef WinHttpSetOption

namespace
{
    struct InternetHandle
    {
        HINTERNET value;
        explicit InternetHandle(HINTERNET value_) : value(value_) { REQUIRE(value != nullptr); }
        ~InternetHandle() { WinHttpCloseHandle(value); }
        InternetHandle(const InternetHandle&) = delete;
        InternetHandle& operator=(const InternetHandle&) = delete;
    };

    void collect_warning(const wchar_t* entry, void* context)
    {
        static_cast<std::vector<std::wstring>*>(context)->emplace_back(entry);
    }

    struct ProxySession
    {
        InternetHandle handle;
        std::wstring bypass;
        std::vector<std::wstring> warnings;

        ProxySession(const std::wstring& proxy, std::wstring bypass_)
            : handle(WinHttpOpen(L"tls12-download-test", WINHTTP_ACCESS_TYPE_NAMED_PROXY, proxy.c_str(), nullptr, 0))
            , bypass(std::move(bypass_))
        {
            REQUIRE(set_proxy_bypass(handle.value, proxy.c_str(), bypass.data(), collect_warning, &warnings));
            bypass.resize(wcslen(bypass.c_str()));
            REQUIRE(WinHttpSetTimeouts(handle.value, 2000, 2000, 2000, 2000));
        }
    };

    struct Winsock
    {
        Winsock()
        {
            WSADATA data;
            REQUIRE(WSAStartup(MAKEWORD(2, 2), &data) == 0);
        }
        ~Winsock() { WSACleanup(); }
    };

    struct Socket
    {
        SOCKET value;
        explicit Socket(SOCKET value_) : value(value_) { }
        ~Socket()
        {
            if (value != INVALID_SOCKET) closesocket(value);
        }
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;
    };

    // Replies locally even when acting as a proxy; never forwards requests to the Internet.
    struct HttpServer
    {
        Winsock winsock;
        Socket listener{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        unsigned short port;
        std::atomic<unsigned int> hits{0};
        std::atomic<bool> healthy{true};
        std::atomic<bool> stop{false};
        std::thread worker;

        explicit HttpServer(std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
        {
            REQUIRE(listener.value != INVALID_SOCKET);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            REQUIRE(bind(listener.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
            int size = sizeof(address);
            REQUIRE(getsockname(listener.value, reinterpret_cast<sockaddr*>(&address), &size) == 0);
            port = ntohs(address.sin_port);
            REQUIRE(listen(listener.value, SOMAXCONN) == 0);
            worker = std::thread([this, response = std::move(response)] {
                while (!stop)
                {
                    fd_set readable;
                    FD_ZERO(&readable);
                    FD_SET(listener.value, &readable);
                    timeval timeout{0, 100000};
                    int ready = select(0, &readable, nullptr, nullptr, &timeout);
                    if (ready == 0) continue;
                    if (ready == SOCKET_ERROR)
                    {
                        healthy = false;
                        return;
                    }

                    Socket client{accept(listener.value, nullptr, nullptr)};
                    DWORD receive_timeout = 2000;
                    if (client.value == INVALID_SOCKET || setsockopt(client.value,
                                                                     SOL_SOCKET,
                                                                     SO_RCVTIMEO,
                                                                     reinterpret_cast<const char*>(&receive_timeout),
                                                                     sizeof(receive_timeout)) != 0)
                    {
                        healthy = false;
                        return;
                    }

                    std::string request;
                    while (request.find("\r\n\r\n") == std::string::npos)
                    {
                        char buffer[1024];
                        int received = recv(client.value, buffer, sizeof(buffer), 0);
                        if (received <= 0 || request.size() > 8192)
                        {
                            healthy = false;
                            return;
                        }
                        request.append(buffer, static_cast<size_t>(received));
                    }

                    ++hits;
                    size_t sent = 0;
                    while (sent != response.size())
                    {
                        int result =
                            send(client.value, response.data() + sent, static_cast<int>(response.size() - sent), 0);
                        if (result <= 0)
                        {
                            healthy = false;
                            return;
                        }
                        sent += static_cast<size_t>(result);
                    }
                }
            });
        }

        ~HttpServer()
        {
            stop = true;
            worker.join();
        }

        std::wstring proxy() const { return L"127.0.0.1:" + std::to_wstring(port); }
    };

    DWORD request(ProxySession& session, const wchar_t* host, INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT)
    {
        InternetHandle connection{WinHttpConnect(session.handle.value, host, port, 0)};
        InternetHandle request{
            WinHttpOpenRequest(connection.value, L"GET", L"/", nullptr, nullptr, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)};
        if (!WinHttpSendRequest(request.value, nullptr, 0, nullptr, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr))
        {
            return GetLastError();
        }

        DWORD status = 0;
        DWORD size = sizeof(status);
        REQUIRE(WinHttpQueryHeaders(
            request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr));
        REQUIRE(status == 200);
        return ERROR_SUCCESS;
    }

#if defined(VCPKG_TEST_TLS12_DOWNLOAD)
    struct Handle
    {
        HANDLE value;
        explicit Handle(HANDLE value_) : value(value_) { }
        ~Handle()
        {
            if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
    };

    std::string run_bootstrap(const std::optional<std::wstring>& proxy,
                              const std::optional<std::wstring>& bypass,
                              bool stop_before_network,
                              DWORD expected_exit = 3)
    {
        std::vector<wchar_t> path(32768);
        REQUIRE(GetModuleFileNameW(nullptr, path.data(), 32768));
        std::wstring helper = path.data();
        helper.resize(helper.find_last_of(L"\\") + 1);
        helper += L"tls12-download.exe";

        std::wstring environment;
        if (proxy) environment += L"HTTPS_PROXY=" + *proxy + L'\0';
        if (bypass) environment += L"NO_PROXY=" + *bypass + L'\0';
        REQUIRE(GetWindowsDirectoryW(path.data(), 32768));
        environment += L"SystemRoot=" + std::wstring(path.data()) + L'\0';
        environment += L'\0';

        REQUIRE(GetTempPathW(32768, path.data()));
        wchar_t temporary[MAX_PATH];
        REQUIRE(GetTempFileNameW(path.data(), L"vcp", 0, temporary));
        // Using the executable as a directory fails after the environment reads, before networking.
        std::wstring output = stop_before_network ? helper + L"\\invalid" : temporary;
        std::wstring command = L"\"" + helper + L"\" account.blob.core.windows.invalid / \"" + output + L"\"";
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE read_pipe;
        HANDLE write_pipe;
        REQUIRE(CreatePipe(&read_pipe, &write_pipe, &security, 0));
        Handle read{read_pipe};
        Handle write{write_pipe};
        REQUIRE(SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0));
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = write.value;
        startup.hStdError = write.value;
        PROCESS_INFORMATION process{};
        REQUIRE(CreateProcessW(helper.c_str(),
                               command.data(),
                               nullptr,
                               nullptr,
                               TRUE,
                               CREATE_UNICODE_ENVIRONMENT,
                               environment.data(),
                               nullptr,
                               &startup,
                               &process));
        Handle child{process.hProcess};
        Handle thread{process.hThread};
        CloseHandle(write.value);
        write.value = nullptr;
        std::string result;
        DWORD read_error = ERROR_SUCCESS;
        std::thread reader([&] {
            char buffer[1024];
            DWORD received;
            while (ReadFile(read.value, buffer, sizeof(buffer), &received, nullptr) && received)
            {
                result.append(buffer, received);
            }
            read_error = GetLastError();
        });
        DWORD wait = WaitForSingleObject(child.value, 15000);
        if (wait != WAIT_OBJECT_0)
        {
            TerminateProcess(child.value, 3);
            WaitForSingleObject(child.value, INFINITE);
        }
        reader.join();
        CAPTURE(result);
        // Successful downloads would retain the file; all of these failure cases must delete it.
        if (stop_before_network)
        {
            REQUIRE(DeleteFileW(temporary));
        }
        else
        {
            CHECK(GetFileAttributesW(temporary) == INVALID_FILE_ATTRIBUTES);
        }
        REQUIRE(wait == WAIT_OBJECT_0);
        DWORD exit_code;
        REQUIRE(GetExitCodeProcess(child.value, &exit_code));
        CHECK(exit_code == expected_exit);
        CHECK(read_error == ERROR_BROKEN_PIPE);
        return result;
    }
#endif
}

TEST_CASE ("bootstrap proxy bypass accepts valid lists in one call", "[tls12-download]")
{
    const wchar_t* cases[] = {
        L"",
        L"localhost,127.0.0.1;*.example.invalid",
        L"*;<local>;[::1];example.invalid:443",
        L".example.invalid,example.invalid",
        L"localhost,, ;localhost;",
    };
    for (const auto* input : cases)
    {
        CAPTURE(std::wstring(input));
        auto before = proxy_option_calls;
        ProxySession session{L"127.0.0.1:1", input};
        CHECK(proxy_option_calls == before + 1);
        CHECK(session.bypass == input);
        CHECK(session.warnings.empty());
    }
}

TEST_CASE ("bootstrap proxy bypass filters rejected lists", "[tls12-download]")
{
    struct Case
    {
        const wchar_t* input;
        const wchar_t* expected;
        std::vector<std::wstring> warnings;
    };
    const Case cases[] = {
        {L" ,;\t\r\n::1", L"", {L"::1"}},
        {L"localhost,127.0.0.1;*.example.invalid,::1", L"localhost;127.0.0.1;*.example.invalid", {L"::1"}},
        {L" ::1,localhost,192.0.2.0/24;127.0.0.1 ", L"localhost;127.0.0.1", {L"::1", L"192.0.2.0/24"}},
        {L"localhost;::1", L"localhost", {L"::1"}},
        {L"::1;192.0.2.0/24", L"", {L"::1", L"192.0.2.0/24"}},
        {L"*;<local>;[::1];example.invalid:443;::1", L"*;<local>;[::1];example.invalid:443", {L"::1"}},
        {L".example.invalid,example.invalid,::1", L".example.invalid;example.invalid", {L"::1"}},
        {L"localhost,, ;localhost;::1", L"localhost;localhost", {L"::1"}},
    };
    for (const auto& test : cases)
    {
        CAPTURE(std::wstring(test.input));
        ProxySession session{L"127.0.0.1:1", test.input};
        CHECK(session.bypass == test.expected);
        CHECK(session.warnings == test.warnings);
        WINHTTP_PROXY_INFO actual{};
        DWORD size = sizeof(actual);
        REQUIRE(WinHttpQueryOption(session.handle.value, WINHTTP_OPTION_PROXY, &actual, &size));
        CHECK(actual.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY);
        CHECK(std::wstring(actual.lpszProxy) == L"127.0.0.1:1");
        CHECK((actual.lpszProxyBypass ? std::wstring(actual.lpszProxyBypass) : L"") == test.expected);
        GlobalFree(actual.lpszProxy);
        GlobalFree(actual.lpszProxyBypass);
    }
}

TEST_CASE ("bootstrap proxy bypass routing", "[tls12-download]")
{
    HttpServer proxy;
    struct Case
    {
        const wchar_t* bypass;
        bool direct;
    };
    const Case cases[] = {
        {L"", false},
        {L"::1,192.0.2.0/24", false},
        {L"unrelated.invalid,::1", false},
        {L"blob.core.windows.invalid", false},
        {L".blob.core.windows.invalid", false},
        {L"localhost,account.blob.core.windows.invalid", true},
        {L"localhost;account.blob.core.windows.invalid", true},
        {L"*.blob.core.windows.invalid", true},
        {L"localhost,account.blob.core.windows.invalid,::1", true},
        {L"localhost;account.blob.core.windows.invalid;192.0.2.0/24", true},
        {L"*.blob.core.windows.invalid,::1", true},
        {L"*", true},
    };
    for (const auto& test : cases)
    {
        ProxySession session{proxy.proxy(), test.bypass};
        auto before = proxy.hits.load();
        CHECK(request(session, L"account.blob.core.windows.invalid") ==
              static_cast<DWORD>(test.direct ? ERROR_WINHTTP_NAME_NOT_RESOLVED : ERROR_SUCCESS));
        CHECK(proxy.hits.load() == before + (test.direct ? 0 : 1));
        CHECK(proxy.healthy.load());
    }
}

TEST_CASE ("bootstrap proxy bypass handles a maximum length environment value", "[tls12-download]")
{
    std::wstring input = L"::1,";
    std::wstring expected;
    for (int i = 0; i != 3276; ++i)
    {
        input += L"localhost;";
        if (i) expected += L';';
        expected += L"localhost";
    }
    input += L"::1";
    REQUIRE(input.size() == 32767);
    ProxySession session{L"127.0.0.1:1", input};
    CHECK(session.bypass == expected);
    CHECK(session.warnings == std::vector<std::wstring>{L"::1", L"::1"});
}

TEST_CASE ("bootstrap proxy bypass does not swallow other WinHTTP errors", "[tls12-download]")
{
    InternetHandle session{WinHttpOpen(L"tls12-download-test", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0)};
    InternetHandle connection{WinHttpConnect(session.value, L"localhost", INTERNET_DEFAULT_HTTP_PORT, 0)};
    wchar_t bypass[] = L"localhost,::1";
    std::vector<std::wstring> warnings;
    auto before = proxy_option_calls;
    BOOL result = set_proxy_bypass(connection.value, L"127.0.0.1:1", bypass, collect_warning, &warnings);
    DWORD error = GetLastError();
    CHECK_FALSE(result);
    CHECK(error == ERROR_WINHTTP_INCORRECT_HANDLE_TYPE);
    CHECK(warnings.empty());
    CHECK(proxy_option_calls == before + 1);
    CHECK(std::wstring(bypass) == L"localhost,::1");
}

TEST_CASE ("bootstrap proxy bypass does not misreport invalid proxy settings", "[tls12-download]")
{
    InternetHandle session{WinHttpOpen(L"tls12-download-test", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0)};
    wchar_t bypass[] = L"localhost,::1";
    std::vector<std::wstring> warnings;
    BOOL result = set_proxy_bypass(session.value, nullptr, bypass, collect_warning, &warnings);
    DWORD error = GetLastError();
    CHECK_FALSE(result);
    CHECK(error == ERROR_INVALID_PARAMETER);
    CHECK(warnings.empty());
    CHECK(std::wstring(bypass) == L"localhost,::1");
}

TEST_CASE ("bootstrap proxy bypass is reevaluated on redirects", "[tls12-download]")
{
    bool filter = GENERATE(false, true);
    SECTION ("proxied to bypassed")
    {
        HttpServer proxy{"HTTP/1.1 302 Found\r\nLocation: http://bypassed.invalid/\r\nContent-Length: 0\r\nConnection: "
                         "close\r\n\r\n"};
        ProxySession session{proxy.proxy(), filter ? L"bypassed.invalid,::1" : L"bypassed.invalid"};
        CHECK(request(session, L"original.invalid") == ERROR_WINHTTP_NAME_NOT_RESOLVED);
        CHECK(proxy.hits.load() == 1);
        CHECK(proxy.healthy.load());
    }
    SECTION ("bypassed to proxied")
    {
        HttpServer origin{"HTTP/1.1 302 Found\r\nLocation: http://redirected.invalid/\r\nContent-Length: "
                          "0\r\nConnection: close\r\n\r\n"};
        HttpServer proxy;
        ProxySession session{proxy.proxy(), filter ? L"127.0.0.1,192.0.2.0/24" : L"127.0.0.1"};
        CHECK(request(session, L"127.0.0.1", origin.port) == ERROR_SUCCESS);
        CHECK(origin.hits.load() == 1);
        CHECK(proxy.hits.load() == 1);
        CHECK(origin.healthy.load());
        CHECK(proxy.healthy.load());
    }
}

#if defined(VCPKG_TEST_TLS12_DOWNLOAD)
TEST_CASE ("bootstrap empty and unset proxy environment", "[tls12-download]")
{
    const std::optional<std::wstring> proxies[] = {std::nullopt, L"", L"http://127.0.0.1:1"};
    const std::optional<std::wstring> bypasses[] = {std::nullopt, L""};
    for (const auto& proxy : proxies)
    {
        for (const auto& bypass : bypasses)
        {
            auto output = run_bootstrap(proxy, bypass, true);
            CAPTURE(output);
            CHECK(output.find("GetEnvironmentVariableW") == std::string::npos);
            CHECK(output.find("CreateFileW") != std::string::npos);
            CHECK(output.find("using proxy bypass:") == std::string::npos);
            CHECK((output.find("using proxy:") != std::string::npos) == (proxy && !proxy->empty()));
        }
    }
}

TEST_CASE ("bootstrap executable warns and preserves HTTPS proxy routing", "[tls12-download]")
{
    bool filter = GENERATE(false, true);
    HttpServer proxy{"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};
    for (bool direct : {false, true})
    {
        auto before = proxy.hits.load();
        std::wstring bypass = direct ? L"account.blob.core.windows.invalid" : L"unrelated.invalid";
        if (filter) bypass = L"::1," + bypass + L",192.0.2.0/24";
        auto output = run_bootstrap(L"http://" + proxy.proxy(), bypass, false, direct ? 3 : 2);
        CAPTURE(output);
        CHECK((output.find("Skipping NO_PROXY entry \"::1\"") != std::string::npos) == filter);
        CHECK((output.find("Skipping NO_PROXY entry \"192.0.2.0/24\"") != std::string::npos) == filter);
        CHECK(output.find("WinHttpOpen") == std::string::npos);
        CHECK(output.find(direct ? "WinHttpSendRequest" : "HTTP status: 502") != std::string::npos);
        CHECK(proxy.hits.load() == before + (direct ? 0 : 1));
        CHECK(proxy.healthy.load());
    }
}
#endif
#endif
