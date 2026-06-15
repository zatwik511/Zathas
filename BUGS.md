# Bug Tracker

## Resolved
1. ~~File upload not working~~ — verified working for text and PDF (POST /api/upload returns doc_id).
2. ~~Only PDFs can be uploaded~~ — server accepts plain text too (magic-byte detection in server.cpp); client `accept` allows `.txt`.
3. ~~No export button on public side~~ — Export button present in App.jsx.
4. ~~PDF upload hangs at 100%~~ — `scan_content()` in pdf_extract.cpp could infinite-loop on a stray `(`, `[`, or `<` outside a text block, hanging the server thread and freezing the upload at 100%. Fixed by adding forward-progress guards so the parser can never stall. Verified against a PDF that previously hung (now extracts 6206 chars in ~0.2s).
5. ~~Chatting with a PDF crashes: "invalid UTF-8 byte" (json type_error.316)~~ — extracted PDF text contained raw Latin-1 bytes (e.g. 0xB6) that are invalid UTF-8, so serializing the chat request to JSON threw. Fixed by transcoding high bytes (Latin-1 -> UTF-8) in pdf_extract.cpp's cleanup pass. Verified: upload + chat-with-document now streams normally.

## Open / known limitations
- Image-based or encrypted PDFs return HTTP 422 (no extractable text). The frontend currently shows no message on a non-200 upload — the progress pill just disappears. Minor UX gap, not a hang.
- The lightweight from-scratch PDF parser does not decode font CMaps, so text from complex/multi-font PDFs (e.g. spreadsheets exported to PDF) can come out garbled. Simple text PDFs extract cleanly. Plain `.txt` uploads that aren't UTF-8 could still hit the same serialization issue (not yet hardened).
