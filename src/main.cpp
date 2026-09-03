#include "config.h"
#include "inference.h"
#include "cloud_inference.h"
#include "server.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <cstdlib>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ── Simple .env parser ─────────────────────────────────────────────────────────
static std::string read_env_file(const std::string& path, const std::string& key)
{
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (line.substr(0, eq) == key)
            return line.substr(eq + 1);
    }
    return {};
}

static void print_usage(const char* prog)
{
    std::cerr
        << "Usage:\n"
        << "  " << prog << " [options]\n\n"
        << "Options:\n"
        << "  --model       <path>   Path to a local .gguf model file (optional)\n"
        << "  --lora        <path>   GGUF LoRA adapter for the local model (optional)\n"
        << "                         (required only if GROQ_API_KEY is not set)\n"
        << "  --port        <int>    HTTP port to listen on        (default: " << config::kPort << ")\n"
        << "  --host        <addr>   Host address                  (default: " << config::kHost << ")\n"
        << "  --ctx         <int>    Context size in tokens        (default: " << config::kContextSize << ")\n"
        << "  --threads     <int>    CPU threads for local inference (default: " << config::kThreads << ")\n"
        << "  --gpu-layers  <int>    Layers to offload to GPU      (default: " << config::kGpuLayers << ")\n"
        << "  --max-tokens  <int>    Max tokens per response       (default: " << config::kMaxTokens << ")\n"
        << "  --temperature <float>  Sampling temperature          (default: " << config::kTemperature << ")\n"
        << "  --static-dir  <path>   Directory with frontend files (default: " << config::kStaticDir << ")\n"
        << "  --env         <path>   Load settings from .env file  (default: .env)\n"
        << "  --skip-checks          Start without validating the cloud model\n"
        << "\n"
        << ".env keys:\n"
        << "  MODEL_PATH    — local model file path (same as --model)\n"
        << "  GROQ_API_KEY  — enables fast cloud inference + vision + voice\n"
        << "  CLOUD_MODEL   — chat model      (default: " << config::kCloudModel   << ")\n"
        << "  VISION_MODEL  — image model     (default: " << config::kVisionModel  << ")\n"
        << "  WHISPER_MODEL — speech-to-text  (default: " << config::kWhisperModel << ")\n";
}

