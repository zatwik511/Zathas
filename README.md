# Zathas

[![CI](https://github.com/zatwik511/Zathas/actions/workflows/ci.yml/badge.svg)](https://github.com/zatwik511/Zathas/actions/workflows/ci.yml)

A multimodal AI chatbot whose entire backend is **hand-written C++** — HTTP server,
SSE streaming, file ingestion, and inference routing, compiled to a single binary
that also serves the UI. No Python or Node.js at runtime.

**Live: [zathas.com](https://zathas.com)**

Chat runs on cloud inference by default; a local [llama.cpp](https://github.com/ggml-org/llama.cpp)
engine is built in and used instead when you point it at a GGUF model.

---

## Features

- **Streaming chat** over Server-Sent Events, with live markdown and syntax highlighting
- **Multimodal uploads** the assistant can actually read:
  - **Images** via a vision model
  - **Audio** transcribed with Whisper
  - **PDFs** — text PDFs by extraction; scanned PDFs rendered to images and read by vision
  - **Office docs** (`.docx` / `.xlsx` / `.pptx`) and 30+ text and code formats
- **Conversation history** with AI-generated titles, persisted in the browser
- **Voice input**, drag-and-drop and paste-to-upload, per-code-block copy
- **Stop / regenerate / edit-and-resend**
- **Startup validation** — the server refuses to boot if its chat model has been
  retired, and tells you which models *are* available
- **Abuse limits** — per-IP rate limiting, body and message size caps, and a
  service-wide daily budget so one visitor cannot spend the whole quota
- **Optional server modules** — mount extra routes without forking the server

---

## Architecture

```
browser ──HTTP──▶ cpp-httplib server (C++17, single binary)
                    ├── POST /api/chat       → streaming chat (SSE)
                    ├── POST /api/upload     → text / PDF / office / image / audio
                    ├── POST /api/transcribe → voice → text (Whisper)
                    ├── POST /api/title      → short AI chat titles
                    ├── GET  /api/health     → status, backend, daily usage
                    └── GET  /*              → React frontend
```

**Inference routing.** One `IInferenceEngine` interface with two implementations:
a cloud engine speaking the OpenAI-compatible API, and a local llama.cpp engine.
Which one runs is decided at startup from configuration.

**Streaming.** The provider's SSE stream is parsed incrementally and forwarded to
the browser token by token. Reasoning models that wrap their chain-of-thought in
`<think>` tags have it stripped server-side rather than in the UI, because
anything sent to the client is replayed as history on later turns and paid for
again.

**Conversation persistence is client-side.** History lives in the browser's
`localStorage` and is replayed as context on each turn. The server keeps no
per-user conversation state, which is deliberate: a shared server-side store on
a multi-user deployment would leak one visitor's content into another's prompt.

**Optional modules.** `src/modules/module.h` defines a small extension point.
CMake compiles `src/modules/impl/*.cpp` if that directory exists and a no-op
default otherwise, so a deployment can add routes without forking. Likewise any
`.jsx` in `frontend-react/src/pages/extra/` is auto-routed by filename.

---

## Prerequisites

| Tool | Notes |
|------|-------|
| CMake ≥ 3.16, a C++17 compiler | core build |
| OpenSSL, zlib | HTTPS calls and PDF decompression |
| poppler-utils (`pdftoppm`, `pdfinfo`) | scanned PDFs — `sudo apt install poppler-utils` |
| Node.js | only to build the frontend |
| SQLite3 | only if you supply optional modules that need it |

---

## Quickstart

```bash
git clone https://github.com/zatwik511/Zathas.git
cd Zathas

# 1. Configure
cp .env.example .env
#    set GROQ_API_KEY=...  (free key at https://console.groq.com)

# 2. Build the frontend
cd frontend-react && npm install && npm run build && cd ..

# 3. Build the server (fetches llama.cpp, cpp-httplib, nlohmann/json)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# 4. Run
./build/zathas_ai --static-dir ./frontend
```

Then open **http://localhost:8080**.

**On Windows (MSYS2/UCRT64)** you must name the generator explicitly, or CMake
picks NMake and fails with `CMAKE_CXX_COMPILER not set`:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:\msys64\ucrt64
cmake --build build --parallel
```

**GPU (local engine):** add `-DGGML_VULKAN=ON` (AMD/Intel) or `-DGGML_CUDA=ON` (NVIDIA).
**LoRA training tools:** add `-DZATHAS_BUILD_FINETUNE=ON` to also build `llama-finetune`.

### Docker

```bash
GROQ_API_KEY=your-key docker compose up --build
```

The image is multi-stage and runs unprivileged; configuration is read from the
environment, so no `.env` is baked in.

### Tests

```bash
ctest --test-dir build --output-on-failure
```

No test framework — plain assertions, covering the parts that have actually
broken: the streaming parser (`<think>` stripping, tags split across chunks,
reasoning delivered in a separate field, budget exhausted mid-thought), title
handling, the rate limiter and daily cap, and provider-error classification.

---

## Configuration

Everything is optional except having one way to run inference. Defaults live in
`src/config.h`; `.env` overrides them.

| `.env` key | Default | What it does |
|---|---|---|
| `GROQ_API_KEY` | — | Enables cloud chat, vision and voice |
| `CLOUD_MODEL` | `openai/gpt-oss-120b` | Chat model. Validated at startup |
| `VISION_MODEL` | `qwen/qwen3.6-27b` | Used when an image or scanned PDF is attached |
| `WHISPER_MODEL` | `whisper-large-v3` | Speech-to-text |
| `MODEL_PATH` | — | GGUF model for local inference; required if no API key |
| `LORA_PATH` | — | GGUF LoRA adapter for the local model |

| CLI flag | Default | |
|---|---|---|
| `--model <path>` | — | Local GGUF model |
| `--lora <path>` | — | LoRA adapter |
| `--port <int>` | `8080` | |
| `--host <addr>` | `0.0.0.0` | |
| `--ctx <int>` | `4096` | Context window (local engine) |
| `--threads <int>` | `4` | CPU threads (local engine) |
| `--gpu-layers <int>` | `0` | Layers offloaded to GPU |
| `--max-tokens <int>` | `512` | Max tokens per response |
| `--temperature <float>` | `0.7` | |
| `--static-dir <path>` | `./frontend` | |
| `--env <path>` | `.env` | |
| `--skip-checks` | off | Start without validating models against the provider |

---

## Running it in public

A public deployment spends your provider quota on behalf of strangers, so every
cost-incurring endpoint is bounded twice — per IP, and per day across everyone.
Both are in `src/config.h`:

| Limit | Default |
|---|---|
| Chat + title requests | 25 / minute / IP |
| Uploads + transcription | 15 / minute / IP |
| Request body | 10 MB |
| Single message | 16,000 characters |
| Provider-billed requests | 2,000 / day, service-wide |

The daily budget is charged only once a request has passed validation and is
definitely about to reach the provider, so malformed or oversized requests
cannot drain it.

`scripts/deploy.sh` pulls, backs the current binary up under a timestamp, builds,
restarts the service, verifies `/api/health`, and automatically restores the
previous binary if that check fails.

---

## Why C++

The interesting constraint was doing it without a framework. There is no Flask,
no Express, no LangChain — request routing, SSE framing, multipart parsing,
streaming JSON handling, PDF decompression and file-type sniffing are all
explicit. That makes the failure modes visible in a way a framework usually
hides, which turned out to matter more than expected.

A few things this project taught the hard way:

- **A streamed reply cannot be un-sent.** Once tokens reach the client, any
  recovery path has to work forwards, not by restarting.
- **Hosted models are not stable infrastructure.** A model that worked for months
  can be retired without warning, so the server now validates its configuration
  at startup instead of discovering it on a user's first message.
- **Reasoning models bill for thinking.** They spend tokens before emitting
  anything visible; a budget sized for the answer alone produces empty replies.
- **Shared server-side memory is a leak on a multi-user deployment.** Anything
  summarised across visitors ends up in someone else's prompt.

---

## Project structure

```
.
├── CMakeLists.txt              # Build; FetchContent for all C++ deps
├── Dockerfile                  # Multi-stage; runs unprivileged
├── docker-compose.yml
├── .env.example                # Every variable the server reads
├── scripts/deploy.sh           # Deploy with health check and auto-rollback
├── tests/test_main.cpp         # Unit tests, no framework
├── frontend-react/             # React + Vite + Tailwind source
├── frontend/                   # Built static frontend, served by the binary
└── src/
    ├── config.h                # Single source of every default
    ├── main.cpp                # Entry point, CLI, .env, startup validation
    ├── server.cpp/.h           # HTTP routes, limits, admission control
    ├── inference.cpp/.h        # Local llama.cpp engine
    ├── cloud_inference.cpp/.h  # Cloud chat, Whisper, model validation
    ├── sse_parser.h            # Streaming parser (<think> stripping, reasoning)
    ├── title_util.h            # Title sanitising and placeholder rejection
    ├── pdf_extract / pdf_render / office_extract
    ├── media_ingest.h          # File-type detection, base64, UTF-8
    ├── rate_limit.h            # Per-IP limiter and daily cap
    └── modules/                # Optional server modules (no-op by default)
```

---

## License

Copyright (c) 2026 Satwik Bhatnagar. All rights reserved. This repository is
public so the work can be read and evaluated, not used — see [LICENSE](LICENSE).
