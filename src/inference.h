#pragma once
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>

// Thrown by remote engines when a provider call fails. Carries the HTTP status
// so callers can tell "this provider is broken right now" from "this request is
// wrong and will fail everywhere".
class ProviderError : public std::runtime_error {
public:
    // status 0 means the request never got a response (DNS, TLS, timeout).
    ProviderError(int status, const std::string& message)
        : std::runtime_error(message), status_(status) {}

    int status() const noexcept { return status_; }

    // Worth trying elsewhere: the model is gone (404), we are being throttled
    // (429), the provider is failing (5xx), or we never reached it (0). A 400 or
    // 401 is about this request or this key and would fail identically on a
    // second provider, so it is not retried.
    bool worth_retrying_elsewhere() const noexcept {
        return status_ == 0 || status_ == 404 || status_ == 429 || status_ >= 500;
    }

private:
    int status_;
};

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

    // Optional context injected ahead of the current session. Both are empty by
    // default and are provided by whatever is driving the conversation.
    //
    // `background` is free-form knowledge folded into the system turn — useful
    // for standing facts a deployment wants the model to have. `prior_sessions`
    // is verbatim earlier turns replayed as conversation history.
    //
    // Note for multi-user deployments: anything placed here is visible to the
    // request it is attached to, so it must be scoped to that conversation.
    // Do not populate it from data belonging to other users.
    std::string          background;
    std::vector<Message> prior_sessions;

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
    // `lora_path` optionally points at a GGUF LoRA adapter applied on top of the
    // base model. Empty means no adapter; a failed load is logged and ignored
    // rather than fatal, so a bad adapter path degrades to the base model.
    explicit InferenceEngine(const std::string& model_path,
                             int   n_ctx        = 4096,
                             int   n_threads    = 4,
                             int   n_gpu_layers = 0,
                             const std::string& lora_path = {});
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

    // Convenience overload for one-off prompts that are just a flat message list
    // (summarising, labelling, and similar utility calls) with no layered context.
    std::string generate(const std::vector<Message>& history,
                         int              max_tokens  = 512,
                         float            temperature = 0.7f,
                         const TokenCallback& on_token = {},
                         const DoneCallback&  on_done  = {});

    bool is_loaded() const { return ctx_ != nullptr; }

private:
    struct llama_model*        model_   = nullptr;
    struct llama_context*      ctx_     = nullptr;
    const struct llama_vocab*  vocab_   = nullptr;
    struct llama_adapter_lora* lora_    = nullptr;

    std::string build_prompt(const ContextLayers& ctx) const;
};
