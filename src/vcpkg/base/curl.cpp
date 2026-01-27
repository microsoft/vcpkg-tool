#include <vcpkg/base/checks.h>
#include <vcpkg/base/curl.h>

#ifdef VCPKG_LIBCURL_DLSYM
#include <dlfcn.h>

// these are filled in by the first call to vcpkg_curl_global_init
decltype(&curl_easy_cleanup) vcpkg_curl_easy_cleanup;
decltype(&curl_easy_getinfo) vcpkg_curl_easy_getinfo;
decltype(&curl_easy_init) vcpkg_curl_easy_init;
decltype(&curl_easy_perform) vcpkg_curl_easy_perform;
decltype(&curl_easy_setopt) vcpkg_curl_easy_setopt;
decltype(&curl_easy_strerror) vcpkg_curl_easy_strerror;

decltype(&curl_multi_add_handle) vcpkg_curl_multi_add_handle;
decltype(&curl_multi_cleanup) vcpkg_curl_multi_cleanup;
decltype(&curl_multi_info_read) vcpkg_curl_multi_info_read;
decltype(&curl_multi_init) vcpkg_curl_multi_init;
decltype(&curl_multi_remove_handle) vcpkg_curl_multi_remove_handle;
decltype(&curl_multi_strerror) vcpkg_curl_multi_strerror;
decltype(&curl_multi_perform) vcpkg_curl_multi_perform;
decltype(&curl_multi_wait) vcpkg_curl_multi_poll; // or _wait, if _poll is not present

decltype(&curl_slist_append) vcpkg_curl_slist_append;
decltype(&curl_slist_free_all) vcpkg_curl_slist_free_all;

decltype(&curl_version) vcpkg_curl_version;

template<typename FnT>
static void load_symbol(FnT (&target), void* handle, const char* symbol_name) {
    target = reinterpret_cast<FnT>(dlsym(handle, symbol_name));
    if (!target) {
        vcpkg::Checks::unreachable(VCPKG_LINE_INFO);
    }
}

    void vcpkg_curl_global_init(long flags) {
                // calling dlclose() on the handle after calling curl_version() causes asan to
        // report a false leak, so we intentionally don't unload the library
        auto handle = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
        if (!handle)
        {
            // Ubuntu 16.04 has this if the user only installs `curl`
            handle = dlopen("libcurl-gnutls.so.4", RTLD_NOW | RTLD_LOCAL);
        }
        if (!handle)
        {
            // It's possible that someone explicitly installs this one
            handle = dlopen("libcurl-nss.so.4", RTLD_NOW | RTLD_LOCAL);
        }

        if (!handle) {
            vcpkg::Checks::unreachable(VCPKG_LINE_INFO); // FIXME emit an error for users
        }

        {
            decltype(&curl_global_init) global_init;
            load_symbol(global_init, handle, "curl_global_init");
            global_init(flags);
        }

        load_symbol(vcpkg_curl_easy_cleanup, handle, "curl_easy_cleanup");
        load_symbol(vcpkg_curl_easy_getinfo, handle, "curl_easy_getinfo");
        load_symbol(vcpkg_curl_easy_init, handle, "curl_easy_init");
        load_symbol(vcpkg_curl_easy_perform, handle, "curl_easy_perform");
        load_symbol(vcpkg_curl_easy_setopt, handle, "curl_easy_setopt");
        load_symbol(vcpkg_curl_easy_strerror, handle, "curl_easy_strerror");

        load_symbol(vcpkg_curl_multi_add_handle, handle, "curl_multi_add_handle");
        load_symbol(vcpkg_curl_multi_cleanup, handle, "curl_multi_cleanup");
        load_symbol(vcpkg_curl_multi_info_read, handle, "curl_multi_info_read");
        load_symbol(vcpkg_curl_multi_init, handle, "curl_multi_init");
        load_symbol(vcpkg_curl_multi_remove_handle, handle, "curl_multi_remove_handle");
        load_symbol(vcpkg_curl_multi_strerror, handle, "curl_multi_strerror");
        load_symbol(vcpkg_curl_multi_perform, handle, "curl_multi_perform");
        // try to load curl_multi_poll first, fall back to curl_multi_wait
        vcpkg_curl_multi_poll = reinterpret_cast<decltype(&curl_multi_wait)>(dlsym(handle, "curl_multi_poll"));
        if (!vcpkg_curl_multi_poll) {
            load_symbol(vcpkg_curl_multi_poll, handle, "curl_multi_wait");
        }

        load_symbol(vcpkg_curl_slist_append, handle, "curl_slist_append");
        load_symbol(vcpkg_curl_slist_free_all, handle, "curl_slist_free_all");

        load_symbol(vcpkg_curl_version, handle, "curl_version");
    }
