#pragma once
#include <string>

// Extract plain text from an OOXML Office file (.docx/.xlsx/.pptx).
// `ext` is the lowercase extension ("docx", "xlsx", "pptx") used to pick which
// internal XML parts to read. Returns empty string on failure.
std::string office_extract_text(const std::string& bytes, const std::string& ext);
