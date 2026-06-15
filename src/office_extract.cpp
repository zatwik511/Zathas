#include "office_extract.h"
#include <zlib.h>
#include <cstdint>
#include <vector>
#include <string>

// ── Little-endian readers ────────────────────────────────────────────────────
static uint16_t rd16(const std::string& s, size_t p) {
    if (p + 2 > s.size()) return 0;
    return static_cast<uint8_t>(s[p]) | (static_cast<uint16_t>(static_cast<uint8_t>(s[p + 1])) << 8);
}
static uint32_t rd32(const std::string& s, size_t p) {
    if (p + 4 > s.size()) return 0;
    return  static_cast<uint32_t>(static_cast<uint8_t>(s[p]))        |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[p + 1])) <<  8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[p + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[p + 3])) << 24);
}

// ── Raw DEFLATE inflate (zip entries are headerless deflate) ──────────────────
static std::string raw_inflate(const char* data, size_t len, size_t hint) {
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return {};
    zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(data));
    zs.avail_in = static_cast<uInt>(len);

    std::string out;
    out.reserve(hint ? hint : len * 4);
    char buf[32768];
    int ret;
    do {
        zs.next_out  = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (ret == Z_OK);
    inflateEnd(&zs);
    return out;
}

// ── Get a single zip entry's uncompressed bytes, by exact name or prefix ──────
// If `prefix` is true, returns the concatenation of all entries whose name
// starts with `name` (used for pptx slides).
static std::string zip_read(const std::string& zip, const std::string& name, bool prefix) {
    static const std::string EOCD("PK\x05\x06", 4);
    const size_t eocd = zip.rfind(EOCD);
    if (eocd == std::string::npos) return {};

    const uint16_t count     = rd16(zip, eocd + 10);
    const uint32_t cd_offset = rd32(zip, eocd + 16);

    std::string result;
    size_t p = cd_offset;
    for (uint16_t i = 0; i < count; ++i) {
        if (p + 46 > zip.size() || rd32(zip, p) != 0x02014b50) break;  // central hdr sig
        const uint16_t method     = rd16(zip, p + 10);
        const uint32_t comp_size   = rd32(zip, p + 20);
        const uint32_t uncomp_size = rd32(zip, p + 24);
        const uint16_t fn_len      = rd16(zip, p + 28);
        const uint16_t extra_len   = rd16(zip, p + 30);
        const uint16_t comment_len = rd16(zip, p + 32);
        const uint32_t lho         = rd32(zip, p + 42);
        const std::string entry    = zip.substr(p + 46, fn_len);

        const bool match = prefix ? (entry.rfind(name, 0) == 0) : (entry == name);
        if (match) {
            // Local header: filename/extra lengths live at offsets 26/28.
            const uint16_t lfn = rd16(zip, lho + 26);
            const uint16_t lex = rd16(zip, lho + 28);
            const size_t data_off = lho + 30 + lfn + lex;
            if (data_off + comp_size <= zip.size()) {
                std::string raw = (method == 8)
                    ? raw_inflate(zip.data() + data_off, comp_size, uncomp_size)
                    : zip.substr(data_off, comp_size);
                result += raw;
                if (!prefix) return result;
                result += "\n";
            }
        }
        p += 46 + fn_len + extra_len + comment_len;
    }
    return result;
}

// ── Strip XML tags -> plain text, decoding basic entities ─────────────────────
static std::string strip_xml(std::string xml) {
    // Turn structural breaks into newlines before stripping, for readability.
    static const char* breaks[] = {"</w:p>", "</a:p>", "<w:br/>", "<a:br/>",
                                   "</text:p>", "</tr>", "</si>"};
    for (const char* b : breaks) {
        size_t pos = 0;
        const std::string from = b;
        while ((pos = xml.find(from, pos)) != std::string::npos) {
            xml.replace(pos, from.size(), "\n");
            pos += 1;
        }
    }

    std::string out;
    out.reserve(xml.size() / 2);
    bool in_tag = false;
    for (char c : xml) {
        if (c == '<') in_tag = true;
        else if (c == '>') in_tag = false;
        else if (!in_tag) out += c;
    }

    // Decode the handful of XML entities that appear in text content.
    auto replace_all = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos) {
            out.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replace_all("&lt;", "<");   replace_all("&gt;", ">");
    replace_all("&quot;", "\""); replace_all("&apos;", "'");
    replace_all("&amp;", "&");
    return out;
}

std::string office_extract_text(const std::string& bytes, const std::string& ext) {
    std::string xml;
    if (ext == "docx") {
        xml = zip_read(bytes, "word/document.xml", false);
    } else if (ext == "pptx") {
        xml = zip_read(bytes, "ppt/slides/slide", true);   // all slideN.xml
    } else if (ext == "xlsx") {
        // Shared strings hold most human-readable cell text.
        xml = zip_read(bytes, "xl/sharedStrings.xml", false);
    } else {
        return {};
    }
    if (xml.empty()) return {};
    return strip_xml(xml);
}
