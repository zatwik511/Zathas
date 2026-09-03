#pragma once
#include "config.h"
#include "inference.h"

#include <string>
#include <vector>

// Google Generative Language API backend.
//
// Exists so the service is not single-provider: paired with the Groq engine
// behind FailoverEngine, a model retirement or rate limit on one side does not
// take chat down. Speaks Gemini's own request shape rather than the OpenAI one.
class GeminiInferenceEngine : public IInferenceEngine {
public:
    explicit GeminiInferenceEngine(std::string api_key,
                                   std::string model = config::kGeminiModel,
                                   std::string host  = config::kGeminiHost);

    std::string generate(const ContextLayers& ctx,
                         int              max_tokens  = 512,
                         float            temperature = 0.7f,
                         const TokenCallback& on_token = {},
                         const DoneCallback&  on_done  = {}) override;

private:
    std::string api_key_;
    std::string model_;
    std::string host_;
};

// Lists models this key can reach, as bare ids ("gemini-2.0-flash"). Returns
// false and fills `err` on failure. Used by startup validation.
bool gemini_list_models(const std::string& api_key,
                        std::vector<std::string>* out,
                        std::string* err = nullptr,
                        const std::string& host = config::kGeminiHost);
