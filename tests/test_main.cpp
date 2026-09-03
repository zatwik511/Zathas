// Tests for the parts that have actually broken in production.
//
// No test framework: this project builds its own HTTP stack, so a hundred lines
// of assertions beat a dependency. Run with `ctest --test-dir build` or execute
// the binary directly.

#include "sse_parser.h"
#include "title_util.h"
#include "rate_limit.h"
#include "inference.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool cond, const std::string& what)
{
    ++g_checks;
    if (cond) return;
    ++g_failures;
    std::cerr << "  FAIL: " << what << "\n";
}

void check_eq(const std::string& got, const std::string& want, const std::string& what)
{
    ++g_checks;
    if (got == want) return;
    ++g_failures;
    std::cerr << "  FAIL: " << what << "\n"
              << "        want: [" << want << "]\n"
              << "        got:  [" << got  << "]\n";
}

// Builds one SSE event carrying an OpenAI-style delta.
std::string chunk(const std::string& field, const std::string& value)
{
    nlohmann::json j = {{"choices", nlohmann::json::array({
        {{"delta", {{field, value}}}}
    })}};
    return "data: " + j.dump() + "\n\n";
}

void feed(sse::Parser& p, const std::string& s)
{
    p.feed(s.data(), s.size());
}

// ── SSE parser ────────────────────────────────────────────────────────────────

void test_plain_content()
{
    std::cout << "sse: plain content\n";
    sse::Parser p{{}};
    feed(p, chunk("content", "Hello"));
    feed(p, chunk("content", " world"));
    p.finish();
    check_eq(p.text(), "Hello world", "plain content is passed through");
}

void test_think_stripped()
{
    std::cout << "sse: <think> blocks are stripped\n";
    sse::Parser p{{}};
    feed(p, chunk("content", "<think>reasoning here</think>Answer"));
    p.finish();
    check_eq(p.text(), "Answer", "reasoning inside <think> is removed");
}

void test_think_split_across_chunks()
{
    std::cout << "sse: a tag split across chunks is not leaked\n";
    // The regression this guards: "<think>" arriving in pieces used to be
    // emitted as literal text before the tag could be recognised.
    sse::Parser p{{}};
    feed(p, chunk("content", "<thi"));
    feed(p, chunk("content", "nk>hidden</thi"));
    feed(p, chunk("content", "nk>Visible"));
    p.finish();
    check_eq(p.text(), "Visible", "split tags are buffered, not emitted");
}

void test_unterminated_think()
{
    std::cout << "sse: budget exhausted mid-thought\n";
    // No </think> ever arrives, so everything is suppressed. Suppressing it is
    // correct; showing the user a blank reply is not.
    sse::Parser p{{}};
    feed(p, chunk("content", "<think>thinking and thinking"));
    p.finish();
    check(!p.text().empty(), "an unterminated think block still yields a message");
    check_eq(p.text(), sse::kRanOutWhileReasoning, "and it explains what happened");
}

void test_separate_reasoning_field()
{
    std::cout << "sse: reasoning in a separate field\n";
    // gpt-oss and friends put reasoning in its own field and leave content
    // empty. This is what silently broke title generation: the parser never
    // read that field, so it could not tell "thought too long" from "nothing".
    sse::Parser p{{}};
    feed(p, chunk("reasoning", "deliberating at length"));
    p.finish();
    check(p.saw_reasoning(), "reasoning field is noticed");
    check_eq(p.text(), sse::kRanOutWhileReasoning,
             "empty content plus reasoning is reported as running out of room");
}

void test_reasoning_then_content()
{
    std::cout << "sse: reasoning followed by a real answer\n";
    sse::Parser p{{}};
    feed(p, chunk("reasoning", "thinking"));
    feed(p, chunk("content",   "42"));
    p.finish();
    check_eq(p.text(), "42", "reasoning is not forwarded, content is");
    check(p.saw_reasoning(), "reasoning was still observed");
}

void test_truly_empty_response()
{
    std::cout << "sse: no content and no reasoning\n";
    sse::Parser p{{}};
    p.finish();
    check_eq(p.text(), sse::kNoOutput, "an empty stream is reported distinctly");
}

