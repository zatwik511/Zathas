#include "pdf_extract.h"
#include <zlib.h>
#include <cctype>
#include <cstring>
#include <algorithm>

// Decompress a FlateDecode (zlib) stream
static std::string zlib_inflate(const char* data, size_t len) {
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return {};

    zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(data));
    zs.avail_in = static_cast<uInt>(len);

    std::string out;
    char buf[32768];
    int ret;
    do {
        zs.next_out  = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (ret == Z_OK);

    inflateEnd(&zs);
    return (ret == Z_STREAM_END) ? out : std::string{};
}

// Parse a PDF literal string starting at pos, where s[pos] == '('
static std::string read_literal(const std::string& s, size_t& pos) {
    ++pos;
    std::string r;
    int depth = 1;
    while (pos < s.size() && depth > 0) {
        const unsigned char c = s[pos];
        if (c == '\\' && pos + 1 < s.size()) {
            const char e = s[++pos]; ++pos;
            switch (e) {
                case 'n': r += '\n'; break;  case 'r': r += '\r'; break;
                case 't': r += '\t'; break;  case '(': r += '(';  break;
                case ')': r += ')';  break;  case '\\': r += '\\'; break;
                default:
                    if (e >= '0' && e <= '7') {
                        int v = e - '0';
                        for (int k = 0; k < 2 && pos < s.size() && s[pos] >= '0' && s[pos] <= '7'; ++k)
                            v = v * 8 + (s[pos++] - '0');
                        r += static_cast<char>(v);
                    } else { r += e; }
            }
        } else if (c == '(') { ++depth; r += c; ++pos; }
        else if (c == ')') { --depth; if (depth > 0) r += c; ++pos; }
        else { r += c; ++pos; }
    }
    return r;
}

// Parse a PDF hex string starting at pos, where s[pos] == '<'
static std::string read_hex(const std::string& s, size_t& pos) {
    ++pos;
    std::string r;
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    while (pos < s.size() && s[pos] != '>') {
        if (std::isspace((unsigned char)s[pos])) { ++pos; continue; }
        const char hi = s[pos++];
        const char lo = (pos < s.size() && s[pos] != '>') ? s[pos++] : '0';
        r += static_cast<char>(hv(hi) * 16 + hv(lo));
    }
    if (pos < s.size()) ++pos;
    return r;
}

// Check whether keyword tok starts at pos in s, followed by non-alphanumeric
static bool is_token(const std::string& s, size_t pos, const char* tok) {
    const size_t tlen = std::strlen(tok);
    if (pos + tlen > s.size()) return false;
    if (s.compare(pos, tlen, tok) != 0) return false;
    if (pos + tlen < s.size() && (std::isalnum((unsigned char)s[pos + tlen]) || s[pos + tlen] == '*'))
        return false;
    return true;
}

// Extract visible text from a decoded PDF content stream
static void scan_content(const std::string& cs, std::string& out) {
    const size_t n = cs.size();
    size_t i = 0;
    bool in_bt = false;

    auto skip_ws = [&] { while (i < n && std::isspace((unsigned char)cs[i])) ++i; };

    while (i < n) {
        skip_ws();
        if (i >= n) break;
        const size_t loop_start = i;   // guard: guarantee forward progress
        const char c = cs[i];

        if (c == '(' && in_bt) {
            const std::string s = read_literal(cs, i);
            skip_ws();
            if (is_token(cs, i, "Tj") || is_token(cs, i, "'") || is_token(cs, i, "\"")) {
                out += s;
                if (is_token(cs, i, "'")) out += '\n';
                while (i < n && !std::isspace((unsigned char)cs[i])) ++i;
            }
        } else if (c == '<' && i + 1 < n && cs[i + 1] != '<' && in_bt) {
            const std::string s = read_hex(cs, i);
            skip_ws();
            if (is_token(cs, i, "Tj") || is_token(cs, i, "'")) {
                out += s;
                while (i < n && !std::isspace((unsigned char)cs[i])) ++i;
            }
        } else if (c == '<' && i + 1 < n && cs[i + 1] == '<') {
            // Dictionary — skip to matching >>
            i += 2; int depth = 1;
            while (i < n && depth > 0) {
                if (i + 1 < n && cs[i] == '<' && cs[i+1] == '<') { depth++; i += 2; }
                else if (i + 1 < n && cs[i] == '>' && cs[i+1] == '>') { depth--; i += 2; }
                else ++i;
            }
        } else if (c == '[' && in_bt) {
            ++i;
            std::string arr;
            while (i < n && cs[i] != ']') {
                const size_t arr_start = i;
                skip_ws();
                if (i < n && cs[i] == '(') arr += read_literal(cs, i);
                else if (i < n && cs[i] == '<' && (i + 1 >= n || cs[i + 1] != '<')) arr += read_hex(cs, i);
                else { while (i < n && cs[i] != ']' && cs[i] != '(' && cs[i] != '<' && !std::isspace((unsigned char)cs[i])) ++i; }
                if (i == arr_start) ++i;   // guard: never stall on a stray char
            }
            if (i < n) ++i;
            skip_ws();
            if (is_token(cs, i, "TJ")) { out += arr; i += 2; }
        } else if (is_token(cs, i, "BT")) {
            in_bt = true;  i += 2;
        } else if (is_token(cs, i, "ET")) {
            in_bt = false; i += 2; out += '\n';
        } else if (is_token(cs, i, "T*")) {
            out += '\n'; i += 2;
        } else {
            // Skip unknown token
            while (i < n && !std::isspace((unsigned char)cs[i]) &&
                   cs[i] != '(' && cs[i] != '[' && cs[i] != '<') ++i;
        }

        // Safety net: if none of the branches advanced i — e.g. a stray '(',
        // '[' or '<' encountered outside a text block — force progress so the
        // scanner can never spin forever (which would hang the upload).
        if (i == loop_start) ++i;
    }
}

