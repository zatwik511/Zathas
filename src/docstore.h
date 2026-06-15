#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>

// A stored upload. Depending on `kind`, either `text` (for anything that became
// text — plain text, code, PDF/Office extraction, audio transcript) or `bytes`
// (raw binary, e.g. an image to send to a vision model) carries the payload.
struct StoredDoc {
    std::string kind;      // "text" | "image"
    std::string text;      // extracted / transcribed text  (kind == "text")
    std::string bytes;     // raw binary content            (kind == "image", single)
    std::string mime;      // e.g. "image/png", "image/jpeg"
    std::string filename;  // original filename (for display / hints)
    std::vector<std::string> pages;  // multiple image pages (kind == "image", e.g. scanned PDF)
};

class DocStore {
public:
    // Back-compat helper: store a plain-text document.
    std::string store(const std::string& content) {
        return store_doc(StoredDoc{"text", content, "", "", ""});
    }

    // Store a typed document; returns its id.
    std::string store_doc(const StoredDoc& d) {
        const std::string id = make_id();
        std::lock_guard<std::mutex> lk(mu_);
        docs_[id] = d;
        return id;
    }

    // Back-compat helper: get a document's text (empty if missing or non-text).
    std::string get(const std::string& id) const {
        std::lock_guard<std::mutex> lk(mu_);
        const auto it = docs_.find(id);
        return it != docs_.end() ? it->second.text : "";
    }

    // Get the full typed document (empty StoredDoc if missing).
    StoredDoc get_doc(const std::string& id) const {
        std::lock_guard<std::mutex> lk(mu_);
        const auto it = docs_.find(id);
        return it != docs_.end() ? it->second : StoredDoc{};
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, StoredDoc> docs_;

    static std::string make_id() {
        using namespace std::chrono;
        const auto ns = duration_cast<nanoseconds>(
            system_clock::now().time_since_epoch()).count();
        std::ostringstream ss;
        ss << std::hex << ns;
        return ss.str();
    }
};
