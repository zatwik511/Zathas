# Multi-stage: build the frontend and the server separately, ship neither
# toolchain. The final image carries the binary, the built frontend, and the
# handful of runtime libraries the server actually dlopens or shells out to.

# ── 1. Frontend ───────────────────────────────────────────────────────────────
FROM node:20-slim AS frontend
WORKDIR /src
# Dependencies first, so a source-only change does not reinstall them.
COPY frontend-react/package.json frontend-react/package-lock.json ./frontend-react/
RUN cd frontend-react && npm ci
COPY frontend-react ./frontend-react
# vite's outDir is ../frontend, so this writes /src/frontend
RUN cd frontend-react && npm run build

# ── 2. Server ─────────────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates \
        libssl-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
# CMake's post-build step copies this directory next to the binary, so it has to
# exist even though the runtime image uses the freshly built one instead.
COPY frontend ./frontend
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel "$(nproc)"

# ── 3. Runtime ────────────────────────────────────────────────────────────────
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates libssl3 zlib1g \
        poppler-utils \
        curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 10001 zathas

WORKDIR /app
COPY --from=build    /src/build/zathas_ai /app/zathas_ai
COPY --from=frontend /src/frontend        /app/frontend

# Runs unprivileged: nothing here needs root, and the upload paths handle
# attacker-supplied files.
USER zathas
EXPOSE 8080

# Cheap by design - it does not call the inference provider.
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8080/api/health || exit 1

# Configuration comes from the environment; no .env file is baked in. Point
# --env at a mounted file instead if you prefer that.
ENTRYPOINT ["/app/zathas_ai"]
CMD ["--host", "0.0.0.0", "--port", "8080", "--static-dir", "/app/frontend"]
