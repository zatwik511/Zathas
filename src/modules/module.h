#pragma once
#include "inference.h"
#include "docstore.h"

#include <httplib.h>
#include <memory>
#include <string>

// ── Optional server modules ───────────────────────────────────────────────────
//
// The server exposes a small extension point so a deployment can mount extra
// HTTP routes without forking the server itself. This keeps deployment-specific
// features (internal dashboards, bespoke endpoints, experimental work) out of
// the core request path while still letting them share the engine, config and
// document store.
//
// The default build registers no modules — see modules/none.cpp. To supply an
// implementation, drop one or more .cpp files into src/modules/impl/ that
// define server_modules::register_routes(). CMake detects that directory at
// configure time and builds those files instead of the default, so a plain
// checkout always builds and runs cleanly with no extra setup.
//
// Modules are mounted after the core API routes and before the static-file
// handler, so they may add new paths but cannot shadow the built-in API.

struct ServerConfig;

struct ModuleContext {
    // Primary chat engine — cloud-backed or local, whichever the server started with.
    std::shared_ptr<IInferenceEngine> engine;

    // Local llama.cpp engine, when one was configured. Null on cloud-only
    // deployments; a module that requires it must handle that case.
    std::shared_ptr<IInferenceEngine> local_engine;

    // Server configuration, owned by the server and valid for its lifetime.
    const ServerConfig* config = nullptr;

    // Store of documents uploaded through /api/upload, keyed by id.
    DocStore* doc_store = nullptr;

    // Directory containing the running binary — useful for locating sibling
    // tools or data files regardless of the working directory.
    std::string exe_dir;
};

namespace server_modules {

// Mount any additional routes on `svr`. Called once during startup. The default
// implementation does nothing; see the header comment above.
//
// The referenced ModuleContext is owned by the server and stays alive for as
// long as it is running, so handlers may safely capture it by reference.
void register_routes(httplib::Server& svr, const ModuleContext& ctx);

// Short description of what is mounted, for the startup log and /api/health.
// Returns an empty string when no modules are active.
std::string active_modules();

// Called once after the server has stopped accepting requests, before exit.
// Cleanup point for modules that own persistent state and need to flush or
// compact it. The default implementation does nothing.
void on_shutdown();

}   // namespace server_modules
