#include "modules/module.h"

// Default build: no optional modules are mounted. See modules/module.h for how
// a deployment supplies its own implementation.

namespace server_modules {

void register_routes(httplib::Server&, const ModuleContext&)
{
    // Intentionally empty.
}

std::string active_modules()
{
    return "";
}

void on_shutdown()
{
    // Intentionally empty.
}

}   // namespace server_modules