#endif // ^^^ VCPKG_LIBCURL_DLSYM

namespace vcpkg
{
    CurlEasyHandle::CurlEasyHandle(CurlEasyHandle&& other) noexcept : m_ptr(std::exchange(other.m_ptr, nullptr)) { }
    CurlEasyHandle& CurlEasyHandle::operator=(CurlEasyHandle&& other) noexcept
    {
        m_ptr = std::exchange(other.m_ptr, nullptr);
        return *this;
    }
    CurlEasyHandle::~CurlEasyHandle()
    {
        if (m_ptr)
        {
            vcpkg_curl_easy_cleanup(m_ptr);
        }
    }
    CURL* CurlEasyHandle::get()
    {
        if (!m_ptr)
        {
            m_ptr = vcpkg_curl_easy_init();
            if (!m_ptr)
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }
        }
        return m_ptr;
    }

    CurlMultiHandle::CurlMultiHandle() = default;
    CurlMultiHandle::CurlMultiHandle(CurlMultiHandle&& other) noexcept
        : m_ptr(std::exchange(other.m_ptr, nullptr)), m_easy_handles(std::move(other.m_easy_handles))
    {
    }
    CurlMultiHandle& CurlMultiHandle::operator=(CurlMultiHandle&& other) noexcept
    {
        m_ptr = std::exchange(other.m_ptr, nullptr);
        m_easy_handles = std::move(other.m_easy_handles);
        return *this;
    }
    CurlMultiHandle::~CurlMultiHandle()
    {
        for (auto* easy_handle : m_easy_handles)
        {
            vcpkg_curl_multi_remove_handle(m_ptr, easy_handle);
        }

        if (m_ptr)
        {
            vcpkg_curl_multi_cleanup(m_ptr);
        }
    }
    void CurlMultiHandle::add_easy_handle(CurlEasyHandle& easy_handle)
    {
        auto* handle = easy_handle.get();
        if (vcpkg_curl_multi_add_handle(this->get(), handle) == CURLM_OK)
        {
            m_easy_handles.push_back(handle);
        }
    }
    CURLM* CurlMultiHandle::get()
    {
        if (!m_ptr)
        {
            m_ptr = vcpkg_curl_multi_init();
            if (!m_ptr)
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }
        }
        return m_ptr;
    }

    CurlHeaders::CurlHeaders(View<std::string> headers)
    {
        for (const auto& header : headers)
        {
            m_headers = vcpkg_curl_slist_append(m_headers, header.c_str());
        }
    }
    CurlHeaders::CurlHeaders(CurlHeaders&& other) noexcept : m_headers(std::exchange(other.m_headers, nullptr)) { }
    CurlHeaders& CurlHeaders::operator=(CurlHeaders&& other) noexcept
    {
        m_headers = std::exchange(other.m_headers, nullptr);
        return *this;
    }
    CurlHeaders::~CurlHeaders()
    {
        if (m_headers)
        {
            vcpkg_curl_slist_free_all(m_headers);
        }
    }
    curl_slist* CurlHeaders::get() const { return m_headers; }
}
