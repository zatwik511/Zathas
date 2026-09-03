#pragma once
#include "config.h"
#include "inference.h"
#include "docstore.h"
#include "rate_limit.h"
#include "modules/module.h"
#include <httplib.h>
#include <string>
#include <memory>

struct ServerConfig {
    std::string host         = config::kHost;
    int         port         = config::kPort;
    std::string static_dir   = config::kStaticDir;
    int         max_tokens   = config::kMaxTokens;
    float       temperature  = config::kTemperature;
    std::string exe_dir;      // directory of the running binary

    // Cloud credentials/models for chat + multimodal handling.
    std::string groq_api_key;                                   // enables vision + Whisper
    std::string cloud_model   = config::kCloudModel;
    std::string vision_model  = config::kVisionModel;
    std::string whisper_model = config::kWhisperModel;

    // Extra origins permitted to call the API cross-site, comma-separated.
    // Empty means same-origin only.
    std::string allowed_origins = config::kAllowedOrigins;
};

class ChatServer {
public:
    // `local_engine` is the llama.cpp engine when one was configured; it may be
    // null, and may be the same object as `engine` on local-only deployments.
    // It is handed to optional server modules that need on-device inference.
    ChatServer(std::shared_ptr<IInferenceEngine> engine,
               const ServerConfig& config,
               std::shared_ptr<IInferenceEngine> local_engine = nullptr);

    void run();
    void shutdown();

private:
    // Per-IP admission, applied early so a flood is rejected cheaply.
    bool admit(const httplib::Request& req, httplib::Response& res, RateLimiter& limiter);

    // Spends one unit of the service-wide daily budget. Call this only once the
    // request has passed validation and is definitely about to reach the
    // provider — a request rejected as malformed or oversized costs nothing to
    // serve, so charging it would let junk traffic exhaust the day's budget.
    bool charge_daily(httplib::Response& res);

    std::shared_ptr<IInferenceEngine> engine_;   // cloud (Groq) or local model
    std::shared_ptr<IInferenceEngine> local_engine_;
    ServerConfig                      cfg_;
    DocStore                          doc_store_;
    httplib::Server                   svr_;

    // Handed to optional modules at startup. Held as a member so route handlers
    // registered by a module can safely capture it by reference.
    ModuleContext                     module_ctx_;

    // Per-IP rate limiters for cost-incurring endpoints.
    RateLimiter chat_limiter_  {config::kChatPerMinute,  config::kRateWindowSecs};
    RateLimiter media_limiter_ {config::kMediaPerMinute, config::kRateWindowSecs};

    // Service-wide budget, shared by every caller.
    DailyCap    daily_cap_     {config::kDailyRequestCap};
};
