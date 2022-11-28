#pragma once

#include <vcpkg/base/fwd/span.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <vector>

namespace vcpkg
{
    template<class T>
    struct Span
    {
    public:
        static_assert(std::is_object_v<T>, "Span<non-object-type> is illegal");

        using value_type = std::decay_t<T>;
        using element_type = T;
        using pointer = std::add_pointer_t<T>;
        using reference = std::add_lvalue_reference_t<T>;
        using iterator = pointer;

        constexpr Span() noexcept : m_ptr(nullptr), m_count(0) { }
        constexpr Span(std::nullptr_t) noexcept : m_ptr(nullptr), m_count(0) { }
        constexpr Span(pointer ptr, size_t count) noexcept : m_ptr(ptr), m_count(count) { }
        constexpr Span(pointer ptr_begin, pointer ptr_end) noexcept : m_ptr(ptr_begin), m_count(ptr_end - ptr_begin) { }

        template<size_t N>
        constexpr Span(T (&arr)[N]) noexcept : m_ptr(arr), m_count(N)
        {
        }

        template<class Range,
                 class = decltype(std::declval<Range>().data()),
                 class = std::enable_if_t<!std::is_same_v<std::decay_t<Range>, Span>>>
        constexpr Span(Range&& v) noexcept : Span(v.data(), v.size())
        {
            static_assert(std::is_same_v<typename std::decay_t<Range>::value_type, value_type>,
                          "Cannot convert incompatible ranges");
        }

        constexpr iterator begin() const noexcept { return m_ptr; }
        constexpr iterator end() const noexcept { return m_ptr + m_count; }

        constexpr reference operator[](size_t i) const noexcept { return m_ptr[i]; }
        constexpr pointer data() const noexcept { return m_ptr; }
        constexpr size_t size() const noexcept { return m_count; }
        constexpr bool empty() const noexcept { return m_count == 0; }

    private:
        pointer m_ptr;
        size_t m_count;
    };
}
