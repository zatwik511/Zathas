# Zathas

A modern, multimodal AI chatbot with a **from-scratch C++ backend** and a sci-fi React frontend. The server is a single compiled binary that streams responses, ingests almost any file type, and serves the UI — no Python or Node.js at runtime.

Chat runs on fast cloud inference by default, with an optional local [llama.cpp](https://github.com/ggml-org/llama.cpp) engine baked in.

---

## Features

- **Streaming chat** over Server-Sent Events with live markdown + syntax-highlighted code
- **Multimodal uploads** — the assistant can actually read:
  - 🖼️ **Images** (via a vision model)
  - 🎙️ **Audio** (transcribed with Whisper)
  - 📄 **PDFs** — text PDFs by extraction, scanned PDFs rendered to images and read by vision
  - 📝 **Office docs** (`.docx` / `.xlsx` / `.pptx`) and 30+ text/code formats
- **Conversation history** with AI-generated titles (stored client-side in the browser)
- **Voice input**, drag-and-drop + paste-to-upload, per-code-block copy
- **Stop / regenerate / edit-and-resend** controls
- **Per-IP rate limiting** to protect cost-incurring endpoints

---

## Architecture

```
browser ──HTTP──▶ cpp-httplib server (C++17)
                    ├── POST /api/chat       → streaming chat (SSE)
                    ├── POST /api/upload     → file ingestion (text/PDF/office/image/audio)
                    ├── POST /api/transcribe → voice → text (Whisper)
                    ├── POST /api/title      → short AI chat titles
                    ├── GET  /health
                    └── GET  /*              → React frontend
```

Inference is provided by a cloud API by default, or a local llama.cpp model when configured.

---

## Prerequisites

| Tool | Notes |
|------|-------|
| CMake ≥ 3.16, a C++17 compiler | core build |
| OpenSSL, zlib | HTTPS calls + PDF decompression |
| poppler-utils (`pdftoppm`, `pdfinfo`) | reading scanned PDFs (`sudo apt install poppler-utils`) |
| Node.js | only to build the frontend |

---

## Setup

```bash
git clone https://github.com/zatwik511/Zathas.git
cd Zathas

# 1. Configure
cp .env.example .env
#    set GROQ_API_KEY=...   (cloud inference, vision, voice)
#    optionally MODEL_PATH=./models/your-model.gguf for local inference

# 2. Build the frontend
cd frontend-react && npm install && npm run build && cd ..

# 3. Build the server (downloads llama.cpp, cpp-httplib, nlohmann/json)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# 4. Run
./build/zathas_ai --static-dir ./frontend
```

Then open **http://localhost:8080**.

GPU builds: add `-DGGML_VULKAN=ON` (AMD/Intel) or `-DGGML_CUDA=ON` (NVIDIA) for the local engine.

---

## Options

```
--model       <path>    Local .gguf model (optional; required only without GROQ_API_KEY)
--port        <int>     HTTP port                    (default: 8080)
--host        <addr>    Host address                 (default: 0.0.0.0)
--ctx         <int>     Context window (local model) (default: 4096)
--threads     <int>     CPU threads (local model)    (default: 4)
--gpu-layers  <int>     GPU offload layers           (default: 0)
--max-tokens  <int>     Max tokens per response      (default: 512)
--temperature <float>   Sampling temperature         (default: 0.7)
--static-dir  <path>    Frontend directory           (default: ./frontend)
--env         <path>    .env file                    (default: .env)
```

---

## Project structure

```
.
├── CMakeLists.txt            # Build (FetchContent for all C++ deps)
├── .env.example
├── frontend-react/           # React + Vite + Tailwind source
├── frontend/                 # Built static frontend (served by the binary)
└── src/
    ├── main.cpp              # Entry point, CLI + .env
    ├── server.cpp/.h         # HTTP server + routes
    ├── inference.cpp/.h      # llama.cpp local engine
    ├── cloud_inference.cpp/.h# Cloud chat + Whisper
    ├── pdf_extract / pdf_render / office_extract  # file ingestion
    ├── media_ingest.h        # file-type detection, base64, UTF-8
    └── rate_limit.h          # per-IP rate limiting
```

---

## License

Copyright (c) 2026 Satwik Bhatnagar. All rights reserved. Public for portfolio/showcase purposes — see [LICENSE](LICENSE).
