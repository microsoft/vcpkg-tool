#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>

namespace vcpkg
{
    static std::time_t get_current_time_as_time_since_epoch()
    {
        using std::chrono::system_clock;
        return system_clock::to_time_t(system_clock::now());
    }

    static std::time_t utc_mktime(tm* time_ptr)
    {
#if defined(_WIN32)
        return _mkgmtime(time_ptr);
#else
        return timegm(time_ptr);
#endif
    }

    static tm to_local_time(const std::time_t& t)
    {
        tm parts{};
#if defined(_WIN32)
        localtime_s(&parts, &t);
#else
        parts = *localtime(&t);
#endif
        return parts;
    }

    Optional<tm> to_utc_time(const std::time_t& t)
    {
        tm parts{};
#if defined(_WIN32)
        const errno_t err = gmtime_s(&parts, &t);
        if (err)
        {
            return nullopt;
        }
#else
        auto null_if_failed = gmtime_r(&t, &parts);
        if (null_if_failed == nullptr)
        {
            return nullopt;
        }
#endif
        return parts;
    }

    static tm date_plus_hours(tm* date, const int hours)
    {
        using namespace std::chrono_literals;
        static constexpr std::chrono::seconds SECONDS_IN_ONE_HOUR =
            std::chrono::duration_cast<std::chrono::seconds>(1h);

        const std::time_t date_in_seconds = utc_mktime(date) + (hours * SECONDS_IN_ONE_HOUR.count());
        return to_utc_time(date_in_seconds).value_or_exit(VCPKG_LINE_INFO);
    }

    static std::string format_time_userfriendly(const std::chrono::nanoseconds& nanos)
    {
        using std::chrono::duration_cast;
        using std::chrono::hours;
        using std::chrono::milliseconds;
        using std::chrono::minutes;
        using std::chrono::nanoseconds;
        using std::chrono::seconds;

        auto ns_total = nanos.count();

        std::string ret;

        const auto one_day_ns = duration_cast<nanoseconds>(hours(24)).count();
        if (ns_total >= one_day_ns) {
            int64_t d = ns_total / one_day_ns;
            ns_total %= one_day_ns;
            if (d != 0) ret.append(Strings::format("%dd", d));
        }

        const auto one_hour_ns = duration_cast<nanoseconds>(hours(1)).count();
        if (ns_total >= one_hour_ns) {
            int64_t h = ns_total / one_hour_ns;
            ns_total %= one_hour_ns;
            if (h != 0) ret.append(Strings::format("%dh", h));
        }

        const auto one_minute_ns = duration_cast<nanoseconds>(minutes(1)).count();
        if (ns_total >= one_minute_ns) {
            int64_t m = ns_total / one_minute_ns;
            ns_total %= one_minute_ns;
            if (m != 0) ret.append(Strings::format("%dm", m));
        }

        const auto one_second_ns = duration_cast<nanoseconds>(seconds(1)).count();
        const auto one_millisecond_ns = duration_cast<nanoseconds>(milliseconds(1)).count();
        if (ns_total >= one_millisecond_ns) {
            int64_t s = ns_total / one_second_ns;
            ns_total %= one_second_ns;
            int64_t ms = ns_total / one_millisecond_ns;
            ret.append(Strings::format("%d.%03ds", s, ms));
        }

        return ret;
    }

    ElapsedTimer::ElapsedTimer() noexcept
        : m_start_tick(std::chrono::high_resolution_clock::now().time_since_epoch().count())
    {
    }

    std::string ElapsedTime::to_string() const { return format_time_userfriendly(as<std::chrono::nanoseconds>()); }
    void ElapsedTime::to_string(std::string& into) const
    {
        into += format_time_userfriendly(as<std::chrono::nanoseconds>());
    }

    std::string ElapsedTimer::to_string() const { return elapsed().to_string(); }
    void ElapsedTimer::to_string(std::string& into) const { return elapsed().to_string(into); }

    Optional<CTime> CTime::now()
    {
        const std::time_t ct = get_current_time_as_time_since_epoch();
        const Optional<tm> opt = to_utc_time(ct);
        if (auto p_tm = opt.get())
        {
            return CTime{*p_tm};
        }

        return nullopt;
    }

    std::string CTime::now_string()
    {
        auto maybe_time = CTime::now();
        if (auto ptime = maybe_time.get())
        {
            return ptime->to_string();
        }

        return std::string();
    }

    Optional<CTime> CTime::parse(ZStringView str)
    {
        CTime ret;
        const auto assigned =
#if defined(_WIN32)
            sscanf_s
#else
            sscanf
#endif
            (str.c_str(),
             "%d-%d-%dT%d:%d:%d.",
             &ret.m_tm.tm_year,
             &ret.m_tm.tm_mon,
             &ret.m_tm.tm_mday,
             &ret.m_tm.tm_hour,
             &ret.m_tm.tm_min,
             &ret.m_tm.tm_sec);
        if (assigned != 6) return nullopt;
        if (ret.m_tm.tm_year < 1900) return nullopt;
        ret.m_tm.tm_year -= 1900;
        if (ret.m_tm.tm_mon < 1) return nullopt;
        ret.m_tm.tm_mon -= 1;
        utc_mktime(&ret.m_tm);

        return ret;
    }

    CTime CTime::add_hours(const int hours) const { return CTime{date_plus_hours(&this->m_tm, hours)}; }

    std::string CTime::to_string() const { return this->strftime("%Y-%m-%dT%H:%M:%SZ"); }
    std::string CTime::strftime(const char* format) const
    {
        std::array<char, 80> date{};
        ::strftime(date.data(), date.size(), format, &m_tm);
        return date.data();
    }
    std::chrono::system_clock::time_point CTime::to_time_point() const
    {
        const time_t t = utc_mktime(&m_tm);
        return std::chrono::system_clock::from_time_t(t);
    }

    tm get_current_date_time_local()
    {
        const std::time_t now_time = get_current_time_as_time_since_epoch();
        return to_local_time(now_time);
    }
}
