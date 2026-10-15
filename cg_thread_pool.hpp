/**
 * cg_thread_pool.hpp - Persistent worker-thread pool for CG-AHE protocols
 *
 * Worker threads are created once and reused through a condition-variable task
 * queue. Each worker keeps its own thread-local CG-AHE scheme state, so
 * encryption, decryption, mauling, and polynomial loops can run in parallel
 * without repeatedly constructing BICYCL/CG-AHE objects.
 *
 * Usage:
 *   CGThreadPool pool(num_threads);   // blocks until all workers are ready
 *   pool.parallel_for(0, N, fn);      // fn(i) for i in [0,N), blocks until done
 */
#pragma once

#include "cg_config.hpp"
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <exception>

class CGThreadPool {
public:
    explicit CGThreadPool(size_t n_threads = 0)
        : n_(n_threads == 0 ? std::thread::hardware_concurrency() : n_threads)
        , stop_(false)
    {
        if (n_ > 256) throw std::runtime_error("too many worker threads");
        workers_.reserve(n_);
        try {
            for (size_t i = 0; i < n_; ++i) workers_.emplace_back([this] { worker_loop(); });
        } catch (...) {
            { std::lock_guard<std::mutex> lock(mu_); stop_ = true; }
            cv_work_.notify_all();
            for (auto& worker : workers_) worker.join();
            throw;
        }
    }

    ~CGThreadPool() {
        {
            std::unique_lock<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_work_.notify_all();
        for (auto& w : workers_) w.join();
    }

    // The pool owns worker threads and is intentionally not copyable.
    CGThreadPool(const CGThreadPool&) = delete;
    CGThreadPool& operator=(const CGThreadPool&) = delete;

    /**
     * Execute fn(i) for i in [begin, end) across all worker threads.
     * Uses work-stealing via an atomic cursor. Blocks until all work is done.
     */
    template<typename Fn>
    void parallel_for(size_t begin, size_t end, Fn fn, size_t chunk_size = 1) {
        if (end <= begin) return;
        if (n_ == 0) {
            for (size_t i = begin; i < end; ++i) fn(i);
            return;
        }
        if (chunk_size == 0) chunk_size = 1;

        // Set up shared iteration state
        std::atomic<size_t> cursor{begin};
        std::atomic<size_t> done_count{0};
        std::mutex           done_mu;
        std::condition_variable done_cv;
        std::mutex exception_mu;
        std::exception_ptr first_exception = nullptr;
        std::atomic<bool> has_exception{false};
        const size_t total = end - begin;
        const size_t n_tasks = std::min(n_, (total + chunk_size - 1) / chunk_size);

        // Each worker task: steal iterations until cursor >= end
        auto task = [&] {
            try {
                while (true) {
                    if (has_exception.load(std::memory_order_acquire))
                        break;
                    size_t base = cursor.fetch_add(chunk_size, std::memory_order_relaxed);
                    if (base >= end) break;
                    size_t stop = std::min(base + chunk_size, end);
                    for (size_t i = base; i < stop; ++i)
                        fn(i);
                }
            } catch (...) {
                std::lock_guard<std::mutex> lk(exception_mu);
                if (!first_exception) {
                    first_exception = std::current_exception();
                    has_exception.store(true, std::memory_order_release);
                }
            }
            std::unique_lock<std::mutex> lk(done_mu);
            size_t finished = done_count.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (finished == n_tasks) done_cv.notify_one();
        };

        // Dispatch task to all workers
        {
            std::unique_lock<std::mutex> lk(mu_);
            for (size_t t = 0; t < n_tasks; ++t)
                tasks_.push(task);
        }
        cv_work_.notify_all();

        // Wait for all workers to finish this batch
        {
            std::unique_lock<std::mutex> lk(done_mu);
            done_cv.wait(lk, [&] { return done_count.load() == n_tasks; });
        }

        if (has_exception.load(std::memory_order_acquire))
            std::rethrow_exception(first_exception);
    }

    size_t num_threads() const { return n_; }

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_work_.wait(lk, [this] {
                    return stop_ || !tasks_.empty();
                });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    size_t                              n_;
    std::vector<std::thread>            workers_;
    std::queue<std::function<void()>>   tasks_;
    std::mutex                          mu_;
    std::condition_variable             cv_work_;
    bool                                stop_;
};

/**
 * Global persistent pool — initialised once at program startup.
 * All thread_local state (CG_Scheme, RandGen) inside parallel_for lambdas
 * is constructed exactly once per worker thread.
 */
inline CGThreadPool& global_pool() {
    static CGThreadPool pool([] {
        const char* env = std::getenv("CG_RF_THREADS");
        if (env && *env) {
            size_t n = cg_parse_size(env, "CG_RF_THREADS", false, 256);
            if (n > 0) return (size_t)n;
        }
        size_t hw = std::thread::hardware_concurrency();
        return hw == 0 ? (size_t)1 : hw;
    }());
    return pool;
}
