#pragma once

/**
 * @file ObjectInternal.h
 * @brief How the engine and the editor put a component on an object when it
 *        does not come from a script.
 *
 * A script adds a component with `obj->AddComponent<T>()`, and that is the
 * whole public surface. The scene loaders hand over instances the factory
 * built from a file, and the editor's commands, panels and scene loader
 * create what the user picked from a type name. Those paths live here, in an
 * engine-internal header that packages cannot include.
 */

#include <deki/Object.h>

#include <string>

namespace Deki
{
namespace Internal
{

/// Attach a component the caller created (the object takes ownership).
void AttachComponent(Object& object, Component* component);

#ifdef DEKI_EDITOR
/**
 * Create and attach a component by its qualified class name, the string a
 * scene file carries. A bare class name and a 0.10 GUID resolve too; the
 * loader passes the file's "class" hint so a renamed type still loads.
 * @return The new component, or nullptr when the class is not registered.
 */
Component* AddComponentByName(Object& object, const std::string& typeName, const std::string& classHint = {});

/**
 * Take a component off its object WITHOUT destroying it: the editor's undo
 * keeps the instance and puts the same one back, so every pointer other undo
 * steps hold into it stays valid. Deleting it and creating a new one on undo
 * left those steps writing into freed memory.
 * @return Its index on the object, or -1 when the object does not have it.
 *         The caller owns it until ReattachComponent (or deletes it).
 */
int DetachComponent(Object& object, Component* component);

/// Put a detached component back at `index` (clamped to the end); the object
/// owns it again.
void ReattachComponent(Object& object, Component* component, size_t index);
#endif

}  // namespace Internal
}  // namespace Deki
