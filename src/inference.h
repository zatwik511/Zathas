#pragma once
#include <string>
#include <vector>
#include <functional>

struct Message {
    std::string role;    // "user" or "assistant"
    std::string content;
};

// Tiered context passed to generate(). Built by the server on every request.
struct ContextLayers {
    std::string          system_prompt;
    std::string          document;          // uploaded document text; empty if none
    std::string          document_name;     // original filename of the attachment
    std::vector<Message> current_session;   // live turns from the current conversation

    // Multimodal: when image(s) are attached, these carry them so a vision-capable
    // model can be used. image_b64 holds one or more base64-encoded images (e.g.
    // several rendered pages of a scanned PDF); image_mime is the shared mime
    // (e.g. "image/png"). Empty when there is no image.
    std::vector<std::string> image_b64;
    std::string              image_mime;
};

// Callbacks used during streaming generation.
// on_token: called for each new token string.
// on_done:  called when generation is complete.
using TokenCallback = std::function<void(const std::string& token)>;
using DoneCallback  = std::function<void()>;

// Abstract interface — implemented by InferenceEngine (local) and CloudInferenceEngine (API).
class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;
    virtual std::string generate(const ContextLayers& ctx,
                                 int              max_tokens  = 512,
                                 float            temperature = 0.7f,
                                 const TokenCallback& on_token = {},
                                 const DoneCallback&  on_done  = {}) = 0;
};

class InferenceEngine : public IInferenceEngine {
public:
    explicit InferenceEngine(const std::string& model_path,
                             int   n_ctx        = 4096,
                             int   n_threads    = 4,
                             int   n_gpu_layers = 0);
    ~InferenceEngine();

    // Non-copyable
    InferenceEngine(const InferenceEngine&)            = delete;
    InferenceEngine& operator=(const InferenceEngine&) = delete;

    // Primary entry point: generate a response from tiered context layers.
    std::string generate(const ContextLayers& ctx,
                         int              max_tokens  = 512,
                         float            temperature = 0.7f,
                         const TokenCallback& on_token = {},
                         const DoneCallback&  on_done  = {}) override;

    bool is_loaded() const { return ctx_ != nullptr; }

private:
    struct llama_model*        model_   = nullptr;
    struct llama_context*      ctx_     = nullptr;
    const struct llama_vocab*  vocab_   = nullptr;

    std::string build_prompt(const ContextLayers& ctx) const;
};
