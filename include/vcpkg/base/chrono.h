#pragma once

#include <vcpkg/base/format.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>

#include <atomic>
#include <chrono>
#include <string>

namespace vcpkg
{
    struct ElapsedTime
    {
        using clock = std::chrono::high_resolution_clock;
        using duration = clock::duration;

        constexpr ElapsedTime() noexcept : m_duration() { }
        constexpr ElapsedTime(duration d) noexcept : m_duration(d) { }

        template<class TimeUnit>
        TimeUnit as() const
        {
            return std::chrono::duration_cast<TimeUnit>(m_duration);
        }

        ElapsedTime& operator+=(const ElapsedTime& other)
        {
            m_duration += other.m_duration;
            return *this;
        }

        std::string to_string() const;
        void to_string(std::string& into) const;

    private:
        duration m_duration;
    };

    // This type is safe to access from multiple threads.
    struct ElapsedTimer
    {
        using clock = std::chrono::high_resolution_clock;
        using duration = clock::duration;
        using time_point = clock::time_point;
        using rep = clock::rep;

        ElapsedTimer() noexcept;

        ElapsedTime elapsed() const
        {
            return ElapsedTime(clock::now() - time_point(duration(this->m_start_tick.load())));
        }

        double microseconds() const { return elapsed().as<std::chrono::duration<double, std::micro>>().count(); }
        uint64_t us_64() const { return elapsed().as<std::chrono::duration<uint64_t, std::micro>>().count(); }

        std::string to_string() const;
        void to_string(std::string& into) const;

    private:
        // This atomic stores rep rather than time_point to support older compilers
        std::atomic<rep> m_start_tick;
    };

    struct StatsTimer
    {
        StatsTimer(std::atomic<uint64_t>& stat) : m_stat(&stat), m_timer() { }
        ~StatsTimer() { m_stat->fetch_add(m_timer.us_64()); }

    private:
        std::atomic<uint64_t>* const m_stat;
        const ElapsedTimer m_timer;
    };

    struct CTime
    {
        static Optional<CTime> now();
        static std::string now_string();
        static Optional<CTime> parse(ZStringView str);

        constexpr CTime() noexcept : m_tm{} { }
        explicit constexpr CTime(tm t) noexcept : m_tm{t} { }

        CTime add_hours(const int hours) const;

        std::string to_string() const;

        std::string strftime(const char* format) const;

        std::chrono::system_clock::time_point to_time_point() const;

    private:
        mutable tm m_tm;
    };

    Optional<tm> to_utc_time(const std::time_t& t);

    tm get_current_date_time_local();
}

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::ElapsedTime);
VCPKG_FORMAT_WITH_TO_STRING(vcpkg::ElapsedTimer);
VCPKG_FORMAT_WITH_TO_STRING(vcpkg::CTime);
