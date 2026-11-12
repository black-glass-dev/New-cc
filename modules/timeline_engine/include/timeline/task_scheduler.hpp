#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

namespace owncut::timeline {

enum class TaskPriority : uint8_t {
    RealtimePlayback = 0,
    TouchScrubbing = 1,
    SmartRenderCache = 2,
    BackgroundProxyWaveform = 3
};

struct SchedulerStats final {
    uint64_t submitted[4]{};
    uint64_t completed[4]{};
    uint64_t cancelled[4]{};
    uint64_t coalesced[4]{};
};

class TaskCancellationToken final {
public:
    TaskCancellationToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
    void cancel() noexcept { flag_->store(true, std::memory_order_release); }
    [[nodiscard]] bool is_cancelled() const noexcept {
        return flag_->load(std::memory_order_acquire);
    }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

// Priority-isolated thread pool with dedicated real-time/scrub lanes and task coalescing.
class TaskScheduler final {
public:
    explicit TaskScheduler(std::size_t background_threads = 2);
    ~TaskScheduler();

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    // Submit a task under a specific priority lane. Optional `coalesce_key` replaces any pending
    // unstarted task in the same lane with the same key (ideal for rapid touch scrubbing).
    uint64_t submit(TaskPriority priority,
                    std::function<void(const TaskCancellationToken&)> work,
                    std::string coalesce_key = {},
                    TaskCancellationToken token = {});

    template <typename Fn>
    auto submit_future(TaskPriority priority, Fn&& fn) -> std::future<decltype(fn())> {
        using Result = decltype(fn());
        auto promise = std::make_shared<std::promise<Result>>();
        auto future = promise->get_future();
        submit(priority, [promise, f = std::forward<Fn>(fn)](const TaskCancellationToken& tok) mutable {
            if (tok.is_cancelled()) {
                try {
                    throw std::runtime_error("task cancelled");
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
                return;
            }
            try {
                if constexpr (std::is_void_v<Result>) {
                    f();
                    promise->set_value();
                } else {
                    promise->set_value(f());
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
        return future;
    }

    void cancel_lane(TaskPriority priority);
    void wait_idle();
    void shutdown();

    [[nodiscard]] SchedulerStats stats() const;

private:
    struct QueuedTask {
        uint64_t id{0};
        TaskPriority priority{TaskPriority::BackgroundProxyWaveform};
        std::string coalesce_key;
        TaskCancellationToken token;
        std::function<void(const TaskCancellationToken&)> work;
        bool active{true};
    };

    void worker_loop(bool high_priority_only);
    bool pop_next(bool high_priority_only, QueuedTask& out);

    mutable std::mutex mutex_;
    std::condition_variable cv_high_;
    std::condition_variable cv_bg_;
    std::condition_variable cv_idle_;
    std::deque<QueuedTask> lanes_[4];
    std::unordered_map<std::string, uint64_t> pending_keys_[4];
    std::vector<std::thread> workers_;
    std::size_t in_flight_{0};
    uint64_t next_id_{1};
    bool stopping_{false};
    SchedulerStats stats_{};
};

} // namespace owncut::timeline
