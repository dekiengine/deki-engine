#pragma once

// ARM's NEON vector unit, where the target has it: ARMv7 built with a NEON
// -mfpu (the Miyoo Mini's Cortex-A7, a Raspberry Pi) and every AArch64 core.
// DEKI_NEON is 1 there and <arm_neon.h> is included; 0 everywhere else.
//
// Code that uses NEON keeps a plain C++ path beside it, for the cores without
// (the ESP32's Xtensa, desktop x86), and a test that both give the same
// result. Use only intrinsics ARMv7 and AArch64 share, so one path serves both.
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define DEKI_NEON 1
#include <arm_neon.h>
#else
#define DEKI_NEON 0
#endif
