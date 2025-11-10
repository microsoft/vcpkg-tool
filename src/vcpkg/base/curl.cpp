#include <vcpkg/base/checks.h>
#include <vcpkg/base/curl.h>

namespace
{
    struct CurlGlobalInit
    {
        CurlGlobalInit() : init_status(curl_global_init(CURL_GLOBAL_DEFAULT)) { }
        ~CurlGlobalInit() { curl_global_cleanup(); }

        CurlGlobalInit(const CurlGlobalInit&) = delete;
        CurlGlobalInit(CurlGlobalInit&&) = delete;
        CurlGlobalInit& operator=(const CurlGlobalInit&) = delete;
        CurlGlobalInit& operator=(CurlGlobalInit&&) = delete;

        CURLcode get_init_status() const { return init_status; }

    private:
        CURLcode init_status;
    };
}

namespace vcpkg
{
    CURLcode get_curl_global_init_status() noexcept
    {
        static CurlGlobalInit g_curl_global_init;
        return g_curl_global_init.get_init_status();
    }

    CurlEasyHandle::CurlEasyHandle() { get_curl_global_init_status(); }
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
            curl_easy_cleanup(m_ptr);
        }
    }
    CURL* CurlEasyHandle::get()
    {
        if (!m_ptr)
        {
            m_ptr = curl_easy_init();
            if (!m_ptr)
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }
        }
        return m_ptr;
    }

    CurlMultiHandle::CurlMultiHandle() { get_curl_global_init_status(); }
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
            curl_multi_remove_handle(m_ptr, easy_handle);
        }

        if (m_ptr)
        {
            curl_multi_cleanup(m_ptr);
        }
    }
    void CurlMultiHandle::add_easy_handle(CurlEasyHandle& easy_handle)
    {
        auto* handle = easy_handle.get();
        if (curl_multi_add_handle(this->get(), handle) == CURLM_OK)
        {
            m_easy_handles.push_back(handle);
        }
    }
    CURLM* CurlMultiHandle::get()
    {
        if (!m_ptr)
        {
            m_ptr = curl_multi_init();
            if (!m_ptr)
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }
        }
        return m_ptr;
    }

    CurlHeaders::CurlHeaders(View<std::string> headers) : m_headers(nullptr)
    {
        for (const auto& header : headers)
        {
            m_headers = curl_slist_append(m_headers, header.c_str());
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
            curl_slist_free_all(m_headers);
        }
    }
    curl_slist* CurlHeaders::get() const { return m_headers; }
}
