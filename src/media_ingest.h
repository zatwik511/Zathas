#pragma once
#include <string>
#include <cctype>

// ── File-type classification ────────────────────────────────────────────────────

enum class FileKind { Text, Image, Audio, Video, Pdf, Office, Unknown };

inline std::string lower_ext(const std::string& filename) {
    const auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string e = filename.substr(dot + 1);
    for (auto& c : e) c = static_cast<char>(std::tolower((unsigned char)c));
    return e;
}

inline std::string image_mime_for(const std::string& ext) {
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")  return "image/gif";
    if (ext == "webp") return "image/webp";
    if (ext == "bmp")  return "image/bmp";
    return "image/png";
}

// Classify by extension first, then magic bytes as a fallback.
inline FileKind detect_file_kind(const std::string& filename, const std::string& content) {
    const std::string ext = lower_ext(filename);

    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
        ext == "webp" || ext == "bmp")
        return FileKind::Image;
    if (ext == "mp3" || ext == "wav" || ext == "m4a" || ext == "ogg" ||
        ext == "flac" || ext == "mpga" || ext == "aac" || ext == "opus")
        return FileKind::Audio;
    if (ext == "mp4" || ext == "mov" || ext == "mkv" || ext == "avi" || ext == "webm")
        return FileKind::Video;
    if (ext == "pdf")  return FileKind::Pdf;
    if (ext == "docx" || ext == "xlsx" || ext == "pptx") return FileKind::Office;

    // Magic-byte fallback for files with no/unknown extension.
    if (content.size() >= 4) {
        const unsigned char b0 = content[0], b1 = content[1],
                            b2 = content[2], b3 = content[3];
        if (content.compare(0, 4, "%PDF") == 0)                 return FileKind::Pdf;
        if (b0 == 0x89 && b1 == 0x50 && b2 == 0x4E && b3 == 0x47) return FileKind::Image; // PNG
        if (b0 == 0xFF && b1 == 0xD8 && b2 == 0xFF)              return FileKind::Image; // JPEG
        if (b0 == 0x47 && b1 == 0x49 && b2 == 0x46)              return FileKind::Image; // GIF
        if (b0 == 0x50 && b1 == 0x4B && b2 == 0x03 && b3 == 0x04) return FileKind::Office; // ZIP/Office
    }

    return FileKind::Text;
}

// ── UTF-8 sanitiser ─────────────────────────────────────────────────────────────
// Preserves already-valid UTF-8 (so genuine multibyte text/emoji survive), and
// transcodes any stray byte as Latin-1 -> UTF-8. Guarantees valid UTF-8 output so
// the text can be placed into a JSON request/response without throwing.
inline std::string sanitize_utf8(const std::string& in) {
    std::string out;
    out.reserve(in.size() + in.size() / 8);
    const size_t n = in.size();
    auto cont = [&](size_t k) {
        return k < n && (static_cast<unsigned char>(in[k]) & 0xC0) == 0x80;
    };
    size_t i = 0;
    while (i < n) {
        const unsigned char c = in[i];
        if (c < 0x80) {
            out += static_cast<char>(c); ++i;
        } else if ((c & 0xE0) == 0xC0 && cont(i + 1)) {
            out.append(in, i, 2); i += 2;
        } else if ((c & 0xF0) == 0xE0 && cont(i + 1) && cont(i + 2)) {
            out.append(in, i, 3); i += 3;
        } else if ((c & 0xF8) == 0xF0 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
            out.append(in, i, 4); i += 4;
        } else {
            // Invalid byte — treat as Latin-1 and emit a 2-byte UTF-8 sequence.
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
            ++i;
        }
    }
    return out;
}

// ── Base64 ───────────────────────────────────────────────────────────────────────
inline std::string base64_encode(const std::string& data) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    const size_t n = data.size();
    size_t i = 0;
    while (i + 3 <= n) {
        const unsigned v = (static_cast<unsigned char>(data[i])     << 16) |
                           (static_cast<unsigned char>(data[i + 1]) <<  8) |
                            static_cast<unsigned char>(data[i + 2]);
        out += tbl[(v >> 18) & 0x3F]; out += tbl[(v >> 12) & 0x3F];
        out += tbl[(v >>  6) & 0x3F]; out += tbl[v & 0x3F];
        i += 3;
    }
    if (n - i == 1) {
        const unsigned v = static_cast<unsigned char>(data[i]) << 16;
        out += tbl[(v >> 18) & 0x3F]; out += tbl[(v >> 12) & 0x3F];
        out += '='; out += '=';
    } else if (n - i == 2) {
        const unsigned v = (static_cast<unsigned char>(data[i])     << 16) |
                           (static_cast<unsigned char>(data[i + 1]) <<  8);
        out += tbl[(v >> 18) & 0x3F]; out += tbl[(v >> 12) & 0x3F];
        out += tbl[(v >>  6) & 0x3F]; out += '=';
    }
    return out;
}
