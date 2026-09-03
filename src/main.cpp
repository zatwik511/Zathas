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
        << "  --port        <int>    HTTP port to listen on        (default: 8080)\n"
        << "  --host        <addr>   Host address                  (default: 0.0.0.0)\n"
        << "  --ctx         <int>    Context size in tokens        (default: 4096)\n"
        << "  --threads     <int>    CPU threads for local inference (default: 4)\n"
        << "  --gpu-layers  <int>    Layers to offload to GPU      (default: 0)\n"
        << "  --max-tokens  <int>    Max tokens per response       (default: 512)\n"
        << "  --temperature <float>  Sampling temperature          (default: 0.7)\n"
        << "  --static-dir  <path>   Directory with frontend files (default: ./frontend)\n"
        << "  --env         <path>   Load settings from .env file  (default: .env)\n"
        << "\n"
        << ".env keys:\n"
        << "  MODEL_PATH   — local model file path (same as --model)\n"
        << "  GROQ_API_KEY — enables fast cloud inference + vision + voice\n"
        << "  CLOUD_MODEL  — Groq model name (default: openai/gpt-oss-120b)\n";
}

int main(int argc, char* argv[])
{
    std::string model_path;
    std::string lora_path;
    std::string env_file    = ".env";
    ServerConfig srv_cfg;
    int n_ctx        = 4096;
    int n_threads    = 4;
    int n_gpu_layers = 0;

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
        else if (arg == "--help" || arg == "-h") { print_usage(argv[0]); return 0; }
        else { std::cerr << "Unknown option: " << arg << "\n"; print_usage(argv[0]); return 1; }
    }

    // ── Read .env ──────────────────────────────────────────────────────────────
    if (model_path.empty())
        model_path = read_env_file(env_file, "MODEL_PATH");
    if (model_path.empty())
        if (const char* v = std::getenv("MODEL_PATH")) model_path = v;

    const std::string groq_api_key = read_env_file(env_file, "GROQ_API_KEY");
    srv_cfg.groq_api_key = groq_api_key;   // enables vision + voice
    std::string cloud_model = read_env_file(env_file, "CLOUD_MODEL");
    if (cloud_model.empty()) cloud_model = "openai/gpt-oss-120b";

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

        // Chat engine — cloud (Groq) if a key is set, otherwise the local model.
        std::shared_ptr<IInferenceEngine> engine;
        if (!groq_api_key.empty()) {
            std::cout << "[main] Inference: Groq API (" << cloud_model << ")\n";
            engine = std::make_shared<CloudInferenceEngine>(groq_api_key, cloud_model);
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
