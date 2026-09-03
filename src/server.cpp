#include "server.h"
#include "pdf_extract.h"
#include "pdf_render.h"
#include "media_ingest.h"
#include "office_extract.h"
#include "cloud_inference.h"
#include "modules/module.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <csignal>
#include <filesystem>
#include <algorithm>
#include <chrono>

using json = nlohmann::json;

// Client IP for rate limiting. Honour X-Forwarded-For (first hop) so it works
// correctly behind a reverse proxy (e.g. Caddy/nginx on the deployed box).
static std::string client_ip(const httplib::Request& req) {
    const std::string xff = req.get_header_value("X-Forwarded-For");
    if (!xff.empty()) {
        const auto comma = xff.find(',');
        std::string ip = (comma == std::string::npos) ? xff : xff.substr(0, comma);
        const auto a = ip.find_first_not_of(" \t");
        const auto b = ip.find_last_not_of(" \t");
        return (a == std::string::npos) ? ip : ip.substr(a, b - a + 1);
    }
    return req.remote_addr;
}

static ChatServer* g_server = nullptr;

static void on_signal(int)
{
    if (g_server) g_server->shutdown();
}

static std::vector<Message> parse_history(const json& j)
{
    std::vector<Message> msgs;
    for (const auto& item : j)
        msgs.push_back({ item.at("role").get<std::string>(),
                         item.at("content").get<std::string>() });
    return msgs;
}

// ── ChatServer ─────────────────────────────────────────────────────────────────

ChatServer::ChatServer(std::shared_ptr<IInferenceEngine> engine,
                       const ServerConfig& config,
                       std::shared_ptr<IInferenceEngine> local_engine)
    : engine_(std::move(engine)),
      local_engine_(std::move(local_engine)),
      cfg_(config)
{
    std::cout << "[server] Chat engine: " << (engine_ ? "ready" : "none") << "\n";
}

void ChatServer::shutdown()
{
    svr_.stop();
}

