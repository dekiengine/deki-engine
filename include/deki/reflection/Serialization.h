#pragma once

// ============================================================================
// Serialization templates
// ============================================================================
// Serialize and Deserialize for structs marked DEKI_SERIALIZABLE. The
// generator writes a specialization for each struct in its .gen.cpp.
//
//   FrameAnimationData data = Deserialize<FrameAnimationData>(json);
//   nlohmann::json j = Serialize(data);
//

#ifdef DEKI_EDITOR
#include <nlohmann/json.hpp>
#endif

// The namespace opens outside the editor guard so the closing brace balances
// in runtime builds too, where the namespace is empty.
namespace Deki
{

#ifdef DEKI_EDITOR

// Primary templates; the generator specializes them.

template <typename T>
nlohmann::json Serialize(const T& obj);

template <typename T>
T Deserialize(const nlohmann::json& j);

#endif  // DEKI_EDITOR

}  // namespace Deki
