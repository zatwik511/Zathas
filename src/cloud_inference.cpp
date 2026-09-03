#include "cloud_inference.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

using json = nlohmann::json;

CloudInferenceEngine::CloudInferenceEngine(std::string api_key,
                                           std::string model,
                                           std::string host,
                                           std::string vision_model)
    : api_key_(std::move(api_key))
    , model_(std::move(model))
    , host_(std::move(host))
    , vision_model_(std::move(vision_model))
{
    std::cout << "[cloud] Engine ready — text: " << model_
              << ", vision: " << vision_model_ << "\n";
}

// ── Convert ContextLayers to OpenAI messages array ────────────────────────────

static json build_messages(const ContextLayers& ctx)
{
    json msgs = json::array();

    // System turn, with any background knowledge folded in.
    std::string sys = ctx.system_prompt;
    if (!ctx.background.empty()) sys += "\n\n" + ctx.background;
    msgs.push_back({{"role", "system"}, {"content", sys}});

    // Prior sessions, replayed verbatim ahead of the current conversation.
    for (const auto& m : ctx.prior_sessions)
        msgs.push_back({{"role", m.role}, {"content", m.content}});

    // Document injection. Frame it as an attached file whose text has already
    // been extracted for the model, so it uses the content directly instead of
    // claiming it "cannot read files".
    if (!ctx.document.empty()) {
        const std::string doc = ctx.document.size() > 12000
            ? ctx.document.substr(0, 12000) + "\n[document truncated]"
            : ctx.document;
        const std::string name = ctx.document_name.empty() ? "the uploaded file"
                                                            : "\"" + ctx.document_name + "\"";
        msgs.push_back({{"role", "user"},
            {"content", "The user attached a file (" + name + "). Its text has been "
                        "extracted for you below — treat it as fully available and answer "
                        "questions about it directly.\n\n<document>\n" + doc +
                        "\n</document>"}});
        msgs.push_back({{"role", "assistant"},
            {"content", "Got it — I've read " + name + " and can answer questions about it."}});
    }

    // Current session. If an image is attached, the final user turn becomes a
    // multimodal message (text + image_url) so the vision model can see it.
    const bool has_image = !ctx.image_b64.empty();
    for (size_t k = 0; k < ctx.current_session.size(); ++k) {
        const auto& m = ctx.current_session[k];
        const bool is_last_user =
            has_image && m.role == "user" && (k + 1 == ctx.current_session.size());
        if (is_last_user) {
            json content = json::array();
            content.push_back({{"type", "text"}, {"text", m.content}});
            for (const auto& b64 : ctx.image_b64) {
                content.push_back({{"type", "image_url"},
                                   {"image_url", {{"url", "data:" + ctx.image_mime +
                                                          ";base64," + b64}}}});
            }
            msgs.push_back({{"role", "user"}, {"content", content}});
        } else {
            msgs.push_back({{"role", m.role}, {"content", m.content}});
        }
    }

    return msgs;
}

// ── <think> stripping ─────────────────────────────────────────────────────────
// Reasoning-capable models (the vision model is one) stream their chain of
// thought inside <think>...</think> ahead of the actual answer. It is dropped
// here rather than in the UI: anything forwarded to the client is kept in the
// message state, persisted, and replayed as history on every later turn, so
// hiding it at render time would still pay input tokens for it repeatedly.

static constexpr const char* THINK_OPEN  = "<think>";
static constexpr const char* THINK_CLOSE = "</think>";

// Length of the longest suffix of `s` that is a proper prefix of `tag`. Used to
// hold back a few trailing bytes in case a tag is split across two SSE chunks.
static size_t partial_tag_suffix(const std::string& s, const char* tag)
{
    const size_t tag_len = std::strlen(tag);
    for (size_t n = std::min(s.size(), tag_len - 1); n > 0; --n) {
        if (s.compare(s.size() - n, n, tag, n) == 0) return n;
    }
    return 0;
}

// ── SSE chunk parser ──────────────────────────────────────────────────────────

struct SSEParser {
    std::string              buf;
    std::string              full_response;
    const TokenCallback&     on_token;
    bool                     done = false;

    std::string              hold;                 // bytes awaiting tag resolution
    bool                     in_think = false;
    bool                     seen_visible = false; // trims blank lines before the answer

    explicit SSEParser(const TokenCallback& cb) : on_token(cb) {}

