#pragma once

#include <deki/Export.h>
#include <nlohmann/json_fwd.hpp>

/**
 * @brief Rewrite an object's saved components before they load.
 *
 * For format changes DEKI_RENAMED_FROM cannot express: a value that must be
 * converted, or fields that move from one component to another. A migration
 * receives the "components" array of one object in the scene JSON and edits it
 * in place; it runs on every object, so it checks for the old shape and
 * returns untouched when it is not there. The next save writes the new shape.
 *
 * Editor only: device scenes are compiled from the JSON load, so they are
 * migrated on the way. Register from a static object in the package, whose
 * destructor unregisters when a hot reload unloads the DLL:
 *
 *   static Deki::ComponentsMigrationRegistrar s_Migration(&MigrateCamera);
 */

namespace Deki
{

using ComponentsMigration = void (*)(nlohmann::json& components);

DEKI_ENGINE_API void RegisterComponentsMigration(ComponentsMigration migration);
DEKI_ENGINE_API void UnregisterComponentsMigration(ComponentsMigration migration);

/// Run every registered migration on one object's components array.
DEKI_ENGINE_API void RunComponentsMigrations(nlohmann::json& components);

struct ComponentsMigrationRegistrar
{
    explicit ComponentsMigrationRegistrar(ComponentsMigration m) : m_Migration(m) { RegisterComponentsMigration(m); }
    ~ComponentsMigrationRegistrar() { UnregisterComponentsMigration(m_Migration); }
    ComponentsMigrationRegistrar(const ComponentsMigrationRegistrar&) = delete;
    ComponentsMigrationRegistrar& operator=(const ComponentsMigrationRegistrar&) = delete;

private:
    ComponentsMigration m_Migration;
};

}  // namespace Deki
