#pragma once

#include <cstdint>
#include <cstdio>  // snprintf (mismatch description)
#include <cstddef>

#include <deki/reflection/Property.h>  // DekiPropertyInfo / DekiComponentMeta / DekiFieldRef
#include <deki/Object.h>               // sizeof(DekiObject) / sizeof(DekiComponent)

/**
 * @brief Layout fingerprint of the reflection structs that cross DLL boundaries.
 *
 * A package DLL builds its own property tables as static arrays and hands the
 * editor a pointer plus a count. The editor then walks them with ITS compiled
 * sizeof, so the two sides must agree on the layout exactly. They usually do,
 * because everything is rebuilt together, and when they don't the failure is
 * vicious: appending a field to Deki::PropertyInfo changes the array STRIDE, so
 * element 0 reads fine and every element after it lands mid-struct. A `const
 * char*` read out of the middle of a struct is non-null garbage, so it sails
 * past null checks and faults far away (seen as a crash inside strlen, several
 * frames deep in JSON key lookup, during scene compilation).
 *
 * The append-only rule the metadata structs document protects a SINGLE struct
 * read through a pointer: a stale reader finds the fields it knows at the right
 * offsets and ignores the rest. It does not protect an ARRAY, because the stride
 * changes. So arrays of these structs need a real handshake, which is this.
 *
 * Every package DLL exports its compiled values (ReflectionAbi.cpp, linked into
 * each package by BuildFileGenerator) and the loader refuses a package that
 * disagrees, naming it and saying to rebuild. Loudly wrong beats silently
 * corrupt.
 *
 * LIMIT: this catches size changes, not same-size reorderings. Bump
 * kDekiReflectionAbiVersion by hand when you move a field without changing the
 * struct's size, so old packages are still rejected.
 */
struct DekiReflectionAbi
{
    uint32_t version;             // kDekiReflectionAbiVersion at the package's build time
    uint32_t sizeofPropertyInfo;  // stride of Deki::ComponentMeta::properties
    uint32_t sizeofComponentMeta;
    uint32_t sizeofFieldRef;  // stride of the compact field tables

    // Object layout. Packages do not merely pass these types around: they read
    // Deki::Object's `parent`/`children` and walk its component vector through
    // INLINE accessors, and they derive from Deki::Component. Both layouts are
    // therefore compiled into every package, and a package built against a
    // different engine revision reads the wrong offsets with no diagnostic.
    //
    // Computed from each translation unit's own view of the header - never from
    // a helper defined in the engine's .cpp, which would link every caller to
    // the engine's answer and agree unconditionally.
    uint32_t sizeofDekiObject;
    uint32_t sizeofDekiComponent;

    // Which transform the build compiled into Deki::Object: bit 0 is
    // DEKI_TRANSFORM_2D, bit 1 is DEKI_TRANSFORM_3D. A package built at a
    // different width reads different offsets, and sizeof alone would not
    // catch a headless build against a 2D one whose padding happened to land
    // on the same number.
    uint32_t transformConfig;
};

