#pragma once

#include <vcpkg/base/cstringview.h>
#include <vcpkg/base/optional.h>

#include <chrono>
#include <string>

namespace vcpkg::Chrono
{
    struct ElapsedTime
    {
        using duration = std::chrono::high_resolution_clock::time_point::duration;

        constexpr ElapsedTime() noexcept : m_duration() { }
        constexpr ElapsedTime(duration d) noexcept : m_duration(d) { }

        template<class TimeUnit>
        TimeUnit as() const
        {
            return std::chrono::duration_cast<TimeUnit>(m_duration);
        }

        std::string to_string() const;
        void to_string(std::string& into) const;

    private:
        duration m_duration;
    };

    struct ElapsedTimer
    {
        static ElapsedTimer create_started();

        constexpr ElapsedTimer() noexcept : m_start_tick() { }

        ElapsedTime elapsed() const
        {
            return ElapsedTime(std::chrono::high_resolution_clock::now() - this->m_start_tick);
        }

        double microseconds() const { return elapsed().as<std::chrono::duration<double, std::micro>>().count(); }

        std::string to_string() const;
        void to_string(std::string& into) const;

    private:
        std::chrono::high_resolution_clock::time_point m_start_tick;
    };

    struct CTime
    {
        static Optional<CTime> get_current_date_time();
        static Optional<CTime> parse(CStringView str);

        constexpr CTime() noexcept : m_tm{} { }
        explicit constexpr CTime(tm t) noexcept : m_tm{t} { }

        CTime add_hours(const int hours) const;

        std::string to_string() const;

        std::chrono::system_clock::time_point to_time_point() const;

    private:
        mutable tm m_tm;
    };

    tm get_current_date_time();

    tm get_current_date_time_local();
}
