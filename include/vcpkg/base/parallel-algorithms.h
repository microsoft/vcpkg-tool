#pragma once

#if defined(USE_PARALLEL_ALG) && !defined(GNU_USE_PARALLEL_ALG)
#include <algorithm>
#include <execution>
#elif defined(GNU_USE_PARALLEL_ALG)
#include <parallel/algorithm>
#else
#include <algorithm>
#endif

#if defined(USE_PARALLEL_ALG)
#include <mutex>
#endif

#if defined(USE_PARALLEL_ALG) && !defined(GNU_USE_PARALLEL_ALG)
#define vcpkg_parallel_for_each(BEGIN, END, CB) std::for_each(std::execution::par, BEGIN, END, CB)
#elif defined(GNU_USE_PARALLEL_ALG)
#define vcpkg_parallel_for_each(BEGIN, END, CB) __gnu_parallel::for_each(BEGIN, END, CB)
#else
#define vcpkg_parallel_for_each(BEGIN, END, CB) std::for_each(BEGIN, END, CB)
#endif

#if defined(USE_PARALLEL_ALG) && !defined(GNU_USE_PARALLEL_ALG)
#define vcpkg_par_unseq_for_each(BEGIN, END, CB) std::for_each(std::execution::par_unseq, BEGIN, END, CB)
#else
#define vcpkg_par_unseq_for_each(BEGIN, END, CB) vcpkg_parallel_for_each(BEGIN, END, CB)
#endif

#if defined(USE_PARALLEL_ALG)
#define VCPKG_MUTEX std::mutex mtx
#define VCPKG_LOCK_GUARD std::lock_guard<std::mutex> guard(mtx)
#else
#define VCPKG_MUTEX
#define VCPKG_LOCK_GUARD
#endif
