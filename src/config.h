#pragma once
#include <cstddef>

// ── Defaults ──────────────────────────────────────────────────────────────────
//
// Single source of truth. Every built-in default lives here and nowhere else —
// changing one is a one-line edit. Runtime overrides come from .env or CLI
// flags (see main.cpp); this file only says what happens when neither is given.
//
// Anything below that names a hosted model is liable to be retired by the
// provider without warning, which is why each is also overridable from .env.

namespace config {

// ── Cloud provider (Groq) ─────────────────────────────────────────────────────
inline constexpr const char* kGroqHost     = "api.groq.com";
inline constexpr const char* kCloudModel   = "openai/gpt-oss-120b";
inline constexpr const char* kVisionModel  = "qwen/qwen3.6-27b";
inline constexpr const char* kWhisperModel = "whisper-large-v3";


// ── HTTP server ───────────────────────────────────────────────────────────────
inline constexpr const char* kHost      = "0.0.0.0";
inline constexpr int         kPort      = 8080;
inline constexpr const char* kStaticDir = "./frontend";

// ── Generation ────────────────────────────────────────────────────────────────
inline constexpr int   kMaxTokens   = 512;
inline constexpr float kTemperature = 0.7f;
inline constexpr int   kContextSize = 4096;
inline constexpr int   kThreads     = 4;
inline constexpr int   kGpuLayers   = 0;

// A reasoning model spends tokens before it emits anything visible. Vision
// requests therefore get a floor well above kMaxTokens: at the normal budget
// the model runs out mid-thought and the reply arrives empty.
inline constexpr int kVisionMinTokens = 1536;

// Same reason, for the short utility call that names a conversation.
inline constexpr int kTitleMaxTokens = 300;

// ── Abuse limits ──────────────────────────────────────────────────────────────
inline constexpr int    kChatPerMinute   = 25;   // per IP, /api/chat + /api/title
inline constexpr int    kMediaPerMinute  = 15;   // per IP, /api/upload + /api/transcribe
inline constexpr int    kRateWindowSecs  = 60;
inline constexpr size_t kMaxPayloadBytes = 10 * 1024 * 1024 + 4096;

}   // namespace config
