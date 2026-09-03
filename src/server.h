#pragma once
#include "inference.h"
#include "docstore.h"
#include "rate_limit.h"
#include "modules/module.h"
#include <httplib.h>
#include <string>
#include <memory>

struct ServerConfig {
    std::string host         = "0.0.0.0";
    int         port         = 8080;
    std::string static_dir   = "./frontend";
    int         max_tokens   = 512;
    float       temperature  = 0.7f;
    std::string exe_dir;      // directory of the running binary

    // Groq credentials/models for cloud chat + multimodal handling.
    std::string groq_api_key;                                   // enables vision + Whisper
    std::string vision_model  = "meta-llama/llama-4-scout-17b-16e-instruct";
    std::string whisper_model = "whisper-large-v3";
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
    std::shared_ptr<IInferenceEngine> engine_;   // cloud (Groq) or local model
    std::shared_ptr<IInferenceEngine> local_engine_;
    ServerConfig                      cfg_;
    DocStore                          doc_store_;
    httplib::Server                   svr_;

    // Handed to optional modules at startup. Held as a member so route handlers
    // registered by a module can safely capture it by reference.
    ModuleContext                     module_ctx_;

    // Per-IP rate limiters for cost-incurring endpoints.
    RateLimiter chat_limiter_  {25, 60};   // 25 messages / minute / IP
    RateLimiter media_limiter_ {15, 60};   // 15 uploads or transcriptions / minute / IP
};
