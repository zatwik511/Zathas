#pragma once
#include <string>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <chrono>

// Simple in-memory sliding-window rate limiter, keyed by client (IP).
// Thread-safe. Intended to protect public, cost-incurring endpoints from abuse.
class RateLimiter {
public:
    RateLimiter(int max_requests, int window_seconds)
        : max_(max_requests), window_(window_seconds) {}

    // Returns true if the request is allowed; false if the limit is exceeded.
    bool allow(const std::string& key) {
        const auto now = std::chrono::steady_clock::now();
        const auto cutoff = now - std::chrono::seconds(window_);
        std::lock_guard<std::mutex> lk(mu_);

        auto& dq = hits_[key];
        while (!dq.empty() && dq.front() < cutoff) dq.pop_front();
        if (static_cast<int>(dq.size()) >= max_) return false;
        dq.push_back(now);

        if (hits_.size() > 4096) prune(cutoff);   // bound memory
        return true;
    }

private:
    void prune(std::chrono::steady_clock::time_point cutoff) {
        for (auto it = hits_.begin(); it != hits_.end(); ) {
            while (!it->second.empty() && it->second.front() < cutoff) it->second.pop_front();
            if (it->second.empty()) it = hits_.erase(it); else ++it;
        }
    }

    int max_;
    int window_;
    std::mutex mu_;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> hits_;
};
