#pragma once

/**
 * @file DekiProfiler.h
 * @brief Profiling macros for instrumenting critical code paths
 *
 * Usage:
 * @code
 * void MyFunction() {
 *     DEKI_PROFILE_SCOPE;  // Profile this function
 *     // ... your code
 * }
 *
 * void MyNamedSection() {
 *     {
 *         DEKI_PROFILE_SCOPE_N("CustomSection");
 *         // ... profiled section
 *     }
 * }
 * @endcode
 */

#define DEKI_PROFILE_SCOPE
#define DEKI_PROFILE_SCOPE_N(name)
#define DEKI_PROFILE_FRAME
#define DEKI_PROFILE_FRAME_N(name)
#define DEKI_PROFILE_PLOT(name, value)
#define DEKI_PROFILE_MESSAGE(text, size)
#define DEKI_PROFILE_SCOPE_COLOR(color)
