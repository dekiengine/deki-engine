#pragma once

namespace Deki
{

/// Runs the engine: initialises it, loads the boot scene from flash storage,
/// runs the main loop until it ends, and shuts down. Returns the process exit
/// code.
///
/// The platform package's entry point (main() on desktop, app_main() on
/// ESP-IDF) sets up its providers and then calls this.
int Main();

}  // namespace Deki