    void emit(const std::string& piece) {
        std::string out = piece;
        if (!seen_visible) {
            const size_t i = out.find_first_not_of(" \t\r\n");
            if (i == std::string::npos) return;   // still only whitespace
            out.erase(0, i);
            seen_visible = true;
        }
        full_response += out;
        if (on_token) on_token(out);
    }

    // Feed one model token, emitting only the text outside <think> blocks.
    void push(const std::string& tok) {
        hold += tok;
        for (;;) {
            const char* tag = in_think ? THINK_CLOSE : THINK_OPEN;
            const size_t p = hold.find(tag);
            if (p != std::string::npos) {
                if (!in_think && p > 0) emit(hold.substr(0, p));
                hold.erase(0, p + std::strlen(tag));
                in_think = !in_think;
                continue;
            }
            const size_t keep = partial_tag_suffix(hold, tag);
            if (hold.size() > keep) {
                if (!in_think) emit(hold.substr(0, hold.size() - keep));
                hold.erase(0, hold.size() - keep);
            }
            return;
        }
    }

    // Flush whatever is still held once the stream ends.
    void finish() {
        if (!in_think && !hold.empty()) emit(hold);
        hold.clear();

        // A reasoning model that exhausts its token budget mid-thought never
        // emits </think>, so everything it produced is suppressed above and the
        // user is left staring at an empty reply. Say what happened instead.
        if (!seen_visible) {
            static const std::string msg =
                "(I ran out of room working through that one and didn't reach an "
                "answer. Please try again, or ask for something more specific.)";
            seen_visible   = true;
            full_response += msg;
            if (on_token) on_token(msg);
        }
    }

    // Feed raw bytes; returns false to abort the connection.
    bool feed(const char* data, size_t len) {
        buf.append(data, len);
        size_t pos;
        while ((pos = buf.find("\n\n")) != std::string::npos) {
            const std::string event = buf.substr(0, pos);
            buf = buf.substr(pos + 2);

            if (event.size() < 6 || event.substr(0, 6) != "data: ") continue;
            const std::string payload = event.substr(6);

            if (payload == "[DONE]") { done = true; continue; }

            try {
                const auto j = json::parse(payload);
                const auto& choices = j.at("choices");
                if (!choices.empty()) {
                    const auto& delta = choices[0].at("delta");
                    if (delta.contains("content") && delta["content"].is_string()) {
                        const std::string tok = delta["content"].get<std::string>();
                        if (!tok.empty()) push(tok);
                    }
                }
            } catch (...) { /* skip malformed chunk */ }
        }
        return true;
    }
};

// ── Main generate ─────────────────────────────────────────────────────────────

std::string CloudInferenceEngine::generate(
    const ContextLayers& ctx,
    int   max_tokens,
    float temperature,
    const TokenCallback& on_token,
    const DoneCallback&  on_done)
{
    // Route to the vision model when an image is attached, otherwise the text model.
    const bool         use_vision = !ctx.image_b64.empty();
    const std::string& use_model  = use_vision ? vision_model_ : model_;

    // The vision model reasons inside <think> before it writes anything the user
    // sees, and that reasoning alone can run to ~500 tokens. At the default
    // budget it regularly hits the cap mid-thought, leaving no answer at all, so
    // give the vision path enough headroom for reasoning *and* a reply.
    const int effective_max_tokens =
        use_vision ? std::max(max_tokens, config::kVisionMinTokens) : max_tokens;

    json body = {
        {"model",       use_model},
        {"messages",    build_messages(ctx)},
        {"stream",      true},
        {"max_tokens",  effective_max_tokens},
        {"temperature", temperature}
    };
    // Keep the text model's chain-of-thought short — gpt-oss reasons in a
    // separate `reasoning` field (not <think> tags in content) that we never
    // read, so unbounded reasoning just eats max_tokens with nothing to show
    // for it. Vision model (different family) doesn't support this field.
    if (!use_vision) body["reasoning_effort"] = "low";

    httplib::SSLClient cli(host_);
    cli.set_connection_timeout(15);
    cli.set_read_timeout(120);

    SSEParser parser(on_token);

    // Use low-level send() so we can attach a streaming ContentReceiver to a POST.
    // cpp-httplib routes the ENTIRE response body through content_receiver once
    // it's set — including error bodies — so resp.body is never populated on
    // failure. Capture the status via response_handler (fires before the body
    // starts streaming) and divert non-200 bytes into error_body instead of
    // feeding them to the SSE parser.
    bool        error_status = false;
    std::string error_body;

    httplib::Request req;
    req.method = "POST";
    req.path   = "/openai/v1/chat/completions";
    req.set_header("Authorization", "Bearer " + api_key_);
    req.set_header("Content-Type",  "application/json");
    req.body = body.dump();
    req.response_handler = [&](const httplib::Response& r) {
        error_status = (r.status != 200);
        return true;
    };
    req.content_receiver = [&](const char* data, size_t len,
                                uint64_t /*offset*/, uint64_t /*total*/) {
        if (error_status) { error_body.append(data, len); return true; }
        return parser.feed(data, len);
    };

    httplib::Response resp;
    httplib::Error    err = httplib::Error::Success;
    if (!cli.send(req, resp, err)) {
        throw ProviderError(0, "Groq connection failed: " + httplib::to_string(err));
    }
    if (resp.status != 200) {
        throw ProviderError(resp.status, "Groq returned HTTP " +
                            std::to_string(resp.status) + ": " + error_body);
    }

    parser.finish();
    if (on_done) on_done();
    return parser.full_response;
}

