#include "cloud_inference.h"
#include "sse_parser.h"
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

    sse::Parser parser(on_token);

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
    return parser.text();
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
