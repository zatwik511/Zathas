#include "gemini_inference.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using json = nlohmann::json;

GeminiInferenceEngine::GeminiInferenceEngine(std::string api_key,
                                             std::string model,
                                             std::string host)
    : api_key_(std::move(api_key))
    , model_(std::move(model))
    , host_(std::move(host))
{
    std::cout << "[gemini] Fallback engine ready - model: " << model_ << "\n";
}

// ── Request building ──────────────────────────────────────────────────────────
//
// Gemini differs from the OpenAI shape in three ways that matter here:
//   - the system prompt is its own `systemInstruction`, not a message
//   - the assistant role is called "model"
//   - images are inline_data parts rather than image_url

namespace {

json text_part(const std::string& s)
{
    return json{{"text", s}};
}

json build_request(const ContextLayers& ctx, int max_tokens, float temperature,
                   const std::string& model)
{
    json contents = json::array();

    auto push_turn = [&](const char* role, json parts) {
        contents.push_back({{"role", role}, {"parts", std::move(parts)}});
    };

    // Background knowledge, as an opening exchange so it is clearly context
    // rather than something the user just said.
    if (!ctx.background.empty()) {
        push_turn("user",  json::array({text_part("<background>\n" + ctx.background +
                                                  "\n</background>")}));
        push_turn("model", json::array({text_part(
            "Understood. I have reviewed the background information.")}));
    }

    if (!ctx.document.empty()) {
        const std::string doc = ctx.document.size() > 12000
            ? ctx.document.substr(0, 12000) + "\n[document truncated]"
            : ctx.document;
        const std::string name = ctx.document_name.empty()
            ? "the uploaded file" : "\"" + ctx.document_name + "\"";
        push_turn("user",  json::array({text_part(
            "The user attached a file (" + name + "). Its text has been extracted "
            "below - treat it as fully available and answer questions about it "
            "directly.\n\n<document>\n" + doc + "\n</document>")}));
        push_turn("model", json::array({text_part(
            "Got it - I've read " + name + " and can answer questions about it.")}));
    }

    for (const auto& m : ctx.prior_sessions)
        push_turn(m.role == "assistant" ? "model" : "user",
                  json::array({text_part(m.content)}));

    const bool has_image = !ctx.image_b64.empty();
    for (size_t k = 0; k < ctx.current_session.size(); ++k) {
        const auto& m = ctx.current_session[k];
        const bool is_last_user =
            has_image && m.role == "user" && (k + 1 == ctx.current_session.size());

        json parts = json::array({text_part(m.content)});
        if (is_last_user) {
            for (const auto& b64 : ctx.image_b64) {
                parts.push_back({{"inline_data",
                                  {{"mime_type", ctx.image_mime.empty() ? "image/png"
                                                                        : ctx.image_mime},
                                   {"data", b64}}}});
            }
        }
        push_turn(m.role == "assistant" ? "model" : "user", std::move(parts));
    }

    json body = {
        {"contents", contents},
        {"generationConfig", {{"maxOutputTokens", max_tokens},
                              {"temperature",     temperature}}}
    };
    if (!ctx.system_prompt.empty())
        body["systemInstruction"] = {{"parts", json::array({text_part(ctx.system_prompt)})}};

    (void)model;
    return body;
}

// Pulls text out of one streamed chunk. Gemini nests it several levels deep and
// any level may be absent on a given chunk (safety blocks, empty candidates).
void collect_text(const json& j, std::string* out)
{
    if (!j.contains("candidates")) return;
    for (const auto& cand : j["candidates"]) {
        if (!cand.contains("content")) continue;
        const auto& content = cand["content"];
        if (!content.contains("parts")) continue;
        for (const auto& part : content["parts"])
            if (part.contains("text") && part["text"].is_string())
                *out += part["text"].get<std::string>();
    }
}

}   // namespace

// ── Generation ────────────────────────────────────────────────────────────────

std::string GeminiInferenceEngine::generate(const ContextLayers& ctx,
                                            int   max_tokens,
                                            float temperature,
                                            const TokenCallback& on_token,
                                            const DoneCallback&  on_done)
{
    const json body = build_request(ctx, max_tokens, temperature, model_);

    httplib::SSLClient cli(host_);
    cli.set_connection_timeout(15);
    cli.set_read_timeout(120);

    std::string buf;            // unparsed SSE bytes
    std::string full_response;
    bool        error_status = false;
    std::string error_body;

    httplib::Request req;
    req.method = "POST";
    // The key goes in a header rather than the query string so it cannot end up
    // in an intermediary's access log.
    req.path = "/v1beta/models/" + model_ + ":streamGenerateContent?alt=sse";
    req.set_header("x-goog-api-key", api_key_);
    req.set_header("Content-Type", "application/json");
    req.body = body.dump();

    req.response_handler = [&](const httplib::Response& r) {
        error_status = (r.status != 200);
        return true;
    };
    req.content_receiver = [&](const char* data, size_t len,
                               uint64_t /*off*/, uint64_t /*total*/) {
        if (error_status) { error_body.append(data, len); return true; }

        buf.append(data, len);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("data: ", 0) != 0) continue;

            const std::string payload = line.substr(6);
            if (payload == "[DONE]") continue;

            try {
                std::string piece;
                collect_text(json::parse(payload), &piece);
                if (!piece.empty()) {
                    full_response += piece;
                    if (on_token) on_token(piece);
                }
            } catch (...) { /* skip malformed chunk */ }
        }
        return true;
    };

    httplib::Response resp;
    httplib::Error    err = httplib::Error::Success;
    if (!cli.send(req, resp, err)) {
        throw ProviderError(0, "Gemini connection failed: " + httplib::to_string(err));
    }
    if (resp.status != 200) {
        throw ProviderError(resp.status, "Gemini returned HTTP " +
                            std::to_string(resp.status) + ": " + error_body);
    }

    if (on_done) on_done();
    return full_response;
}

// ── Startup validation ────────────────────────────────────────────────────────

bool gemini_list_models(const std::string& api_key,
                        std::vector<std::string>* out,
                        std::string* err,
                        const std::string& host)
{
    httplib::SSLClient cli(host);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(15);

    const httplib::Headers headers = {{"x-goog-api-key", api_key}};
    auto res = cli.Get("/v1beta/models?pageSize=200", headers);

    if (!res) {
        if (err) *err = "could not reach " + host + ": " + httplib::to_string(res.error());
        return false;
    }
    if (res->status == 401 || res->status == 403) {
        if (err) *err = "authentication rejected (HTTP " + std::to_string(res->status) +
                        ") - check GEMINI_API_KEY";
        return false;
    }
    if (res->status != 200) {
        if (err) *err = "HTTP " + std::to_string(res->status) + " from " + host +
                        ": " + res->body;
        return false;
    }

    try {
        const auto j = json::parse(res->body);
        if (!j.contains("models")) return true;   // reachable, just nothing listed
        for (const auto& m : j["models"]) {
            if (!m.contains("name") || !m["name"].is_string()) continue;
            std::string name = m["name"].get<std::string>();
            // Listed as "models/gemini-2.0-flash"; callers use the bare id.
            const auto slash = name.find('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);
            out->push_back(name);
        }
    } catch (const std::exception& e) {
        if (err) *err = std::string("unexpected model-list response: ") + e.what();
        return false;
    }
    return true;
}
