#pragma once
#include "inference.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

// ── Streaming response parser ─────────────────────────────────────────────────
//
// Turns a provider's Server-Sent Events stream into visible answer text. Two
// things make this less trivial than it looks, and both have caused production
// bugs:
//
//   1. Some models wrap their chain of thought in <think>...</think> inside the
//      normal content field. That has to be stripped HERE rather than in the UI,
//      because anything forwarded to the client is kept in message state and
//      replayed as history on later turns, so hiding it at render time would
//      still pay input tokens for it again and again. The tags can also be split
//      across chunk boundaries, so partial tags are held back.
//
//   2. Other models put their reasoning in a SEPARATE field and leave content
//      empty. If such a model exhausts its token budget while reasoning, nothing
//      visible ever arrives. Reporting that as an empty reply is useless, so the
//      parser tracks whether reasoning was seen and says which happened.
//
// Header-only so it can be unit tested without linking the HTTP client.

namespace sse {

inline constexpr const char* kThinkOpen  = "<think>";
inline constexpr const char* kThinkClose = "</think>";

// Shown when the model produced nothing visible at all. Exposed so callers can
// recognise it: it is a diagnostic, and must never be mistaken for real output
// (a chat title made out of this string, for instance).
inline constexpr const char* kRanOutWhileReasoning =
    "(I ran out of room working through that one and didn't reach an answer. "
    "Please try again, or ask for something more specific.)";
inline constexpr const char* kNoOutput =
    "(The model returned an empty response. Please try again.)";

// Longest suffix of `s` that is a proper prefix of `tag`, so a tag split across
// two chunks is not emitted as literal text.
inline size_t partial_tag_suffix(const std::string& s, const char* tag)
{
    const size_t tag_len = std::strlen(tag);
    for (size_t n = std::min(s.size(), tag_len - 1); n > 0; --n)
        if (s.compare(s.size() - n, n, tag, n) == 0) return n;
    return 0;
}

class Parser {
public:
    // The callback is stored by value. Holding a reference would dangle for any
    // caller that passes a temporary, and the cost of copying a std::function
    // once per request is irrelevant next to a network round trip.
    explicit Parser(TokenCallback cb) : on_token_(std::move(cb)) {}

    const std::string& text() const { return full_; }
    bool done() const { return done_; }
    bool saw_reasoning() const { return saw_reasoning_; }
    bool produced_visible_output() const { return seen_visible_; }

    // Feed raw bytes from the stream. Returns false to abort the connection.
    bool feed(const char* data, size_t len)
    {
        buf_.append(data, len);
        size_t pos;
        while ((pos = buf_.find("\n\n")) != std::string::npos) {
            const std::string event = buf_.substr(0, pos);
            buf_ = buf_.substr(pos + 2);

            if (event.size() < 6 || event.compare(0, 6, "data: ") != 0) continue;
            const std::string payload = event.substr(6);
            if (payload == "[DONE]") { done_ = true; continue; }

            try {
                const auto j = nlohmann::json::parse(payload);
                if (!j.contains("choices") || j["choices"].empty()) continue;
                const auto& delta = j["choices"][0].at("delta");

                // Reasoning is never forwarded, only noted, so that a reply that
                // is empty *because the model spent its budget thinking* can be
                // reported as such.
                for (const char* key : {"reasoning", "reasoning_content"})
                    if (delta.contains(key) && delta[key].is_string() &&
                        !delta[key].get<std::string>().empty())
                        saw_reasoning_ = true;

                if (delta.contains("content") && delta["content"].is_string()) {
                    const std::string tok = delta["content"].get<std::string>();
                    if (!tok.empty()) push(tok);
                }
            } catch (...) { /* skip malformed chunk */ }
        }
        return true;
    }

    // Flush anything still held once the stream ends.
    void finish()
    {
        if (!in_think_ && !hold_.empty()) emit(hold_);
        hold_.clear();

        // Suppressing an unterminated <think> block is correct; showing the user
        // nothing is not. Say which of the two silences this was.
        if (!seen_visible_) {
            const std::string msg =
                (in_think_ || saw_reasoning_) ? kRanOutWhileReasoning : kNoOutput;
            seen_visible_ = true;
            full_ += msg;
            if (on_token_) on_token_(msg);
        }
    }

private:
    void emit(const std::string& piece)
    {
        std::string out = piece;
        if (!seen_visible_) {
            const size_t i = out.find_first_not_of(" \t\r\n");
            if (i == std::string::npos) return;   // still only leading whitespace
            out.erase(0, i);
            seen_visible_ = true;
        }
        full_ += out;
        if (on_token_) on_token_(out);
    }

    // Emit only the text outside <think> blocks.
    void push(const std::string& tok)
    {
        hold_ += tok;
        for (;;) {
            const char* tag = in_think_ ? kThinkClose : kThinkOpen;
            const size_t p = hold_.find(tag);
            if (p != std::string::npos) {
                if (!in_think_ && p > 0) emit(hold_.substr(0, p));
                hold_.erase(0, p + std::strlen(tag));
                in_think_ = !in_think_;
                continue;
            }
            const size_t keep = partial_tag_suffix(hold_, tag);
            if (hold_.size() > keep) {
                if (!in_think_) emit(hold_.substr(0, hold_.size() - keep));
                hold_.erase(0, hold_.size() - keep);
            }
            return;
        }
    }

    std::string          buf_;            // unparsed bytes
    std::string          hold_;           // awaiting tag resolution
    std::string          full_;
    TokenCallback        on_token_;
    bool                 done_          = false;
    bool                 in_think_      = false;
    bool                 seen_visible_  = false;
    bool                 saw_reasoning_ = false;
};

}   // namespace sse
