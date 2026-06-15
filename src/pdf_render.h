#pragma once
#include <string>
#include <vector>

// Render the first `max_pages` pages of a PDF to PNG images using the external
// `pdftoppm` tool (from poppler). Returns one raw-PNG-bytes string per page, in
// order. Empty vector if pdftoppm is unavailable or the PDF can't be rendered.
// Used for scanned / image-only PDFs that have no extractable text layer.
std::vector<std::string> render_pdf_pages(const std::string& pdf_bytes,
                                          int max_pages = 4);

// Return the number of pages in a PDF using the external `pdfinfo` tool, or -1
// if it cannot be determined (tool missing / not a valid PDF).
int pdf_page_count(const std::string& pdf_bytes);
