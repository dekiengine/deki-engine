#pragma once

namespace Deki
{

/**
 * @brief Run the engine: initialise, load the boot scene from flash storage,
 *        drive the main loop until it ends, shut down.
 *
 * The platform integration package's entry (main() on desktop, app_main() on
 * ESP-IDF) sets up its providers and then calls this. Returns the process
 * exit code.
 */
int Main();

}  // namespace Deki
