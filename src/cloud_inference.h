#pragma once
#include "inference.h"
#include <string>

// Calls a Groq-compatible OpenAI chat/completions endpoint over HTTPS.
// Drop-in replacement for InferenceEngine on the public route.
class CloudInferenceEngine : public IInferenceEngine {
public:
    CloudInferenceEngine(std::string api_key,
                         std::string model = "llama-3.3-70b-versatile",
                         std::string host  = "api.groq.com",
                         std::string vision_model = "meta-llama/llama-4-scout-17b-16e-instruct");

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

// Transcribe an audio file via Groq's Whisper endpoint. Returns the transcript,
// or empty string on failure (with an error message in `err` if provided).
std::string groq_transcribe(const std::string& api_key,
                            const std::string& audio_bytes,
                            const std::string& filename,
                            const std::string& model = "whisper-large-v3",
                            std::string* err = nullptr,
                            const std::string& host = "api.groq.com");
