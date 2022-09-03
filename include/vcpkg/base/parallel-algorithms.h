#pragma once

#if defined(USE_PARALLEL_ALG) && !defined(GNU_USE_PARALLEL_ALG)
#include <algorithm>
#include <execution>
#elif defined(GNU_USE_PARALLEL_ALG)
#include <parallel/algorithm>
#else
#include <algorithm>
#endif

#if defined(USE_PARALLEL_ALG) && !defined(GNU_USE_PARALLEL_ALG)
#define vcpkg_parallel_for_each std::for_each(std::execution::par,
#elif defined(GNU_USE_PARALLEL_ALG)
#define vcpkg_parallel_for_each __gnu_parallel::for_each(
#else
#define vcpkg_parallel_for_each std::for_each(
#endif

#if defined(USE_PARALLEL_ALG) && !defined(GNU_USE_PARALLEL_ALG)
#define vcpkg_par_unseq_for_each std::for_each(std::execution::par_unseq,
#else
#define vcpkg_par_unseq_for_each vcpkg_parallel_for_each
#endif
