#pragma once
#include "inference.h"

#include <atomic>
#include <memory>
#include <string>

// Sends generation to a primary engine and retries on a second one when the
// primary fails in a way another provider might not share — the model was
// retired, we are being throttled, or the provider is down.
//
// Deliberately does NOT retry once tokens have reached the client: the reply is
// streamed, so restarting elsewhere would splice two different answers together.
class FailoverEngine : public IInferenceEngine {
public:
    FailoverEngine(std::shared_ptr<IInferenceEngine> primary,
                   std::shared_ptr<IInferenceEngine> fallback,
                   std::string primary_name  = "primary",
                   std::string fallback_name = "fallback");

    std::string generate(const ContextLayers& ctx,
                         int              max_tokens  = 512,
                         float            temperature = 0.7f,
                         const TokenCallback& on_token = {},
                         const DoneCallback&  on_done  = {}) override;

    // Which backend served the most recent request, for /api/health.
    std::string last_backend() const;

private:
    std::shared_ptr<IInferenceEngine> primary_;
    std::shared_ptr<IInferenceEngine> fallback_;
    std::string                       primary_name_;
    std::string                       fallback_name_;
    std::atomic<bool>                 last_was_fallback_{false};
};
