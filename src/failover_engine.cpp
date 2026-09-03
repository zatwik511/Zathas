#include "failover_engine.h"

#include <iostream>
#include <utility>

FailoverEngine::FailoverEngine(std::shared_ptr<IInferenceEngine> primary,
                               std::shared_ptr<IInferenceEngine> fallback,
                               std::string primary_name,
                               std::string fallback_name)
    : primary_(std::move(primary))
    , fallback_(std::move(fallback))
    , primary_name_(std::move(primary_name))
    , fallback_name_(std::move(fallback_name))
{
    std::cout << "[failover] Primary: " << primary_name_
              << ", fallback: " << (fallback_ ? fallback_name_ : "none") << "\n";
}

std::string FailoverEngine::last_backend() const
{
    return last_was_fallback_.load() ? fallback_name_ : primary_name_;
}

std::string FailoverEngine::generate(const ContextLayers& ctx,
                                     int   max_tokens,
                                     float temperature,
                                     const TokenCallback& on_token,
                                     const DoneCallback&  on_done)
{
    // Whether any token has reached the caller. Once it has, the answer is
    // partly delivered and switching providers would corrupt it.
    bool emitted = false;
    const TokenCallback watch = [&](const std::string& piece) {
        emitted = true;
        if (on_token) on_token(piece);
    };

    try {
        last_was_fallback_.store(false);
        return primary_->generate(ctx, max_tokens, temperature, watch, on_done);
    }
    catch (const ProviderError& e) {
        if (!fallback_) throw;

        if (!e.worth_retrying_elsewhere()) {
            // A bad request or rejected key fails the same way anywhere.
            std::cerr << "[failover] " << primary_name_ << " failed with HTTP "
                      << e.status() << "; not retryable elsewhere\n";
            throw;
        }
        if (emitted) {
            std::cerr << "[failover] " << primary_name_
                      << " failed mid-stream; not retrying, part of the reply "
                         "has already been sent (" << e.what() << ")\n";
            throw;
        }
        std::cerr << "[failover] " << primary_name_ << " -> " << fallback_name_
                  << " (" << e.what() << ")\n";
    }

    try {
        last_was_fallback_.store(true);
        return fallback_->generate(ctx, max_tokens, temperature, on_token, on_done);
    }
    catch (const ProviderError& e) {
        std::cerr << "[failover] " << fallback_name_ << " also failed: "
                  << e.what() << "\n";
        throw ProviderError(e.status(),
            "Both inference providers are unavailable right now. "
            "Please try again in a moment.");
    }
}
