#pragma once

// ============================================================================
// Serialization Template Declarations
// ============================================================================
// These templates provide a unified interface for serializing structs marked
// with DEKI_SERIALIZABLE. The generator creates template specializations for
// each struct in the corresponding .gen.cpp files.
//
// Usage:
//   FrameAnimationData data = Deserialize<FrameAnimationData>(json);
//   nlohmann::json j = Serialize(data);
//

#ifdef DEKI_EDITOR
#include <nlohmann/json.hpp>
#endif

// The namespace opens outside the editor guard so the closing brace below
// balances in runtime builds too (the namespace is simply empty there).
namespace Deki
{

#ifdef DEKI_EDITOR

// Primary templates (specialized by generator for DEKI_SERIALIZABLE structs)

template <typename T>
nlohmann::json Serialize(const T& obj);

template <typename T>
T Deserialize(const nlohmann::json& j);

#endif  // DEKI_EDITOR

}  // namespace Deki
