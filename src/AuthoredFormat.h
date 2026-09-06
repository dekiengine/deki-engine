#pragma once

// Version of the AUTHORED JSON formats: the files a project tracks in git and
// the editor reads and writes (.scene, .asset, the .data sidecars). Deliberately
// separate from SCENE_FORMAT_VERSION in SceneMessagePack.h, which versions the
// compiled MessagePack scene the device loads: that is build output and can
// change freely; these cannot.
//
// A file without the key is version 1. A reader refuses a file newer than it
// understands, with a message naming both versions, rather than loading it
// partially; older files are migrated or aliased on load and written back at
// the current version on save. Bump a constant only with a migration path, and
// say so in COMPATIBILITY.md and the changelog.

#include <cstdint>

namespace Deki::AuthoredFormat
{

inline constexpr const char* VersionKey = "formatVersion";

inline constexpr int32_t SceneVersion = 1;      // .scene
inline constexpr int32_t AssetVersion = 1;      // .asset envelopes
inline constexpr int32_t AssetDataVersion = 1;  // <asset>.data sidecars

}  // namespace Deki::AuthoredFormat
