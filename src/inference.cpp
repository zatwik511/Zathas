#include "inference.h"

#include <llama.h>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <cstring>

// ── Constructor / Destructor ───────────────────────────────────────────────────

InferenceEngine::InferenceEngine(const std::string& model_path,
                                 int n_ctx,
                                 int n_threads,
                                 int n_gpu_layers,
                                 const std::string& lora_path)
{
    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = n_gpu_layers;

    model_ = llama_load_model_from_file(model_path.c_str(), mparams);
    if (!model_) {
        throw std::runtime_error("Failed to load model from: " + model_path);
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx     = static_cast<uint32_t>(n_ctx);
    cparams.n_threads = static_cast<uint32_t>(n_threads);

    ctx_ = llama_new_context_with_model(model_, cparams);
    if (!ctx_) {
        llama_free_model(model_);
        model_ = nullptr;
        throw std::runtime_error("Failed to create llama context");
    }

    vocab_ = llama_model_get_vocab(model_);

    // Optional LoRA adapter. A failure here is non-fatal: log it and carry on
    // with the base model rather than refusing to start.
    if (!lora_path.empty()) {
        lora_ = llama_adapter_lora_init(model_, lora_path.c_str());
        if (!lora_) {
            std::cerr << "[inference] Warning: failed to load LoRA adapter: "
                      << lora_path << " — continuing with the base model\n";
        } else {
            llama_set_adapter_lora(ctx_, lora_, 1.0f);
            std::cout << "[inference] LoRA adapter loaded: " << lora_path << "\n";
        }
    }

    std::cout << "[inference] Model loaded: " << model_path << "\n";
}

InferenceEngine::~InferenceEngine()
{
    if (lora_)  { llama_adapter_lora_free(lora_); lora_  = nullptr; }
    if (ctx_)   { llama_free(ctx_);               ctx_   = nullptr; }
    if (model_) { llama_free_model(model_);       model_ = nullptr; }
    llama_backend_free();
}

// ── Prompt builders ────────────────────────────────────────────────────────────

// Prompt in Qwen ChatML format:
// system → background → document → prior sessions → current session.
std::string InferenceEngine::build_prompt(const ContextLayers& ctx) const
{
    std::ostringstream oss;

    // 1. System prompt
    oss << "<|im_start|>system\n" << ctx.system_prompt << "<|im_end|>\n";

    // 2. Background knowledge (if any)
    if (!ctx.background.empty()) {
        oss << "<|im_start|>user\n"
            << "<background>\n" << ctx.background << "\n</background><|im_end|>\n"
            << "<|im_start|>assistant\n"
            << "Understood. I have reviewed the background information.<|im_end|>\n";
    }

    // 3. Uploaded document (if any)
    if (!ctx.document.empty()) {
        const std::string doc_text = ctx.document.size() > 8000
            ? ctx.document.substr(0, 8000) + "\n[document truncated]"
            : ctx.document;
        oss << "<|im_start|>user\n"
            << "<document>\n" << doc_text << "\n</document><|im_end|>\n"
            << "<|im_start|>assistant\n"
            << "Understood. I have read the uploaded document.<|im_end|>\n";
    }

    // 4. Prior sessions, replayed verbatim
    for (const auto& msg : ctx.prior_sessions) {
        oss << "<|im_start|>" << msg.role << "\n" << msg.content << "<|im_end|>\n";
    }

    // 5. Current session turns
    for (const auto& msg : ctx.current_session) {
        oss << "<|im_start|>" << msg.role << "\n" << msg.content << "<|im_end|>\n";
    }

    // 6. Assistant generation header
    oss << "<|im_start|>assistant\n";
    return oss.str();
}

// Convenience overload: a flat message list with a default system turn, for
// utility calls (summarising, labelling) that carry no layered context. Reuses
// the main generation path rather than duplicating the sampling loop.
std::string InferenceEngine::generate(const std::vector<Message>& history,
                                      int   max_tokens,
                                      float temperature,
                                      const TokenCallback& on_token,
                                      const DoneCallback&  on_done)
{
    ContextLayers ctx;
    ctx.system_prompt   = "You are Zathas, an AI assistant. "
                          "Your own name is Zathas — not the user's name. "
                          "Be concise and helpful. Never break character.";
    ctx.current_session = history;
    return generate(ctx, max_tokens, temperature, on_token, on_done);
}

// ── Generation ─────────────────────────────────────────────────────────────────

std::string InferenceEngine::generate(const ContextLayers& ctx,
                                      int   max_tokens,
                                      float temperature,
                                      const TokenCallback& on_token,
                                      const DoneCallback&  on_done)
{
    if (!ctx_ || !model_) {
        throw std::runtime_error("Inference engine not initialized");
    }

    const std::string prompt = build_prompt(ctx);

    const int n_prompt_tokens = static_cast<int>(prompt.size()) * 2 + 16;
    std::vector<llama_token> tokens(n_prompt_tokens);
    const int n_tokens = llama_tokenize(
        vocab_,
        prompt.c_str(),
        static_cast<int32_t>(prompt.size()),
        tokens.data(),
        static_cast<int32_t>(tokens.size()),
        /*add_special=*/true,
        /*parse_special=*/true
    );
    if (n_tokens < 0) {
        throw std::runtime_error("Tokenization failed — prompt may be too long");
    }
    tokens.resize(n_tokens);

    const int n_ctx   = static_cast<int>(llama_n_ctx(ctx_));
    const int n_batch = llama_n_batch(ctx_);
    const int max_prompt = n_ctx - max_tokens - 8;
    int prompt_start = 0;
    int prompt_len   = static_cast<int>(tokens.size());
    if (prompt_len > max_prompt) {
        prompt_start = prompt_len - max_prompt;
        prompt_len   = max_prompt;
    }

    llama_kv_self_clear(ctx_);

    for (int i = 0; i < prompt_len; i += n_batch) {
        const int chunk = std::min(n_batch, prompt_len - i);
        llama_batch batch = llama_batch_get_one(tokens.data() + prompt_start + i,
                                                static_cast<int32_t>(chunk));
        if (llama_decode(ctx_, batch) != 0) {
            throw std::runtime_error("llama_decode failed on prompt chunk");
        }
    }

    llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(temperature));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    std::string result;
    result.reserve(512);

    const llama_token eos_id = llama_vocab_eos(vocab_);
    const llama_token eot_id = llama_vocab_eot(vocab_);

    for (int i = 0; i < max_tokens; ++i) {
        const llama_token new_token = llama_sampler_sample(sampler, ctx_, -1);
        llama_sampler_accept(sampler, new_token);

        if (new_token == eos_id || new_token == eot_id) break;

        char buf[256];
        const int n = llama_token_to_piece(vocab_, new_token, buf, sizeof(buf) - 1, 0, true);
        if (n < 0) continue;
        buf[n] = '\0';

        const std::string piece(buf);
        result += piece;
        if (on_token) on_token(piece);

        llama_token next_tokens[1] = { new_token };
        llama_batch next_batch = llama_batch_get_one(next_tokens, 1);
        if (llama_decode(ctx_, next_batch) != 0) break;
    }

    llama_sampler_free(sampler);

    if (on_done) on_done();
    return result;
}
