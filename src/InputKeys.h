#pragma once

/**
 * @brief Generic input key constants
 * These constants provide a framework-independent way to reference common keys
 */

namespace Deki
{

namespace InputKeys
{
// Named values, used like enum values: PascalCase, as the code style allows.
// NOLINTBEGIN(readability-identifier-naming)
// Standard ASCII keys
const uint32_t Space = 32;

// Special keys
const uint32_t Enter = 13;
const uint32_t Esc = 27;
const uint32_t Backspace = 8;

// Arrow keys
const uint32_t Up = 1001;
const uint32_t Down = 1002;
const uint32_t Left = 1003;
const uint32_t Right = 1004;

// Letter keys (lowercase)
const uint32_t A = 97;
const uint32_t B = 98;
const uint32_t C = 99;
const uint32_t D = 100;
const uint32_t E = 101;
const uint32_t F = 102;
const uint32_t G = 103;
const uint32_t H = 104;
const uint32_t I = 105;
const uint32_t J = 106;
const uint32_t K = 107;
const uint32_t L = 108;
const uint32_t M = 109;
const uint32_t N = 110;
const uint32_t O = 111;
const uint32_t P = 112;
const uint32_t Q = 113;
const uint32_t R = 114;
const uint32_t S = 115;
const uint32_t T = 116;
const uint32_t U = 117;
const uint32_t V = 118;
const uint32_t W = 119;
const uint32_t X = 120;
const uint32_t Y = 121;
const uint32_t Z = 122;
// NOLINTEND(readability-identifier-naming)
}  // namespace InputKeys

}  // namespace Deki
