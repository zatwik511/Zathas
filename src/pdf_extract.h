#pragma once
#include <string>
#include <vector>

// Extract plain text from a PDF file.
// Returns empty string on failure or for image-only PDFs.
std::string pdf_extract_text(const std::vector<char>& raw);
