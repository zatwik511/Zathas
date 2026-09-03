#pragma once
#include "config.h"
#include "inference.h"
#include <string>
#include <vector>

// Calls a Groq-compatible OpenAI chat/completions endpoint over HTTPS.
// Drop-in replacement for InferenceEngine on the public route.
class CloudInferenceEngine : public IInferenceEngine {
public:
    CloudInferenceEngine(std::string api_key,
                         std::string model = config::kCloudModel,
                         std::string host  = config::kGroqHost,
                         std::string vision_model = config::kVisionModel);

    std::string generate(const ContextLayers& ctx,
                         int              max_tokens  = 512,
                         float            temperature = 0.7f,
                         const TokenCallback& on_token = {},
                         const DoneCallback&  on_done  = {}) override;

private:
    std::string api_key_;
    std::string model_;
    std::string host_;
    std::string vision_model_;   // used when ctx carries an image
};

// ── Startup validation ────────────────────────────────────────────────────────
// Asks the provider which models this key can reach. Returns false and fills
// `err` on a connection failure, bad key, or unexpected response.
bool cloud_list_models(const std::string& api_key,
                       std::vector<std::string>* out,
                       std::string* err = nullptr,
                       const std::string& host = config::kGroqHost);

// Verifies `model` is actually available to this key. On failure `err` explains
// why and names some models that are available, so a retired or renamed model
// is obvious at startup instead of on a user's first request.
bool cloud_check_model(const std::string& api_key,
                       const std::string& model,
                       std::string* err = nullptr,
                       const std::string& host = config::kGroqHost);

// Transcribe an audio file via Groq's Whisper endpoint. Returns the transcript,
// or empty string on failure (with an error message in `err` if provided).
std::string groq_transcribe(const std::string& api_key,
                            const std::string& audio_bytes,
                            const std::string& filename,
                            const std::string& model = config::kWhisperModel,
                            std::string* err = nullptr,
                            const std::string& host = config::kGroqHost);