std::string pdf_extract_text(const std::vector<char>& raw) {
    const std::string pdf(raw.begin(), raw.end());
    std::string result;

    size_t pos = 0;
    while (pos < pdf.size()) {
        const size_t sk = pdf.find("stream", pos);
        if (sk == std::string::npos) break;

        // Ensure "stream" is not part of "endstream"
        if (sk > 0 && !std::isspace((unsigned char)pdf[sk - 1])) { pos = sk + 1; continue; }

        // Data starts after the newline following "stream"
        size_t ds = sk + 6;
        if (ds < pdf.size() && pdf[ds] == '\r') ++ds;
        if (ds < pdf.size() && pdf[ds] == '\n') ++ds;

        const size_t es = pdf.find("endstream", ds);
        if (es == std::string::npos) break;

        // Trim trailing newline before endstream
        size_t de = es;
        if (de > ds && pdf[de - 1] == '\n') --de;
        if (de > ds && pdf[de - 1] == '\r') --de;

        // Search backwards for this stream's dictionary
        const size_t dict_from = sk > 2048 ? sk - 2048 : 0;
        const std::string dict = pdf.substr(dict_from, sk - dict_from);

        const bool has_flat =
            dict.rfind("/FlateDecode") != std::string::npos ||
            dict.rfind("/Fl ")         != std::string::npos ||
            dict.rfind("/Fl\n")        != std::string::npos;

        // Skip obvious binary streams (images, embedded fonts)
        const bool is_text_stream =
            dict.rfind("/BitsPerComponent") == std::string::npos &&
            dict.rfind("/ColorSpace")       == std::string::npos;

        if (is_text_stream && de > ds) {
            std::string content;
            if (has_flat)
                content = zlib_inflate(pdf.data() + ds, de - ds);
            else
                content.assign(pdf.data() + ds, de - ds);

            if (!content.empty())
                scan_content(content, result);
        }

        pos = es + 9;
    }

    // Normalise whitespace, strip control bytes, and guarantee valid UTF-8.
    // PDF literal-string text is single-byte (WinAnsi/Latin-1) font-encoded, so
    // any byte >= 0x80 is transcoded as Latin-1 -> UTF-8 (a 2-byte sequence).
    // This keeps the text readable AND ensures the output is always valid UTF-8,
    // so it can be placed into a JSON request/response without throwing
    // (json type_error.316 "invalid UTF-8 byte").
    std::string clean;
    clean.reserve(result.size());
    bool prev_ws = false;
    for (const unsigned char ch : result) {
        if (ch == '\n') { clean += '\n'; prev_ws = false; }
        else if (ch < 0x20 || ch == 0x7F) { if (!prev_ws) { clean += ' '; prev_ws = true; } }
        else if (ch < 0x80) { clean += static_cast<char>(ch); prev_ws = false; }
        else {
            clean += static_cast<char>(0xC0 | (ch >> 6));
            clean += static_cast<char>(0x80 | (ch & 0x3F));
            prev_ws = false;
        }
    }

    return clean;
}
