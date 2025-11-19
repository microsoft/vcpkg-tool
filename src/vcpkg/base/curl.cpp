#include <vcpkg/base/checks.h>
#include <vcpkg/base/curl.h>

#include <array>
#include <string>

#if defined(__linux__)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstdlib>

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

#if defined(__linux__)
    struct CurlCaBundle
    {
        std::string ca_file;
        std::string ca_path;
        bool initialized = false;
    };

    bool path_exists(const char* path, bool require_directory)
    {
        struct stat st;
        if (stat(path, &st) != 0)
        {
            return false;
        }

        if (!require_directory)
        {
            return S_ISREG(st.st_mode) || S_ISLNK(st.st_mode);
        }

        return S_ISDIR(st.st_mode);
    }

    CurlCaBundle& get_global_curl_ca_bundle()
    {
        static CurlCaBundle bundle;
        if (bundle.initialized)
        {
            return bundle;
        }

        bundle.initialized = true;

        const char* ssl_ca_file = std::getenv("SSL_CERT_FILE");
        const char* ssl_ca_dir = std::getenv("SSL_CERT_DIR");

        if (ssl_ca_file && *ssl_ca_file)
        {
            bundle.ca_file = ssl_ca_file;
        }

        if (ssl_ca_dir && *ssl_ca_dir)
        {
            bundle.ca_path = ssl_ca_dir;
        }

        // If env vars didn't provide values, probe common Linux locations,
        // largely based on Go's crypto/x509 package.
        if (bundle.ca_file.empty())
        {
            constexpr std::array<const char*, 6> cert_files = {
                "/etc/ssl/certs/ca-certificates.crt",                // Debian/Ubuntu/Gentoo etc.
                "/etc/pki/tls/certs/ca-bundle.crt",                  // Fedora/RHEL 6
                "/etc/ssl/ca-bundle.pem",                            // OpenSUSE
                "/etc/pki/tls/cacert.pem",                           // OpenELEC
                "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", // CentOS/RHEL 7
                "/etc/ssl/cert.pem",                                 // Alpine Linux
            };

            for (const auto* f : cert_files)
            {
                if (path_exists(f, false))
                {
                    bundle.ca_file = f;
                    break;
                }
            }
        }

        if (bundle.ca_path.empty())
        {
            constexpr std::array<const char*, 2> cert_dirs = {
                "/etc/ssl/certs",     // SLES10/SLES11
                "/etc/pki/tls/certs", // Fedora/RHEL
            };

            for (const auto* d : cert_dirs)
            {
                if (path_exists(d, true))
                {
                    bundle.ca_path = d;
                    break;
                }
            }
        }

        return bundle;
    }
#endif
}

namespace vcpkg
{
    CURLcode get_curl_global_init_status() noexcept
    {
        static CurlGlobalInit g_curl_global_init;
        return g_curl_global_init.get_init_status();
    }

    void curl_set_system_ssl_root_certs(CURL* curl)
    {
#if defined(__linux__)
        if (!curl)
        {
            return;
        }

        CurlCaBundle& bundle = get_global_curl_ca_bundle();
        if (!bundle.ca_file.empty())
        {
            curl_easy_setopt(curl, CURLOPT_CAINFO, bundle.ca_file.c_str());
        }

        if (!bundle.ca_path.empty())
        {
            curl_easy_setopt(curl, CURLOPT_CAPATH, bundle.ca_path.c_str());
        }
#else
        (void)curl;
#endif
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

    CurlHeaders::CurlHeaders(View<std::string> headers)
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
