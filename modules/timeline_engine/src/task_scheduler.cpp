#include "timeline/task_scheduler.hpp"

#include <algorithm>

namespace owncut::timeline {

TaskScheduler::TaskScheduler(std::size_t background_threads) {
    const std::size_t bg = std::max<std::size_t>(1, background_threads);
    // Dedicated real-time/touch-scrub worker isolated from heavy background proxy/cache threads.
    workers_.emplace_back([this]() { worker_loop(true); });
    for (std::size_t i = 0; i < bg; ++i) {
        workers_.emplace_back([this]() { worker_loop(false); });
    }
}

TaskScheduler::~TaskScheduler() {
    shutdown();
}

uint64_t TaskScheduler::submit(TaskPriority priority,
                               std::function<void(const TaskCancellationToken&)> work,
                               std::string coalesce_key,
                               TaskCancellationToken token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return 0;

    const auto idx = static_cast<std::size_t>(priority);
    if (!coalesce_key.empty()) {
        auto it = pending_keys_[idx].find(coalesce_key);
        if (it != pending_keys_[idx].end()) {
            const uint64_t old_id = it->second;
            for (auto& task : lanes_[idx]) {
                if (task.id == old_id && task.active) {
                    task.active = false;
                    task.token.cancel();
                    ++stats_.coalesced[idx];
                    break;
                }
            }
        }
    }

    const uint64_t id = next_id_++;
    ++stats_.submitted[idx];
    if (!coalesce_key.empty()) {
        pending_keys_[idx][coalesce_key] = id;
    }
    lanes_[idx].push_back(QueuedTask{id, priority, std::move(coalesce_key), std::move(token), std::move(work), true});

    if (priority == TaskPriority::RealtimePlayback || priority == TaskPriority::TouchScrubbing) {
        cv_high_.notify_one();
    } else {
        cv_bg_.notify_one();
    }
    return id;
}

void TaskScheduler::cancel_lane(TaskPriority priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto idx = static_cast<std::size_t>(priority);
    for (auto& task : lanes_[idx]) {
        if (task.active) {
            task.active = false;
            task.token.cancel();
            ++stats_.cancelled[idx];
        }
    }
    pending_keys_[idx].clear();
}

void TaskScheduler::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_idle_.wait(lock, [this]() {
        if (in_flight_ > 0) return false;
        for (const auto& lane : lanes_) {
            for (const auto& t : lane) {
                if (t.active) return false;
            }
        }
        return true;
    });
}

void TaskScheduler::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
        for (int i = 0; i < 4; ++i) {
            for (auto& task : lanes_[i]) {
                if (task.active) {
                    task.active = false;
                    task.token.cancel();
                    ++stats_.cancelled[i];
                }
            }
            lanes_[i].clear();
            pending_keys_[i].clear();
        }
    }
    cv_high_.notify_all();
    cv_bg_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

SchedulerStats TaskScheduler::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

bool TaskScheduler::pop_next(bool high_priority_only, QueuedTask& out) {
    const int max_lane = high_priority_only ? 1 : 3;
    for (int lane = 0; lane <= max_lane; ++lane) {
        while (!lanes_[lane].empty()) {
            QueuedTask candidate = std::move(lanes_[lane].front());
            lanes_[lane].pop_front();
            if (!candidate.coalesce_key.empty()) {
                auto it = pending_keys_[lane].find(candidate.coalesce_key);
                if (it != pending_keys_[lane].end() && it->second == candidate.id) {
                    pending_keys_[lane].erase(it);
                }
            }
            if (!candidate.active) {
                continue;
            }
            if (candidate.token.is_cancelled()) {
                ++stats_.cancelled[lane];
                continue;
            }
            out = std::move(candidate);
            return true;
        }
    }
    return false;
}

void TaskScheduler::worker_loop(bool high_priority_only) {
    for (;;) {
        QueuedTask task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto& cv = high_priority_only ? cv_high_ : cv_bg_;
            cv.wait(lock, [this, high_priority_only]() {
                if (stopping_) return true;
                const int max_lane = high_priority_only ? 1 : 3;
                for (int i = 0; i <= max_lane; ++i) {
                    if (!lanes_[i].empty()) return true;
                }
                return false;
            });
            if (stopping_) return;
            if (!pop_next(high_priority_only, task)) {
                bool any_left = false;
                for (const auto& lane : lanes_) {
                    for (const auto& t : lane) {
                        if (t.active) { any_left = true; break; }
                    }
                }
                if (!any_left && in_flight_ == 0) cv_idle_.notify_all();
                continue;
            }
            ++in_flight_;
        }

        if (task.work) {
            task.work(task.token);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.completed[static_cast<std::size_t>(task.priority)];
            --in_flight_;
            bool any_left = false;
            for (const auto& lane : lanes_) {
                for (const auto& t : lane) {
                    if (t.active) { any_left = true; break; }
                }
            }
            if (!any_left && in_flight_ == 0) {
                cv_idle_.notify_all();
            }
        }
    }
}

} // namespace owncut::timeline