/**
 * @brief Bump ONLY for a layout change that keeps every sizeof identical
 *        (reordering or repurposing a field). Size changes are caught anyway.
 *
 * 2: dropped sizeofNodeMeta. DekiNodeMeta moved out of the engine into the
 *    deki-nodegraph package, and the engine must not include a package header to
 *    build its own ABI fingerprint. That guard now lives where the struct does:
 *    NodeTypeRegistry::Register takes the caller's compiled sizeof through a
 *    default argument and refuses a meta that disagrees. This field shrinking
 *    is itself a layout change, hence the bump - `version` sits at offset 0 in
 *    both the old and new struct, so a pre-bump DLL is rejected on it.
 *
 * 3: added sizeofDekiObject and sizeofDekiComponent. Until now the handshake
 *    covered only the reflection tables, while packages were also reading
 *    Deki::Object's members through inline accessors with nothing checking that
 *    layout at all. Growing the struct is itself a layout change, so the bump is
 *    required: a version-2 DLL returns a shorter struct, and reading the two new
 *    fields out of it would read past its end. The loader must therefore check
 *    `version` THROUGH THE POINTER before copying the struct by value.
 *
 * 4: dropped the deprecated getType()/getBaseType() forwarders. The struct is
 *    unchanged and every sizeof still matches, so nothing else here would
 *    catch a package built against v3 -- it would reference a spelling that no
 *    longer exists. This is exactly the layout-compatible but semantically
 *    incompatible case this counter exists for: bump it so such a package is
 *    refused at load rather than failing in a confusing way later.
 *
 * 5: added the Deki::Component::DeclaresUpdate() virtual. Every sizeof is
 *    unchanged, but a slot was inserted in the middle of the component vtable,
 *    so a package built against v4 calls the wrong function through every
 *    virtual after it (seen as an integer divide by zero deep inside a stale
 *    plugin while compiling a scene). The vtable is part of the ABI even
 *    though nothing here can measure it: any new virtual on Deki::Component or
 *    Deki::Behaviour needs a bump.
 *
 * (no bump) Deki::PropertyInfo grew `renamedFrom` (DEKI_RENAMED_FROM). The
 *    struct's size changed, which sizeofPropertyInfo catches on its own: a
 *    package built before it is refused and rebuilt.
 *
 * (no bump) Deki::PropertyInfo grew `enumInts`, the underlying value of each
 *    enumerator beside the existing names. Same reasoning: sizeofPropertyInfo
 *    moves, so a package built before it is refused and rebuilt rather than
 *    having its enum tables read as though a list position were a value.
 *
 * 6: engine 0.10 (the namespace release) moved every engine type into namespace Deki (Deki::Object ->
 *    Deki::Object, ...). Every sizeof is unchanged, but the symbol mangling of
 *    every exported engine function changed, so a 1.x package DLL would fail to
 *    load with unresolved imports; the bump turns that into the loader's
 *    "built against another engine, rebuilding" path instead.
 *
 * 7: engine 0.11 (macro-free components). Deki::Component lost six virtuals
 *    (GetType, GetBaseType, DeclaresUpdate, Deserialize, ResolveObjectRefs,
 *    RemapObjectRefs are now plain calls through the instance's
 *    ComponentTypeInfo pointer, which also grew the class by one pointer), and
 *    a component's identity is the hash of its qualified class name instead of
 *    a declared GUID. sizeofDekiComponent catches the size, the version bump
 *    catches the vtable.
 *
 * 8: engine 0.12 dropped Deki::Behaviour::NeedsUpdate() (a virtual) and its
 *    flag. Declaring Update() is the only thing that makes it run. A slot
 *    left the Behaviour vtable, so a v7 package would call the wrong function
 *    through every virtual after it.
 *
 * 9: engine 0.13 merged Deki::Behaviour into Deki::Component. Component gained
 *    Awake/Start/Update and two state flags, lost AsBehaviour(), and
 *    Behaviour is gone: every package rebuilds against the one base.
 *
 * 10: added transformConfig. The transform width became a build switch
 *    (DEKI_TRANSFORM_2D / DEKI_TRANSFORM_3D), so Deki::Object's layout is
 *    now a property of the project's configuration and the handshake has to
 *    say which one a DLL was built for. Growing the struct is itself the
 *    layout change that forces the bump.
 *
 * 11: Deki::IMemoryProvider gained AllocateDMA. Memory::Region::DMA promised a
 *    buffer a peripheral could read, and only the platform knows how to get
 *    one - on the ESP32 some internal RAM is not DMA-reachable, so
 *    "internal and probably fine" was silent corruption waiting for a
 *    display driver. Adding the virtual moves every slot after it, so a
 *    provider DLL built at ABI 10 would call the wrong one. No struct grew,
 *    which is exactly the case the size fingerprint cannot see and this
 *    number exists for.
 *
 * 12: Deki::IMemoryProvider gained AllocateRaw, FreeRaw and GetRawBlockSize,
 *    the plain natural-alignment allocator the global operator new routes
 *    through, so every allocation on a device goes through the provider
 *    without paying for a bookkeeping header. Appended, so ABI 11's slots
 *    keep their indices - but 11 was published to master for a few hours and
 *    package DLLs were built against it, and those would call into the
 *    provider past the end of the vtable they know. Two numbers inside one
 *    unreleased cycle is cheaper than one silent miscall.
 *
 * 13: Deki::Memory's allocating functions take a Deki::AllocContext instead of
 *    a const char*, so an allocation names itself without the caller writing
 *    a label. Nothing grew and no vtable moved - AllocContext is a single
 *    pointer in a release build, the same size the parameter always was - but
 *    the mangled names of Allocate, AllocateInternal and Free all changed, so
 *    a package DLL built at ABI 12 would not resolve them. The size
 *    fingerprint cannot see a parameter type change either, which is the
 *    second time this cycle that is the reason for a bump.
 *
 *    Also in 13: IMemoryProvider::AllocateDma is spelled AllocateDMA, and the
 *    placement virtuals (AllocateInternal, AllocateDMA, FreeInternal,
 *    GetAvailableInternalRAM) lost their default implementations. Neither
 *    moves a slot - a rename keeps its index and a pure virtual keeps its
 *    index - so both are source breaks rather than binary ones, and they ride
 *    along on a number this cycle had already bumped. Unlike 11, ABI 13 never
 *    reached master, so nothing was ever built against an earlier spelling of
 *    it.
 *
 *    AllocContext also has a different LAYOUT between debug and release: it
 *    carries file, function and line only where DEKI_ALLOC_CONTEXT_CAPTURE is
 *    defined. Mixing a debug engine with release packages was already
 *    unsupported (MemoryStats has done this for longer), and this makes the
 *    consequence sharper rather than introducing it.
 *
 * 14: Memory regions replace the MemoryUse enum and IMemoryProvider serves
 *    them through Allocate/Free/GetAvailable/Serves instead of a virtual per
 *    region, so a package can define a region the engine has never heard of.
 *
 * 15: MemoryRegion is Memory::Region, nested where it belongs.
 *
 * 16: IDisplay::Present and PresentRegions take a ColorFormat instead of an
 *    int whose numbering lived in a comment and as literals in every display.
 *
 * 17: packages moved their types into a namespace of their own, so every
 *    component is identified by a qualified name rather than a bare one and
 *    nothing of a package's sits at global scope. No layout moved, but the
 *    mangling of anything naming a package type did - IRenderSystem's
 *    SetRenderer and GetRenderer take DekiRendering::DekiRenderer now - so a
 *    package built before this fails to resolve against the engine's symbols.
 *    The bump turns that into the loader's "rebuild it" path.
 */