// ── Startup validation ────────────────────────────────────────────────────────

bool cloud_list_models(const std::string& api_key,
                       std::vector<std::string>* out,
                       std::string* err,
                       const std::string& host)
{
    httplib::SSLClient cli(host);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(15);

    const httplib::Headers headers = {{"Authorization", "Bearer " + api_key}};
    auto res = cli.Get("/openai/v1/models", headers);

    if (!res) {
        if (err) *err = "could not reach " + host + ": " + httplib::to_string(res.error());
        return false;
    }
    if (res->status == 401 || res->status == 403) {
        if (err) *err = "authentication rejected (HTTP " + std::to_string(res->status) +
                        ") — check the API key";
        return false;
    }
    if (res->status != 200) {
        if (err) *err = "HTTP " + std::to_string(res->status) + " from " + host + ": " + res->body;
        return false;
    }

    try {
        const auto j = json::parse(res->body);
        for (const auto& m : j.at("data"))
            if (m.contains("id") && m["id"].is_string())
                out->push_back(m["id"].get<std::string>());
    } catch (const std::exception& e) {
        if (err) *err = std::string("unexpected model-list response: ") + e.what();
        return false;
    }
    return true;
}

bool cloud_check_model(const std::string& api_key,
                       const std::string& model,
                       std::string* err,
                       const std::string& host)
{
    std::vector<std::string> models;
    if (!cloud_list_models(api_key, &models, err, host)) return false;

    if (std::find(models.begin(), models.end(), model) != models.end()) return true;

    if (err) {
        std::string msg = "model \"" + model + "\" is not available to this key. ";
        if (models.empty()) {
            msg += "The provider returned no models at all.";
        } else {
            std::sort(models.begin(), models.end());
            msg += "Available: ";
            const size_t show = std::min<size_t>(models.size(), 8);
            for (size_t i = 0; i < show; ++i) {
                if (i) msg += ", ";
                msg += models[i];
            }
            if (models.size() > show)
                msg += ", ... (" + std::to_string(models.size()) + " total)";
        }
        *err = msg;
    }
    return false;
}

// ── Audio transcription (Groq Whisper) ────────────────────────────────────────

std::string groq_transcribe(const std::string& api_key,
                            const std::string& audio_bytes,
                            const std::string& filename,
                            const std::string& model,
                            std::string* err,
                            const std::string& host)
{
    httplib::SSLClient cli(host);
    cli.set_connection_timeout(20);
    cli.set_read_timeout(180);

    const httplib::Headers headers = {{"Authorization", "Bearer " + api_key}};
    httplib::MultipartFormDataItems items = {
        {"model",           model,  "", ""},
        {"response_format", "text", "", ""},
        {"file", audio_bytes, filename.empty() ? "audio" : filename,
                 "application/octet-stream"},
    };

    auto res = cli.Post("/openai/v1/audio/transcriptions", headers, items);
    if (!res) {
        if (err) *err = "connection failed: " + httplib::to_string(res.error());
        return "";
    }
    if (res->status != 200) {
        if (err) *err = "HTTP " + std::to_string(res->status) + ": " + res->body;
        return "";
    }
    return res->body;   // response_format=text -> plain transcript
}
