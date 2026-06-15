#include "cloud_inference.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
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

    // System turn
    msgs.push_back({{"role", "system"}, {"content", ctx.system_prompt}});

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

// ── SSE chunk parser ──────────────────────────────────────────────────────────

struct SSEParser {
    std::string              buf;
    std::string              full_response;
    const TokenCallback&     on_token;
    bool                     done = false;

    explicit SSEParser(const TokenCallback& cb) : on_token(cb) {}

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
                        if (!tok.empty()) {
                            full_response += tok;
                            if (on_token) on_token(tok);
                        }
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
    const std::string& use_model = ctx.image_b64.empty() ? model_ : vision_model_;

    const json body = {
        {"model",       use_model},
        {"messages",    build_messages(ctx)},
        {"stream",      true},
        {"max_tokens",  max_tokens},
        {"temperature", temperature}
    };

    httplib::SSLClient cli(host_);
    cli.set_connection_timeout(15);
    cli.set_read_timeout(120);

    SSEParser parser(on_token);

    // Use low-level send() so we can attach a streaming ContentReceiver to a POST.
    httplib::Request req;
    req.method = "POST";
    req.path   = "/openai/v1/chat/completions";
    req.set_header("Authorization", "Bearer " + api_key_);
    req.set_header("Content-Type",  "application/json");
    req.body = body.dump();
    req.content_receiver = [&](const char* data, size_t len,
                                uint64_t /*offset*/, uint64_t /*total*/) {
        return parser.feed(data, len);
    };

    httplib::Response resp;
    httplib::Error    err = httplib::Error::Success;
    if (!cli.send(req, resp, err)) {
        throw std::runtime_error("Cloud API connection failed: " +
                                 httplib::to_string(err));
    }
    if (resp.status != 200) {
        throw std::runtime_error("Cloud API returned HTTP " +
                                 std::to_string(resp.status) + ": " + resp.body);
    }

    if (on_done) on_done();
    return parser.full_response;
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
