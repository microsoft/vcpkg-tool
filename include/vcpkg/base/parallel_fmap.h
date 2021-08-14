#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <thread>

namespace vcpkg::Util
{
    template<class Range, class Func, class Out = FmapOut<Range, Func>>
    std::vector<Out> parallel_fmap(Range&& xs, Func&& f)
    {
        if (xs.size() == 0)
        {
            return {};
        }
        if (xs.size() == 1)
        {
            return {f(xs[0])};
        }
        std::vector<Out> res(xs.size());

        const auto num_threads =
            static_cast<size_t>(std::max(1, std::min(get_concurrency(), static_cast<int>(xs.size()))));

        auto work = [&xs, &res, &f, num_threads](const int first, const int last) {
            for (int n = first; n < last; ++n)
            {
                res[n] = f(xs[n]);
            }
        };

        std::vector<std::thread> threads;
        const auto chunksize = xs.size() / num_threads;
        const auto bonus = xs.size() % num_threads;

        size_t first = 0;
        for (size_t x = 0; x < num_threads - 1; ++x)
        {
            const auto last = first + chunksize + (bonus > x ? 1 : 0);
            Checks::check_exit(VCPKG_LINE_INFO, last < xs.size());
            threads.emplace_back(work, first, last);
            first = last;
        }
        const auto last = first + chunksize;
        Checks::check_exit(VCPKG_LINE_INFO, last == xs.size());
        work(first, last);
        for (auto&& th : threads)
        {
            th.join();
        }
        return res;
    }

}