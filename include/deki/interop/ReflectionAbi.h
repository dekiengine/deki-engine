#pragma once

#include <cstdint>
#include <cstdio>  // snprintf (mismatch description)
#include <cstddef>

#include <deki/reflection/Property.h>  // DekiPropertyInfo / DekiComponentMeta / DekiFieldRef
#include <deki/Object.h>               // sizeof(DekiObject) / sizeof(DekiComponent)

/// Layout fingerprint of the structs that cross DLL boundaries.
///
/// A package DLL builds its property tables as static arrays and hands the
/// editor a pointer and a count. The editor walks them with its own compiled
/// sizeof, so both sides must agree on the layout exactly. If they do not,
/// element 0 reads fine and every later element is read from the middle of a
/// struct; a `const char*` read that way is non-null garbage that passes null
/// checks and crashes far away.
///
/// The append-only rule of the metadata structs protects a single struct read
/// through a pointer, not an array, whose stride changes. Hence this
/// handshake: every package DLL exports its compiled values (ReflectionAbi.cpp,
/// linked into each package by BuildFileGenerator) and the loader refuses a
/// package that disagrees, naming it and saying to rebuild.
///
/// It catches size changes, not same-size reorderings: for those, bump
/// kDekiReflectionAbiVersion by hand.
struct DekiReflectionAbi
{
    uint32_t version;             // kDekiReflectionAbiVersion at the package's build time
    uint32_t sizeofPropertyInfo;  // stride of Deki::ComponentMeta::properties
    uint32_t sizeofComponentMeta;
    uint32_t sizeofFieldRef;  // stride of the compact field tables

    // Object layout. Packages read Deki::Object's parent, children and
    // component vector through inline accessors and derive from
    // Deki::Component, so both layouts are compiled into every package.
    //
    // Each translation unit must compute these from its own view of the
    // header, never through a helper in the engine's .cpp, which would always
    // return the engine's answer and always agree.
    uint32_t sizeofDekiObject;
    uint32_t sizeofDekiComponent;

    // Which transform the build compiled into Deki::Object: bit 0 is
    // DEKI_TRANSFORM_2D, bit 1 is DEKI_TRANSFORM_3D. sizeof alone can miss a
    // width mismatch when padding happens to give the same size.
    uint32_t transformConfig;
};

/// Bump this for any binary change the sizes above cannot see, so packages
/// built before it are refused at load ("rebuild it") instead of misbehaving:
/// - a field reordered or repurposed without a size change;
/// - a virtual added, removed or reordered on an engine class that packages
///   derive from or call (Deki::Component, IMemoryProvider, IDisplay,
///   ICamera, IRenderSystem, ...), since every later vtable slot moves;
/// - a changed signature or name of anything packages link to or look up by
///   name, since its mangled or exported name changes;
/// - a change to DekiReflectionAbi itself. `version` stays at offset 0, and
///   the loader must check it through the pointer before copying the struct,
///   because an older DLL returns a shorter one.
/// A size change alone (such as a field added to Deki::PropertyInfo) needs no
/// bump: the sizeof fields catch it.
///
/// Debug and release builds may differ in layout (AllocContext carries file,
/// function and line only with DEKI_ALLOC_CONTEXT_CAPTURE), so a debug engine
/// with release packages is not supported.
inline constexpr uint32_t kDekiReflectionAbiVersion = 22;

/// The values as compiled into whoever calls this.
inline DekiReflectionAbi DekiCurrentReflectionAbi()
{
    DekiReflectionAbi abi{};
    abi.version = kDekiReflectionAbiVersion;
    abi.sizeofPropertyInfo = static_cast<uint32_t>(sizeof(Deki::PropertyInfo));
    abi.sizeofComponentMeta = static_cast<uint32_t>(sizeof(Deki::ComponentMeta));
    abi.sizeofFieldRef = static_cast<uint32_t>(sizeof(Deki::FieldRef));
    abi.sizeofDekiObject = static_cast<uint32_t>(sizeof(Deki::Object));
    abi.sizeofDekiComponent = static_cast<uint32_t>(sizeof(Deki::Component));
    abi.transformConfig = 0u
#ifdef DEKI_TRANSFORM_2D
                          | 1u
#endif
#ifdef DEKI_TRANSFORM_3D
                          | 2u
#endif
        ;
    return abi;
}

/// True when a loaded DLL's values match ours and it is safe to use.
/// Otherwise `what` receives a description of the first mismatch, for the log.
inline bool DekiReflectionAbiMatches(const DekiReflectionAbi& other, char* what, size_t whatSize)
{
    const DekiReflectionAbi mine = DekiCurrentReflectionAbi();
    struct Field
    {
        const char* name;
        uint32_t mine;
        uint32_t theirs;
    };
    const Field fields[] = {
        { "reflection ABI version", mine.version, other.version },
        { "sizeof(DekiPropertyInfo)", mine.sizeofPropertyInfo, other.sizeofPropertyInfo },
        { "sizeof(DekiComponentMeta)", mine.sizeofComponentMeta, other.sizeofComponentMeta },
        { "sizeof(DekiFieldRef)", mine.sizeofFieldRef, other.sizeofFieldRef },
        { "sizeof(DekiObject)", mine.sizeofDekiObject, other.sizeofDekiObject },
        { "sizeof(DekiComponent)", mine.sizeofDekiComponent, other.sizeofDekiComponent },
        { "transform configuration", mine.transformConfig, other.transformConfig },
    };
    for (const Field& f : fields)
    {
        if (f.mine != f.theirs)
        {
            if (what && whatSize)
            {
                std::snprintf(what, whatSize, "%s is %u there, %u here", f.name, f.theirs, f.mine);
            }
            return false;
        }
    }
    return true;
}

// The symbol every package DLL and the game plugin export (ReflectionAbi.cpp).
//
// A missing export means the DLL predates this handshake, so the loaders warn
// and continue.
//
// On a mismatch the loaders skip the DLL but must not unload it: its static
// initializers have already given engine registries pointers into its code
// (editor extensions, node types, Deki::SetupComponent auto-factories), and
// FreeLibrary would leave them dangling.
#define DEKI_REFLECTION_ABI_EXPORT_NAME "DekiPluginGetReflectionAbi"

extern "C"
{
    typedef const DekiReflectionAbi* (*DekiGetReflectionAbiFunc)();
}