void test_malformed_chunks_ignored()
{
    std::cout << "sse: malformed chunks do not derail the stream\n";
    sse::Parser p{{}};
    feed(p, "data: {not json at all}\n\n");
    feed(p, "data: {\"choices\":[]}\n\n");
    feed(p, chunk("content", "Fine"));
    feed(p, "data: [DONE]\n\n");
    p.finish();
    check_eq(p.text(), "Fine", "valid content still arrives");
    check(p.done(), "[DONE] is recognised");
}

void test_streaming_callback_order()
{
    std::cout << "sse: tokens reach the callback in order\n";
    std::vector<std::string> got;
    TokenCallback cb = [&](const std::string& t) { got.push_back(t); };
    sse::Parser p{cb};
    feed(p, chunk("content", "a"));
    feed(p, chunk("content", "b"));
    p.finish();
    check(got.size() == 2 && got[0] == "a" && got[1] == "b",
          "callback receives each token once, in order");
}

// ── Title handling ────────────────────────────────────────────────────────────

void test_title_sanitize()
{
    std::cout << "title: sanitising model output\n";
    check_eq(title::sanitize("\"Quoted Title\""), "Quoted Title", "surrounding quotes removed");
    check_eq(title::sanitize("Trailing period."),  "Trailing period", "trailing period removed");
    check_eq(title::sanitize("  Padded  "),        "Padded",          "whitespace trimmed");
    check_eq(title::sanitize("First line\nSecond"),"First line",      "only the first line kept");
    check(title::sanitize(std::string(200, 'x')).size() <= title::kMaxChars,
          "over-long titles are capped");
}

void test_title_rejects_placeholder()
{
    std::cout << "title: parser diagnostics never become titles\n";
    // The exact production bug: every new chat was named after the parser's
    // "ran out of room" message.
    const std::string sanitised = title::sanitize(sse::kRanOutWhileReasoning);
    check(title::is_placeholder(sanitised),
          "the sanitised 'ran out of room' text is rejected");
    check(title::is_placeholder(title::sanitize(sse::kNoOutput)),
          "the empty-response text is rejected");
    check(!title::is_placeholder("Capital of France"),
          "a real title is accepted");
    check(!title::is_placeholder("Rate limiting in C++"),
          "a real title containing ordinary words is accepted");
}

// ── Limits ────────────────────────────────────────────────────────────────────

void test_rate_limiter()
{
    std::cout << "limits: per-IP rate limiting\n";
    RateLimiter rl(3, 60);
    check(rl.allow("1.2.3.4"), "first request allowed");
    check(rl.allow("1.2.3.4"), "second allowed");
    check(rl.allow("1.2.3.4"), "third allowed");
    check(!rl.allow("1.2.3.4"), "fourth blocked");
    check(rl.allow("5.6.7.8"), "a different caller is unaffected");
}

void test_daily_cap()
{
    std::cout << "limits: service-wide daily cap\n";
    DailyCap cap(2);
    check(cap.allow(),  "first request within budget");
    check(cap.allow(),  "second within budget");
    check(!cap.allow(), "third refused");
    check(cap.used() == 2, "only successful charges are counted");
    check(cap.limit() == 2, "limit is reported");
}

// ── Provider errors ───────────────────────────────────────────────────────────

void test_provider_error_classification()
{
    std::cout << "errors: retry classification\n";
    check(ProviderError(404, "gone").worth_retrying_elsewhere(),
          "a retired model is worth trying elsewhere");
    check(ProviderError(429, "slow down").worth_retrying_elsewhere(),
          "throttling is worth trying elsewhere");
    check(ProviderError(503, "down").worth_retrying_elsewhere(),
          "a provider outage is worth trying elsewhere");
    check(ProviderError(0, "no route").worth_retrying_elsewhere(),
          "an unreachable provider is worth trying elsewhere");
    check(!ProviderError(400, "bad request").worth_retrying_elsewhere(),
          "a malformed request would fail identically anywhere");
    check(!ProviderError(401, "bad key").worth_retrying_elsewhere(),
          "a rejected key is not a transient condition");
}

}   // namespace

int main()
{
    test_plain_content();
    test_think_stripped();
    test_think_split_across_chunks();
    test_unterminated_think();
    test_separate_reasoning_field();
    test_reasoning_then_content();
    test_truly_empty_response();
    test_malformed_chunks_ignored();
    test_streaming_callback_order();

    test_title_sanitize();
    test_title_rejects_placeholder();

    test_rate_limiter();
    test_daily_cap();

    test_provider_error_classification();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures) std::cerr << g_failures << " FAILED\n";
    return g_failures ? 1 : 0;
}
