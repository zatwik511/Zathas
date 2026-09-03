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

// Service-wide cap on provider-billed requests per UTC day, shared across all
// callers. The per-IP limiter above stops one visitor monopolising the service;
// this stops all of them together exhausting the operator's quota.
//
// Deliberately in-memory: a restart forgives the count, which is the safer
// failure direction for a small deployment (a bad restart loop degrades to "no
// cap" rather than "permanently at capacity").
class DailyCap {
public:
    explicit DailyCap(int max_per_day) : max_(max_per_day) {}

    // Consumes one unit. Returns false once the day's budget is spent.
    bool allow() {
        std::lock_guard<std::mutex> lk(mu_);
        roll_if_new_day();
        if (used_ >= max_) return false;
        ++used_;
        return true;
    }

    int used() const {
        std::lock_guard<std::mutex> lk(mu_);
        const_cast<DailyCap*>(this)->roll_if_new_day();
        return used_;
    }

    int limit() const { return max_; }

private:
    // Days since the epoch in UTC; changes exactly at midnight UTC.
    static long long today() {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::hours>(now).count() / 24;
    }

    void roll_if_new_day() {
        const long long d = today();
        if (d != day_) { day_ = d; used_ = 0; }
    }

    int                max_;
    int                used_ = 0;
    long long          day_  = today();
    mutable std::mutex mu_;
};
