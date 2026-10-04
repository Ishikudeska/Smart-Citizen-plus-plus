#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace engine {

using ProgressCallback = std::function<void(std::size_t done, std::size_t total)>;

// Runs work items [0, count) on `threads` threads (0 = hardware
// concurrency). `makeWorker` is called once per thread and returns a
// callable taking an item index, so each thread can own scratch state.
// `progress` is called on the calling thread about ten times a second and
// once at the end. Stops handing out items once `cancel` is set.
template <class MakeWorker>
void parallelFor(std::size_t count, unsigned threads, MakeWorker &&makeWorker,
                 const ProgressCallback &progress, const std::atomic<bool> *cancel)
{
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};

    unsigned threadCount = threads ? threads : std::thread::hardware_concurrency();
    threadCount =
        std::max(1u, std::min<unsigned>(threadCount, static_cast<unsigned>(std::max<std::size_t>(count, 1))));

    std::mutex mutex;
    std::condition_variable cv;
    unsigned finished = 0;

    std::vector<std::thread> pool;
    pool.reserve(threadCount);
    for (unsigned t = 0; t < threadCount; ++t) {
        pool.emplace_back([&] {
            {
                auto work = makeWorker();
                for (;;) {
                    if (cancel && cancel->load(std::memory_order_relaxed))
                        break;
                    const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= count)
                        break;
                    work(i);
                    done.fetch_add(1, std::memory_order_relaxed);
                }
            }
            std::lock_guard lock(mutex);
            ++finished;
            cv.notify_one();
        });
    }

    {
        std::unique_lock lock(mutex);
        while (finished < threadCount) {
            cv.wait_for(lock, std::chrono::milliseconds(100));
            if (progress) {
                lock.unlock();
                progress(done.load(std::memory_order_relaxed), count);
                lock.lock();
            }
        }
    }
    for (auto &t : pool)
        t.join();
    if (progress)
        progress(done.load(), count);
}

} // namespace engine