void ChatServer::run()
{
    g_server = this;
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    svr_.set_payload_max_length(10 * 1024 * 1024 + 4096);

    // ── GET /health ────────────────────────────────────────────────────────────
    svr_.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // ── POST /api/chat — public persona, SSE streaming ────────────────────────
    svr_.Post("/api/chat", [this](const httplib::Request& req, httplib::Response& res) {
        if (!chat_limiter_.allow(client_ip(req))) {
            res.status = 429;
            res.set_content(json{{"error", "You're sending messages too fast — please wait a moment."}}.dump(),
                            "application/json");
            return;
        }
        json body;
        try { body = json::parse(req.body); }
        catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", std::string("Invalid JSON: ") + e.what()}}.dump(),
                            "application/json");
            return;
        }
        if (!body.contains("message") || !body["message"].is_string()) {
            res.status = 400;
            res.set_content(json{{"error", "Missing or invalid 'message' field"}}.dump(),
                            "application/json");
            return;
        }

        const std::string user_msg = body["message"].get<std::string>();

        ContextLayers ctx;
        ctx.system_prompt  = "You are Zathas, a helpful AI assistant. "
                             "Be concise, friendly, and informative. "
                             "When a file is attached, its contents (text, a transcript, or "
                             "an image) are provided to you directly — read and use them to "
                             "answer. Never claim you cannot read files, PDFs, images, or audio.";
        if (body.contains("doc_id") && body["doc_id"].is_string()) {
            const StoredDoc d = doc_store_.get_doc(body["doc_id"].get<std::string>());
            if (d.kind == "image") {
                ctx.image_mime = d.mime;
                if (!d.pages.empty()) {
                    for (const auto& pg : d.pages)
                        ctx.image_b64.push_back(base64_encode(pg));
                } else {
                    ctx.image_b64.push_back(base64_encode(d.bytes));
                }
            } else {
                ctx.document = d.text;
                ctx.document_name = d.filename;
            }
        }
        if (body.contains("history") && body["history"].is_array()) {
            try { ctx.current_session = parse_history(body["history"]); } catch (...) {}
        }
        ctx.current_session.push_back({"user", user_msg});

        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");

        res.set_chunked_content_provider("text/event-stream",
            [this, ctx = std::move(ctx)](size_t, httplib::DataSink& sink) mutable {
                auto write_sse = [&](const std::string& data) {
                    std::string frame = "data: " + data + "\n\n";
                    sink.write(frame.c_str(), frame.size());
                };

                try {
                    engine_->generate(
                        ctx, cfg_.max_tokens, cfg_.temperature,
                        [&](const std::string& piece) {
                            write_sse(json{{"token", piece}}.dump());
                        },
                        [&]() {
                            write_sse(R"({"done":true})");
                        }
                    );
                } catch (const std::exception& e) {
                    write_sse(json{{"error", std::string(e.what())}}.dump());
                }

                sink.done();
                return true;
            }
        );
    });

    // ── POST /api/title — short AI-generated conversation title ──────────────────
    svr_.Post("/api/title", [this](const httplib::Request& req, httplib::Response& res) {
        if (!chat_limiter_.allow(client_ip(req))) {
            res.status = 429;
            res.set_content(json{{"error", "Rate limit exceeded."}}.dump(), "application/json");
            return;
        }
        if (!engine_) {
            res.status = 503;
            res.set_content(json{{"error", "No model available"}}.dump(), "application/json");
            return;
        }
        std::string message, reply;
        try {
            const auto body = json::parse(req.body);
            if (body.contains("message") && body["message"].is_string())
                message = body["message"].get<std::string>();
            if (body.contains("reply") && body["reply"].is_string())
                reply = body["reply"].get<std::string>();
        } catch (...) {}
        if (message.empty()) {
            res.status = 400;
            res.set_content(json{{"error", "Missing message"}}.dump(), "application/json");
            return;
        }

        ContextLayers ctx;
        ctx.system_prompt =
            "You write extremely short chat titles. Given a conversation, reply with a "
            "2-5 word title summarising its topic. Reply with ONLY the title text — no "
            "quotes, no trailing punctuation, no prefixes like 'Title:'.";
        std::string convo = "User: " + message;
        if (!reply.empty()) convo += "\nAssistant: " + reply;
        if (convo.size() > 1500) convo = convo.substr(0, 1500);
        ctx.current_session.push_back({"user", convo});

        std::string title;
        try {
            title = engine_->generate(ctx, 300, 0.3f);
        } catch (const std::exception& e) {
            res.status = 502;
            res.set_content(json{{"error", std::string("title generation failed: ") + e.what()}}.dump(),
                            "application/json");
            return;
        }

        // Sanitise: first line only, strip surrounding quotes/whitespace/trailing dot, cap length.
        if (const auto nl = title.find('\n'); nl != std::string::npos) title = title.substr(0, nl);
        const auto a = title.find_first_not_of(" \t\r\n\"'");
        const auto b = title.find_last_not_of(" \t\r\n\"'.");
        title = (a == std::string::npos) ? "" : title.substr(a, b - a + 1);
        if (title.size() > 60) title = title.substr(0, 60);

        res.set_content(json{{"title", title}}.dump(), "application/json");
    });

    // ── POST /api/transcribe — speech-to-text for voice input (Whisper) ──────────
    svr_.Post("/api/transcribe", [this](const httplib::Request& req, httplib::Response& res) {
        if (!media_limiter_.allow(client_ip(req))) {
            res.status = 429;
            res.set_content(json{{"error", "Too many requests — please wait a moment."}}.dump(), "application/json");
            return;
        }
        if (cfg_.groq_api_key.empty()) {
            res.status = 503;
            res.set_content(json{{"error", "Voice input requires the cloud backend."}}.dump(), "application/json");
            return;
        }
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json{{"error", "No audio in request"}}.dump(), "application/json");
            return;
        }
        const auto& f = req.get_file_value("file");
        std::string err;
        const std::string text = groq_transcribe(
            cfg_.groq_api_key, f.content,
            f.filename.empty() ? "audio.webm" : f.filename, cfg_.whisper_model, &err);
        if (text.empty()) {
            res.status = 422;
            res.set_content(json{{"error", "Could not transcribe audio: " + err}}.dump(), "application/json");
            return;
        }
        res.set_content(json{{"text", sanitize_utf8(text)}}.dump(), "application/json");
    });

    // ── POST /api/upload — accept any file type, route to the right processor ────
    svr_.Post("/api/upload", [this](const httplib::Request& req, httplib::Response& res) {
        if (!media_limiter_.allow(client_ip(req))) {
            res.status = 429;
            res.set_content(json{{"error", "Too many uploads — please wait a moment."}}.dump(), "application/json");
            return;
        }
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json{{"error", "No file field in request"}}.dump(), "application/json");
            return;
        }

        const auto& f = req.get_file_value("file");

        constexpr size_t MAX_BYTES = 25 * 1024 * 1024;   // larger to allow media
        if (f.content.size() > MAX_BYTES) {
            res.status = 413;
            res.set_content(json{{"error", "File exceeds 25 MB limit"}}.dump(), "application/json");
            return;
        }

        auto fail = [&](int code, const std::string& msg) {
            res.status = code;
            res.set_content(json{{"error", msg}}.dump(), "application/json");
        };
        auto ok = [&](const StoredDoc& d, const std::string& kind_str, int chars) {
            const std::string id = doc_store_.store_doc(d);
            res.set_content(json{{"doc_id", id}, {"kind", kind_str},
                                 {"filename", f.filename}, {"char_count", chars}}.dump(),
                            "application/json");
        };

        const FileKind kind = detect_file_kind(f.filename, f.content);

        switch (kind) {
        case FileKind::Image: {
            // Stored as raw bytes; sent to the vision model at chat time.
            const std::string mime = image_mime_for(lower_ext(f.filename));
            ok(StoredDoc{"image", "", f.content, mime, f.filename}, "image", 0);
            return;
        }
        case FileKind::Audio: {
            if (cfg_.groq_api_key.empty()) {
                fail(503, "Audio transcription requires the cloud (Groq) backend.");
                return;
            }
            std::string err;
            const std::string transcript = groq_transcribe(
                cfg_.groq_api_key, f.content, f.filename, cfg_.whisper_model, &err);
            if (transcript.empty()) {
                fail(422, "Could not transcribe audio: " + err);
                return;
            }
            const std::string clean = sanitize_utf8(transcript);
            ok(StoredDoc{"text", clean, "", "", f.filename}, "audio",
               static_cast<int>(clean.size()));
            return;
        }
        case FileKind::Pdf: {
            // Hybrid strategy:
            //  • Short PDFs (<= 3 pages, e.g. resumes/letters): render to images
            //    and read with the vision model — perfect fidelity on any font.
            //  • Long PDFs: extract text (complete coverage, cheap/fast).
            //  • Either way, fall back to the other method if the first yields nothing.
            const int pages = pdf_page_count(f.content);
            const bool prefer_vision = (pages >= 1 && pages <= 3);

            auto try_vision = [&](const char* kind_str, int cap) -> bool {
                std::vector<std::string> pgs = render_pdf_pages(f.content, cap);
                if (pgs.empty()) return false;
                StoredDoc d;
                d.kind = "image"; d.mime = "image/png"; d.filename = f.filename;
                d.pages = std::move(pgs);
                ok(d, kind_str, 0);
                return true;
            };
            auto try_text = [&]() -> bool {
                std::vector<char> data(f.content.begin(), f.content.end());
                const std::string text = sanitize_utf8(pdf_extract_text(data));
                if (text.empty()) return false;
                ok(StoredDoc{"text", text, "", "", f.filename, {}}, "pdf",
                   static_cast<int>(text.size()));
                return true;
            };

            if (prefer_vision) {
                if (try_vision("pdf-vision", 3)) return;
                if (try_text()) return;
            } else {
                if (try_text()) return;
                if (try_vision("pdf-scanned", 4)) return;
            }
            fail(422, "Could not read this PDF.");
            return;
        }
        case FileKind::Office: {
            const std::string text =
                sanitize_utf8(office_extract_text(f.content, lower_ext(f.filename)));
            if (text.empty()) {
                fail(422, "Could not extract text from this Office document.");
                return;
            }
            ok(StoredDoc{"text", text, "", "", f.filename}, "office",
               static_cast<int>(text.size()));
            return;
        }
        case FileKind::Video: {
            fail(415, "Video isn't supported yet — coming soon.");
            return;
        }
        case FileKind::Text:
        default: {
            const std::string clean = sanitize_utf8(f.content);
            ok(StoredDoc{"text", clean, "", "", f.filename}, "text",
               static_cast<int>(clean.size()));
            return;
        }
        }
    });

    // ── Optional server modules ────────────────────────────────────────────────
    // Mounted after the core API and before the static handler, so a module can
    // add routes but cannot shadow the built-in endpoints. Default build: none.
    {
        module_ctx_.engine       = engine_;
        module_ctx_.local_engine = local_engine_;
        module_ctx_.config       = &cfg_;
        module_ctx_.doc_store    = &doc_store_;
        module_ctx_.exe_dir      = cfg_.exe_dir;
        server_modules::register_routes(svr_, module_ctx_);

        const std::string active = server_modules::active_modules();
        if (!active.empty())
            std::cout << "[server] Modules: " << active << "\n";
    }

    // ── Serve static frontend files ────────────────────────────────────────────
    svr_.set_mount_point("/", cfg_.static_dir);

    svr_.Get(".*", [&](const httplib::Request&, httplib::Response& res) {
        const std::string index_path = cfg_.static_dir + "/index.html";
        std::ifstream f(index_path);
        if (f) {
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            res.set_content(content, "text/html");
        } else {
            res.status = 404;
            res.set_content("Not found", "text/plain");
        }
    });

    std::cout << "[server] Zathas AI listening on http://"
              << cfg_.host << ":" << cfg_.port << "\n";
    std::cout << "[server] Press Ctrl-C to stop.\n";

    svr_.listen(cfg_.host, cfg_.port);

    std::cout << "[server] Goodbye.\n";
}
