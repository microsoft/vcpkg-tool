#pragma once

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <vector>

// Add more forwarding functions to the m_data std::vector as needed.
namespace vcpkg
{
    template<class Ty>
    struct SortedVector
    {
        using size_type = typename std::vector<Ty>::size_type;
        using iterator = typename std::vector<Ty>::const_iterator;

        SortedVector() = default;
        SortedVector(const SortedVector&) = default;
        SortedVector(SortedVector&&) = default;
        SortedVector& operator=(const SortedVector&) = default;
        SortedVector& operator=(SortedVector&&) = default;

        explicit SortedVector(const std::vector<Ty>& data) : m_data(data) { sort_uniqueify(std::less<>{}); }

        template<class Compare>
        SortedVector(const std::vector<Ty>& data, Compare comp) : m_data(data)
        {
            sort_uniqueify(comp);
        }

        explicit SortedVector(std::vector<Ty>&& data) : m_data(std::move(data)) { sort_uniqueify(std::less<>{}); }

        template<class Compare>
        explicit SortedVector(std::vector<Ty>&& data, Compare comp) : m_data(std::move(data))
        {
            sort_uniqueify(comp);
        }

        template<class InIt>
        explicit SortedVector(InIt first, InIt last) : m_data(first, last)
        {
            sort_uniqueify(std::less<>{});
        }

        template<class InIt, class Compare>
        explicit SortedVector(InIt first, InIt last, Compare comp) : m_data(first, last)
        {
            sort_uniqueify(comp);
        }

        SortedVector(std::initializer_list<Ty> elements) : m_data(elements) { sort_uniqueify(std::less<>{}); }

        iterator begin() const { return this->m_data.cbegin(); }

        iterator end() const { return this->m_data.cend(); }

        iterator cbegin() const { return this->m_data.cbegin(); }

        iterator cend() const { return this->m_data.cend(); }

        bool empty() const { return this->m_data.empty(); }

        size_type size() const { return this->m_data.size(); }

        const Ty& operator[](std::size_t i) const { return this->m_data[i]; }

        bool contains(const Ty& element) const { return std::binary_search(m_data.begin(), m_data.end(), element); }

        void append(const SortedVector& other) { append(other, std::less<>{}); }

        template<class Compare>
        void append(const SortedVector& other, Compare comp)
        {
            // This could use a more efficient merge algorithm than inplace_merge with an understanding that we will
            // allocate the whole result if perf becomes a problem
            auto merge_point = m_data.insert(m_data.end(), other.begin(), other.end());
            std::inplace_merge(m_data.begin(), merge_point, m_data.end(), comp);
            uniqueify(comp);
        }

        void append(SortedVector&& other) { append(std::move(other), std::less<>{}); }

        template<class Compare>
        void append(SortedVector&& other, Compare comp)
        {
            auto merge_point = m_data.insert(
                m_data.end(), std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()));
            std::inplace_merge(m_data.begin(), merge_point, m_data.end(), comp);
            uniqueify(comp);
        }

        friend bool operator==(const SortedVector& lhs, const SortedVector& rhs) { return lhs.m_data == rhs.m_data; }
        friend bool operator!=(const SortedVector& lhs, const SortedVector& rhs) { return lhs.m_data != rhs.m_data; }

    private:
        template<class Compare>
        void uniqueify(Compare comp)
        {
            m_data.erase(std::unique(m_data.begin(),
                                     m_data.end(),
                                     [comp](const Ty& lhs, const Ty& rhs) {
                                         // note that we know !comp(rhs, lhs) because m_data is sorted
                                         return !comp(lhs, rhs);
                                     }),
                         m_data.end());
        }

        template<class Compare>
        void sort_uniqueify(Compare comp)
        {
            if (!std::is_sorted(m_data.begin(), m_data.end(), comp))
            {
                std::sort(m_data.begin(), m_data.end(), comp);
            }

            uniqueify(comp);
        }

        std::vector<Ty> m_data;
    };
}