inline constexpr uint32_t kDekiReflectionAbiVersion = 17;

/** @brief The values as compiled into whoever calls this. */
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

/**
 * @brief Compare a loaded DLL's values against ours.
 * @param what  Buffer receiving a description of the FIRST mismatch (for the log).
 * @return true when the layouts match and the DLL is safe to use.
 */
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
                std::snprintf(what, whatSize, "%s is %u there, %u here", f.name, f.theirs, f.mine);
            return false;
        }
    }
    return true;
}

// The symbol every package DLL and the game plugin export (ReflectionAbi.cpp).
//
// A MISSING export only tells you the DLL predates this handshake, so the loaders
// warn and continue: refusing on absence would reject every DLL built before the
// guard shipped, which is all of them on the build that introduces it.
//
// On a real mismatch the loaders skip the DLL but must NOT unload it. Static
// initializers have already run by then and handed engine-side registries
// function pointers into the DLL's code (editor extensions, node types,
// Deki::SetupComponent auto-factories), so FreeLibrary leaves those dangling and the
// next call through one jumps into unmapped memory.
#define DEKI_REFLECTION_ABI_EXPORT_NAME "DekiPlugin_GetReflectionAbi"

extern "C"
{
    typedef const DekiReflectionAbi* (*DekiGetReflectionAbiFunc)();
}
