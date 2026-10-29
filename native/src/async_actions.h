#pragma once
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <utility>

namespace interfayce {
// Called only from the UI thread. Work runs off-thread; completion stays on UI.
// FIFO preserves accepted actions. Reserve one extra slot for a microphone release.
template<class Result> class AsyncActions {
    struct Action {
        std::function<Result()> work;
        std::function<void(Result)> complete;
    };
    std::deque<Action> pending_;
    std::future<Result> running_;
    std::function<void(Result)> complete_;
    static constexpr size_t kMaxPending = 8;
public:
    bool Busy() const { return running_.valid() || !pending_.empty(); }
    bool Submit(std::function<Result()> work, std::function<void(Result)> complete,
                bool release = false) {
        if (pending_.size() >= kMaxPending + (release ? 1 : 0)) return false;
        pending_.push_back({std::move(work), std::move(complete)});
        return true;
    }
    void Tick() {
        if (running_.valid()) {
            if (running_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            auto result = running_.get();
            auto complete = std::move(complete_);
            complete(std::move(result));
        }
        if (!pending_.empty()) {
            auto action = std::move(pending_.front());
            pending_.pop_front();
            complete_ = std::move(action.complete);
            running_ = std::async(std::launch::async, std::move(action.work));
        }
    }
};
}
