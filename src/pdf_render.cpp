#include "pdf_render.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace fs = std::filesystem;

static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

std::vector<std::string> render_pdf_pages(const std::string& pdf_bytes, int max_pages) {
    std::vector<std::string> pages;
    std::error_code ec;

    const fs::path tmp = fs::temp_directory_path(ec);
    if (ec) return pages;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string base = "zathas_" + std::to_string(stamp);
    const fs::path in     = tmp / (base + ".pdf");
    const fs::path prefix = tmp / (base + "_pg");

    {
        std::ofstream f(in, std::ios::binary);
        if (!f) return pages;
        f.write(pdf_bytes.data(), static_cast<std::streamsize>(pdf_bytes.size()));
    }

    // Render pages 1..max_pages to PNG at 150 DPI. pdftoppm must be on PATH.
    std::string cmd =
        "pdftoppm -png -r 150 -f 1 -l " + std::to_string(max_pages) +
        " \"" + in.string() + "\" \"" + prefix.string() + "\"";
#ifdef _WIN32
    cmd = "cmd /c " + cmd + " >NUL 2>&1";
#else
    cmd += " >/dev/null 2>&1";
#endif
    std::system(cmd.c_str());

    // pdftoppm names outputs "<prefix>-N.png" with zero-padding based on page
    // count, so probe a few padding widths per page.
    for (int pg = 1; pg <= max_pages; ++pg) {
        const std::string n = std::to_string(pg);
        fs::path found;
        for (const std::string& cand : {
                 prefix.string() + "-" + n + ".png",
                 prefix.string() + "-0" + n + ".png",
                 prefix.string() + "-00" + n + ".png" }) {
            if (fs::exists(cand)) { found = cand; break; }
        }
        if (found.empty()) break;   // no more pages produced

        std::string data = read_file(found);
        fs::remove(found, ec);
        if (data.empty()) break;
        pages.push_back(std::move(data));
    }

    fs::remove(in, ec);
    return pages;
}

int pdf_page_count(const std::string& pdf_bytes) {
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec);
    if (ec) return -1;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path in = tmp / ("zathas_pc_" + std::to_string(stamp) + ".pdf");
    {
        std::ofstream f(in, std::ios::binary);
        if (!f) return -1;
        f.write(pdf_bytes.data(), static_cast<std::streamsize>(pdf_bytes.size()));
    }

    std::string cmd = "pdfinfo \"" + in.string() + "\"";
    int count = -1;
#ifdef _WIN32
    cmd += " 2>NUL";
    FILE* p = _popen(cmd.c_str(), "r");
#else
    cmd += " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
#endif
    if (p) {
        char line[512];
        while (fgets(line, sizeof(line), p)) {
            if (std::strncmp(line, "Pages:", 6) == 0) {
                count = std::atoi(line + 6);
                break;
            }
        }
#ifdef _WIN32
        _pclose(p);
#else
        pclose(p);
#endif
    }

    fs::remove(in, ec);
    return count;
}
