#pragma once
#include "sse_parser.h"

#include <string>

// Helpers for turning a model's reply into a conversation title. Separated from
// the route so the rules can be tested directly.

namespace title {

inline constexpr size_t kMaxChars = 60;

// First line only, surrounding quotes and whitespace and a trailing period
// removed, capped in length. Models add these embellishments no matter how
// firmly the prompt asks them not to.
inline std::string sanitize(std::string t)
{
    if (const auto nl = t.find('\n'); nl != std::string::npos) t = t.substr(0, nl);

    const auto a = t.find_first_not_of(" \t\r\n\"'");
    const auto b = t.find_last_not_of(" \t\r\n\"'.");
    t = (a == std::string::npos) ? std::string{} : t.substr(a, b - a + 1);

    if (t.size() > kMaxChars) t = t.substr(0, kMaxChars);
    return t;
}

// True when the text is one of the stream parser's diagnostics rather than
// something the model actually wrote.
//
// This matters because those strings arrive through the same path as real
// output: a reasoning model that spends its whole budget thinking produces the
// placeholder, and without this check it would be saved as the conversation's
// name. That is exactly what happened in production - every new chat was titled
// "I ran out of room working through that one and didn't reach...".
inline bool is_placeholder(const std::string& t)
{
    if (t.empty()) return false;
    for (const char* p : {sse::kRanOutWhileReasoning, sse::kNoOutput}) {
        const std::string placeholder = sanitize(p);
        // Compare against the sanitised and truncated form too, since that is
        // what would actually reach storage.
        if (t == p || t == placeholder) return true;
        if (!placeholder.empty() && t.rfind(placeholder.substr(0, t.size()), 0) == 0 &&
            t.size() >= 20)
            return true;
    }
    // Any parenthesised whole-string diagnostic is treated the same way: real
    // titles are not wrapped in brackets.
    return t.front() == '(' && t.back() == ')';
}

}   // namespace title
