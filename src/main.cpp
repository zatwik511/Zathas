#include "config.h"
#include "inference.h"
#include "cloud_inference.h"
#include "gemini_inference.h"
#include "failover_engine.h"
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

    // ── Read .env ──────────────────────────────────────────────────────────────
    if (model_path.empty())
        model_path = read_env_file(env_file, "MODEL_PATH");
    if (model_path.empty())
        if (const char* v = std::getenv("MODEL_PATH")) model_path = v;
    if (lora_path.empty())
        lora_path = read_env_file(env_file, "LORA_PATH");

    const std::string groq_api_key = read_env_file(env_file, "GROQ_API_KEY");
    srv_cfg.groq_api_key = groq_api_key;   // enables vision + voice

    // Model names are overridable because providers retire them without notice.
    // Anything left unset falls back to the defaults in config.h.
    auto env_or_default = [&](const char* key, const char* fallback) {
        const std::string v = read_env_file(env_file, key);
        return v.empty() ? std::string(fallback) : v;
    };
    srv_cfg.cloud_model   = env_or_default("CLOUD_MODEL",   config::kCloudModel);
    srv_cfg.vision_model  = env_or_default("VISION_MODEL",  config::kVisionModel);
    srv_cfg.whisper_model = env_or_default("WHISPER_MODEL", config::kWhisperModel);
    srv_cfg.gemini_model  = env_or_default("GEMINI_MODEL",  config::kGeminiModel);
    const std::string& cloud_model = srv_cfg.cloud_model;

    // Optional second provider. When set, chat fails over to it.
    const std::string gemini_api_key = read_env_file(env_file, "GEMINI_API_KEY");

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

    if (model_path.empty() && groq_api_key.empty() && gemini_api_key.empty()) {
        std::cerr << "Error: Need a local model (--model <path>) or an API key "
                     "(GROQ_API_KEY or GEMINI_API_KEY) in .env\n\n";
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

    // The fallback provider only warns: losing it costs resilience, not service.
    if (!gemini_api_key.empty() && !skip_checks) {
        std::vector<std::string> available;
        std::string err;
        if (!gemini_list_models(gemini_api_key, &available, &err)) {
            std::cerr << "[warn]  Fallback provider unusable: " << err << "\n";
        } else if (std::find(available.begin(), available.end(), srv_cfg.gemini_model)
                       == available.end()) {
            std::cerr << "[warn]  Fallback model \"" << srv_cfg.gemini_model
                      << "\" not available to this key";
            if (!available.empty()) {
                std::sort(available.begin(), available.end());
                std::cerr << ". Available include: ";
                for (size_t i = 0; i < std::min<size_t>(available.size(), 5); ++i)
                    std::cerr << (i ? ", " : "") << available[i];
            }
            std::cerr << ". Set GEMINI_MODEL in " << env_file << ".\n";
        } else {
            std::cout << "[main] Fallback provider verified (" << srv_cfg.gemini_model << ")\n";
        }
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

        // Chat engine. With both cloud providers configured, requests go to Groq
        // and fall back to Gemini when Groq reports the model is gone, throttles
        // us, or fails outright.
        std::shared_ptr<IInferenceEngine> groq_engine;
        std::shared_ptr<IInferenceEngine> gemini_engine;

        if (!groq_api_key.empty()) {
            std::cout << "[main] Inference: Groq API (" << cloud_model << ")\n";
            groq_engine = std::make_shared<CloudInferenceEngine>(
                groq_api_key, cloud_model, config::kGroqHost, srv_cfg.vision_model);
        }
        if (!gemini_api_key.empty()) {
            gemini_engine = std::make_shared<GeminiInferenceEngine>(
                gemini_api_key, srv_cfg.gemini_model);
        }

        std::shared_ptr<IInferenceEngine> engine;
        if (groq_engine && gemini_engine) {
            engine = std::make_shared<FailoverEngine>(groq_engine, gemini_engine,
                                                      "groq", "gemini");
        } else if (groq_engine) {
            engine = groq_engine;
        } else if (gemini_engine) {
            std::cout << "[main] Inference: Gemini API (" << srv_cfg.gemini_model << ")\n";
            engine = gemini_engine;
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
