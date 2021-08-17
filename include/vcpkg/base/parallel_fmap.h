#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <future>

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
        std::atomic<size_t> work_item{0};

        const auto num_threads =
            static_cast<size_t>(std::max(1, std::min(get_concurrency(), static_cast<int>(xs.size()))));

        auto work = [&xs, &res, &f, &work_item]() {
            for (size_t item = work_item.fetch_add(1); item < xs.size(); item = work_item.fetch_add(1))
            {
                res[item] = f(xs[item]);
            }
        };

        std::vector<std::future<void>> workers;
        for (size_t x = 0; x < num_threads - 1; ++x)
        {
            workers.emplace_back(std::async(std::launch::async | std::launch::deferred, work));
            if (work_item >= xs.size())
            {
                break;
            }
        }
        work();
        for (auto&& w : workers)
        {
            w.get();
        }
        return res;
    }

}