int main(int argc, char* argv[])
{
    // Flush on every write. When stdout is a pipe rather than a terminal (under
    // systemd, Docker, or any log collector) it is block-buffered by default,
    // so an abrupt stop discards whatever had not filled a block yet — exactly
    // the lines explaining why the process is stopping.
    std::cout << std::unitbuf;

#ifdef _WIN32
    // Log messages are UTF-8; without this the Windows console renders them in
    // the legacy codepage and non-ASCII punctuation comes out as mojibake.
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::string model_path;
    std::string lora_path;
    std::string env_file    = ".env";
    ServerConfig srv_cfg;
    int  n_ctx        = config::kContextSize;
    int  n_threads    = config::kThreads;
    int  n_gpu_layers = config::kGpuLayers;
    bool skip_checks  = false;

    // ── Parse CLI args ─────────────────────────────────────────────────────────
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a value\n";
                std::exit(1);
            }
            return argv[++i];
        };

        if      (arg == "--model")        model_path           = next();
        else if (arg == "--lora")         lora_path            = next();
        else if (arg == "--port")         srv_cfg.port         = std::stoi(next());
        else if (arg == "--host")         srv_cfg.host         = next();
        else if (arg == "--ctx")          n_ctx                = std::stoi(next());
        else if (arg == "--threads")      n_threads            = std::stoi(next());
        else if (arg == "--gpu-layers")   n_gpu_layers         = std::stoi(next());
        else if (arg == "--max-tokens")   srv_cfg.max_tokens   = std::stoi(next());
        else if (arg == "--temperature")  srv_cfg.temperature  = std::stof(next());
        else if (arg == "--static-dir")   srv_cfg.static_dir   = next();
        else if (arg == "--env")          env_file             = next();
        else if (arg == "--skip-checks")  skip_checks          = true;
        else if (arg == "--help" || arg == "-h") { print_usage(argv[0]); return 0; }
        else { std::cerr << "Unknown option: " << arg << "\n"; print_usage(argv[0]); return 1; }
    }

    // ── Read configuration ─────────────────────────────────────────────────────
    // Every key is looked up in the .env file first, then the process
    // environment. The environment fallback is what makes container and
    // systemd deployments work, where config arrives as env vars and there is
    // no .env file on disk at all.
    auto env_value = [&](const char* key) -> std::string {
        std::string v = read_env_file(env_file, key);
        if (v.empty())
            if (const char* e = std::getenv(key)) v = e;
        return v;
    };
    auto env_or_default = [&](const char* key, const char* fallback) {
        const std::string v = env_value(key);
        return v.empty() ? std::string(fallback) : v;
    };

    if (model_path.empty()) model_path = env_value("MODEL_PATH");
    if (lora_path.empty())  lora_path  = env_value("LORA_PATH");

    const std::string groq_api_key = env_value("GROQ_API_KEY");
    srv_cfg.groq_api_key = groq_api_key;   // enables vision + voice
    srv_cfg.cloud_model   = env_or_default("CLOUD_MODEL",   config::kCloudModel);
    srv_cfg.vision_model  = env_or_default("VISION_MODEL",  config::kVisionModel);
    srv_cfg.whisper_model = env_or_default("WHISPER_MODEL", config::kWhisperModel);
    const std::string& cloud_model = srv_cfg.cloud_model;

    // Directory of this executable — lets components locate sibling tools and
    // data files regardless of the working directory the server was started in.
    {
        std::string exe_path;
#ifdef _WIN32
        char buf[MAX_PATH];
        GetModuleFileNameA(NULL, buf, MAX_PATH);
        exe_path = buf;
#else
        char buf[4096] = {};
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) exe_path = std::string(buf, len);
#endif
        if (!exe_path.empty())
            srv_cfg.exe_dir = std::filesystem::path(exe_path).parent_path().string();
    }

    if (model_path.empty() && groq_api_key.empty()) {
        std::cerr << "Error: Need either --model <path> or GROQ_API_KEY in .env\n\n";
        print_usage(argv[0]);
        return 1;
    }

    // ── Configuration checks ───────────────────────────────────────────────────
    // A model that has been retired or renamed should stop a deploy here, rather
    // than surfacing as a 404 on the next visitor's first message.
    if (!groq_api_key.empty() && !skip_checks) {
        std::vector<std::string> available;
        std::string err;
        const bool reachable = cloud_list_models(groq_api_key, &available, &err);

        // Only a definitive answer is fatal. If the provider simply could not be
        // reached, that is usually transient — refusing to boot would turn a blip
        // during a restart into a total outage, so warn and carry on.
        if (!reachable) {
            std::cerr << "[warn]  Could not verify cloud configuration: " << err << "\n"
                      << "        Starting anyway; chat will fail until this clears.\n";
        }

        const auto has = [&](const std::string& m) {
            return std::find(available.begin(), available.end(), m) != available.end();
        };

        if (reachable && !has(srv_cfg.cloud_model)) {
            std::string detail;
            cloud_check_model(groq_api_key, srv_cfg.cloud_model, &detail);
            std::cerr << "[fatal] Chat model unavailable: " << detail << "\n"
                      << "        Set CLOUD_MODEL in " << env_file << " to one of the above.\n";
            return 1;
        }
        // These cost one feature rather than the whole service, so they warn.
        if (reachable && !has(srv_cfg.vision_model))
            std::cerr << "[warn]  Vision model \"" << srv_cfg.vision_model
                      << "\" unavailable — image and scanned-PDF uploads will fail. "
                         "Set VISION_MODEL in " << env_file << ".\n";
        if (reachable && !has(srv_cfg.whisper_model))
            std::cerr << "[warn]  Speech model \"" << srv_cfg.whisper_model
                      << "\" unavailable — voice input will fail. "
                         "Set WHISPER_MODEL in " << env_file << ".\n";

        if (reachable)
            std::cout << "[main] Cloud configuration verified ("
                      << available.size() << " models reachable)\n";
    }

    // ── Boot ───────────────────────────────────────────────────────────────────
    std::cout << "=== Zathas AI ===\n";

    try {
        // Optional local model (llama.cpp). Used directly when no cloud key is set.
        std::shared_ptr<InferenceEngine> local_engine;
        if (!model_path.empty()) {
            std::cout << "[main] Loading local model: " << model_path << "\n";
            local_engine = std::make_shared<InferenceEngine>(
                model_path, n_ctx, n_threads, n_gpu_layers, lora_path);
        }

        // Chat engine — cloud if a key is set, otherwise the local model.
        std::shared_ptr<IInferenceEngine> engine;
        if (!groq_api_key.empty()) {
            std::cout << "[main] Inference: Groq API (" << cloud_model << ")\n";
            engine = std::make_shared<CloudInferenceEngine>(
                groq_api_key, cloud_model, config::kGroqHost, srv_cfg.vision_model);
        } else {
            std::cout << "[main] Inference: local model\n";
            engine = local_engine;
        }

        ChatServer server(engine, srv_cfg, local_engine);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
