#pragma once

// DEKI_THREAD_LOCAL: `thread_local`, unless the build defines
// DEKI_NO_THREAD_LOCAL.
//
// Some targets cannot have thread-local storage. A Tactility app on a device
// is a relocatable ELF that elf_loader places in memory, and the loader has no
// TLS: the app's thread-local variables would be addressed relative to the
// firmware's own TLS block and overwrite it. Such a build defines
// DEKI_NO_THREAD_LOCAL and the variables become ordinary statics, which is
// correct there because the game runs on one task.
#if defined(DEKI_NO_THREAD_LOCAL)
#define DEKI_THREAD_LOCAL
#else
#define DEKI_THREAD_LOCAL thread_local
#endif
