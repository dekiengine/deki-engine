// Self-contained reflection codegen tool: discovers every class deriving from
// Deki::Component in the included package headers and writes the package's
// reflection tables using C++26 static reflection. A component declares nothing
// for this: its identity is its qualified class name (see deki/TypeId.h) and the
// optional DEKI_CATEGORY / DEKI_DESCRIPTION markers sit above the class. Build with: g++ -std=c++26 -freflection
// -DDEKI_REFLECT ...
#include <cstdint>
#include <cstddef>
#include <string>
#include <deki/reflection/Property.h>  // deki_refl namespace + DEKI_* macros (under DEKI_REFLECT)
#include <deki/Vector.h>               // ::Deki::Vector2 / ::Deki::Vector3
#include <deki/Color.h>
#include <deki/assets/AssetRef.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/reflection/NodeRef.h>
#include <deki/reflection/PropertyRef.h>

// ---- work unit ----
// One generator TU is one work unit. A unit carries one or more packages and one
// shard of their components:
//   - small packages share a unit, so the fixed front-end cost (parsing this file
//     plus the reflection headers, ~2s) is paid once instead of once per package;
//   - a large package is split across several units by shard, so its per-component
//     emit work (the dominant cost, ~2/3 of a big package's codegen) runs in
//     parallel instead of serially inside one TU.
// The CMake step (DekiReflectionCodegen.cmake) plans the units and writes the
// descriptor table below into deki_codegen_includes.inc.
struct CodegenPackage
{
    const char* tag;              // package dir name; a type belongs to this package when its
                                  // source path contains the tag
    const char* prefix;           // {Prefix}Registration.gen.cpp + {Prefix}_Register* symbols
    const char* srcdir;           // component #includes are emitted relative to this
    const char* outdir;           // where this package's .gen.h/.gen.cpp are written
    const char* const* adapters;  // explicit adapter registration functions (scanned from .cpp)
    int adapterCount;
};

#include "deki_codegen_includes.inc"  // unit config + every component header in the unit
#include <meta>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <system_error>
#include <sstream>
#include <cstdlib>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>
#include <utility>
#include <algorithm>
#include <charconv>

// kPackages / kPackageCount / the shard pair all come from the .inc. Missing means the
// CMake step and this file are out of sync - fail loudly rather than guess a default.
#if !defined(DEKI_CODEGEN_SHARD_INDEX) || !defined(DEKI_CODEGEN_SHARD_COUNT)
#error "deki_codegen_includes.inc must define DEKI_CODEGEN_SHARD_INDEX and DEKI_CODEGEN_SHARD_COUNT"
#endif

namespace m = std::meta;

// Every declaration reachable from the global namespace, recursively. Components
// can live in any namespace (the engine's in Deki, a project's in whatever it
// likes), so the walk descends into all of them except library namespaces, which
// hold nothing of ours and would multiply the work (std alone is enormous).
consteval bool skip_namespace(m::info ns)
{
    if (!m::has_identifier(ns))
    {
        return true;  // anonymous: nothing reflectable lives there
    }
    std::string_view id = m::identifier_of(ns);
    return id == "std" || id == "deki_refl" || id == "nlohmann" || id == "__gnu_cxx" || id == "__cxxabiv1" ||
           (id.size() >= 2 && id[0] == '_' && id[1] == '_');
}
consteval void collect_scope(m::info ns, std::vector<m::info>& out)
{
    for (m::info x : m::members_of(ns, m::access_context::unchecked()))
    {
        out.push_back(x);
        if (m::is_namespace(x) && !skip_namespace(x))
        {
            collect_scope(x, out);
        }
    }
}
consteval std::vector<m::info> reflected_scope_members()
{
    std::vector<m::info> all;
    collect_scope(^^::, all);
    return all;
}

// The namespace a reflected type is declared in ("" for the global namespace),
// one level: the DEKI_SERIALIZABLE and DEKI_NODE emitters define members and
// open that namespace around them.
consteval const char* namespace_of(m::info t)
{
    m::info p = m::parent_of(t);
    if (m::is_namespace(p) && p != ^^::)
    {
        return std::define_static_string(m::identifier_of(p));
    }
    return "";
}

// Is every enclosing scope of t a named namespace? Components nested in a class
// or declared in an anonymous namespace have no usable qualified name.
consteval bool scope_is_reflectable(m::info t)
{
    for (m::info p = m::parent_of(t); p != ^^::; p = m::parent_of(p))
    {
        if (!m::is_namespace(p) || !m::has_identifier(p))
        {
            return false;
        }
    }
    return true;
}

// The fully qualified name of a type: "Game::Health", or "Health" at global scope.
// This is the spelling Deki::TypeName<T>() produces at compile time; generated
// code emits ::Deki::TypeName<Game::Health>() rather than the literal, so the
// on-disk key can never drift from the runtime id.
consteval std::string qualified_name_str(m::info t)
{
    std::string s(m::identifier_of(t));
    for (m::info p = m::parent_of(t); m::is_namespace(p) && p != ^^::; p = m::parent_of(p))
    {
        s = std::string(m::identifier_of(p)) + "::" + s;
    }
    return s;
}
consteval const char* qualified_name(m::info t)
{
    return std::define_static_string(qualified_name_str(t));
}
// The namespace a type sits in ("Deki2D"), or "" at global scope. Generated
// definitions of free functions reopen it, so they match the declaration the
// generated header emitted next to the type rather than adding a second
// overload at global scope that ADL then finds ambiguous.
consteval std::string enclosing_namespace_str(m::info t)
{
    std::string s;
    for (m::info p = m::parent_of(t); m::is_namespace(p) && p != ^^::; p = m::parent_of(p))
    {
        s = s.empty() ? std::string(m::identifier_of(p)) : std::string(m::identifier_of(p)) + "::" + s;
    }
    return s;
}
consteval const char* enclosing_namespace(m::info t)
{
    return std::define_static_string(enclosing_namespace_str(t));
}
// "Game::Health" -> "Game_Health": the stem of every generated symbol for a type.
consteval const char* symbol_stem(m::info t)
{
    std::string s = qualified_name_str(t);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == ':')
        {
            if (out.empty() || out.back() != '_')
            {
                out += '_';
            }
            continue;
        }
        out += s[i];
    }
    return std::define_static_string(out);
}
static constexpr const char* SEP = "// =============================================================================";

// ---- type predicates ----
consteval m::info bare(m::info t)
{
    return m::dealias(m::remove_cv(t));
}
consteval bool is_enum(m::info t)
{
    return m::is_enum_type(bare(t));
}
consteval bool is_color(m::info t)
{
    return bare(t) == ^^Deki::Color;
}
consteval bool is_float(m::info t)
{
    return bare(t) == ^^float;
}
consteval bool is_double(m::info t)
{
    return bare(t) == ^^double;
}
consteval bool is_bool(m::info t)
{
    return bare(t) == ^^bool;
}
consteval bool is_string(m::info t)
{
    return bare(t) == m::dealias(^^std::string);
}
consteval bool is_vec2(m::info t)
{
    return bare(t) == ^^Deki::Vector2;
}
consteval bool is_vec3(m::info t)
{
    return bare(t) == ^^Deki::Vector3;
}
consteval bool is_assetref(m::info t)
{
    return m::has_template_arguments(bare(t)) && m::template_of(bare(t)) == ^^Deki::AssetRef;
}
consteval bool is_objectref(m::info t)
{
    return m::has_template_arguments(bare(t)) && m::template_of(bare(t)) == ^^Deki::ObjectRef;
}
consteval bool is_noderef(m::info t)
{
    return bare(t) == ^^Deki::NodeRef;
}
consteval bool is_propertyref(m::info t)
{
    return bare(t) == ^^Deki::PropertyRef;
}
consteval bool is_vector(m::info t)
{
    return m::has_template_arguments(bare(t)) && m::template_of(bare(t)) == ^^std::vector;
}
consteval m::info vec_elem(m::info t)
{
    return m::template_arguments_of(bare(t))[0];
}
consteval bool is_int_like(m::info t)
{
    m::info d = bare(t);
    return d == m::dealias(^^int32_t) || d == m::dealias(^^uint32_t) || d == m::dealias(^^int16_t) ||
           d == m::dealias(^^uint16_t) || d == m::dealias(^^int8_t) || d == m::dealias(^^uint8_t);
}
consteval const char* int_name(m::info d)
{
    if (d == m::dealias(^^int32_t))
    {
        return "Int32";
    }
    if (d == m::dealias(^^uint32_t))
    {
        return "UInt32";
    }
    if (d == m::dealias(^^int16_t))
    {
        return "Int16";
    }
    if (d == m::dealias(^^uint16_t))
    {
        return "UInt16";
    }
    if (d == m::dealias(^^int8_t))
    {
        return "Int8";
    }
    if (d == m::dealias(^^uint8_t))
    {
        return "UInt8";
    }
    return "Unknown";
}
consteval const char* deki_type_name(m::info t)
{
    if (is_assetref(t))
    {
        return "AssetRef";
    }
    if (is_objectref(t))
    {
        return "ObjectRef";
    }
    if (is_noderef(t))
    {
        return "NodeRef";
    }
    if (is_propertyref(t))
    {
        return "PropertyRef";
    }
    if (is_vector(t))
    {
        return "Array";
    }
    if (is_color(t))
    {
        return "Color";
    }
    if (is_vec3(t))
    {
        return "Vector3";
    }
    if (is_vec2(t))
    {
        return "Vector2";
    }
    if (is_enum(t))
    {
        return "Enum";
    }
    if (is_float(t))
    {
        return "Float";
    }
    if (is_double(t))
    {
        return "Double";
    }
    if (is_bool(t))
    {
        return "Bool";
    }
    if (is_string(t))
    {
        return "String";
    }
    return int_name(bare(t));
}
consteval const char* elem_type_name(m::info e)
{
    if (is_objectref(e))
    {
        return "ObjectRef";
    }
    if (is_string(e))
    {
        return "String";
    }
    if (is_float(e))
    {
        return "Float";
    }
    return int_name(bare(e));
}
consteval const char* asset_arg(m::info t)
{
    if (is_assetref(t))
    {
        return std::define_static_string(m::identifier_of(m::template_arguments_of(bare(t))[0]));
    }
    return nullptr;
}
consteval const char* component_ref(m::info t)
{
    if (is_objectref(t))
    {
        return std::define_static_string(m::identifier_of(m::template_arguments_of(bare(t))[0]));
    }
    if (is_vector(t) && is_objectref(vec_elem(t)))
    {
        return std::define_static_string(m::identifier_of(m::template_arguments_of(bare(vec_elem(t)))[0]));
    }
    return nullptr;
}
consteval const char* enum_name(m::info t)
{
    return std::define_static_string(m::identifier_of(bare(t)));
}
consteval bool is_objref_like(m::info t)
{
    return is_objectref(t) || (is_vector(t) && is_objectref(vec_elem(t)));
}

consteval bool has_static_member(m::info type, std::string_view name)
{
    for (m::info mem : m::members_of(type, m::access_context::unchecked()))
    {
        if (m::has_identifier(mem) && m::identifier_of(mem) == name)
        {
            return true;
        }
    }
    return false;
}
// Does the class itself declare an Update() member (of any kind)? Direct
// members only; declares_update_rec walks the component bases.
consteval bool declares_update(m::info type)
{
    return has_static_member(type, "Update");
}
// A component is any complete class deriving from Deki::Component, other than
// the two engine bases themselves.
consteval bool is_component(m::info type)
{
    if (!m::is_class_type(type) || !m::is_complete_type(type))
    {
        return false;
    }
    if (type == ^^Deki::Component)
    {
        return false;
    }
    return m::is_base_of_type(^^Deki::Component, type);
}
// The direct base that carries the component identity: Deki::Component or
// another component. GetComponent<Base>() matches on it.
consteval m::info component_base(m::info cls)
{
    for (m::info b : m::bases_of(cls, m::access_context::unchecked()))
    {
        m::info bt = bare(m::type_of(b));
        if (bt == ^^Deki::Component || is_component(bt))
        {
            return bt;
        }
    }
    return ^^Deki::Component;
}
consteval bool component_base_is_public(m::info cls)
{
    for (m::info b : m::bases_of(cls, m::access_context::unchecked()))
    {
        m::info bt = bare(m::type_of(b));
        if (bt == ^^Deki::Component || is_component(bt))
        {
            return m::is_public(b);
        }
    }
    return false;
}
consteval bool declares_update_rec(m::info cls)
{
    if (declares_update(cls))
    {
        return true;
    }
    m::info b = component_base(cls);
    return is_component(b) && declares_update_rec(b);
}
// DEKI_NODE struct: has a StaticNodeName marker and is NOT a component. Disjoint
// from both components and DEKI_SERIALIZABLE structs, so a node gets its own emit
// path (reflection + node-factory registration).
consteval bool is_node(m::info type)
{
    return m::is_class_type(type) && m::is_complete_type(type) && has_static_member(type, "StaticNodeName") &&
           !is_component(type);
}
// Index into kPackages of the package a reflected type belongs to, or -1 when the type
// came from outside this unit (engine headers, a sibling package pulled in transitively).
// Membership is "the package's tag appears in the type's source path" - the same rule the
// single-package build has always used, just applied to every package in the unit.
// -2 means the path matched two tags; the caller static_asserts on it.
// A tag is a package's directory name, so it matches a path only as a whole
// path component: "deki-input" is not in ".../deki-input-embedded/Keyboard.h".
// A bare substring search said it was, and the unit then failed as matching
// two packages.
consteval bool path_has_component(std::string_view path, std::string_view tag)
{
    size_t pos = 0;
    while ((pos = path.find(tag, pos)) != std::string_view::npos)
    {
        const bool starts = pos == 0 || path[pos - 1] == '/' || path[pos - 1] == '\\';
        const size_t end = pos + tag.size();
        const bool ends = end == path.size() || path[end] == '/' || path[end] == '\\';
        if (starts && ends)
        {
            return true;
        }
        ++pos;
    }
    return false;
}

consteval int package_index_of(m::info decl)
{
    std::string_view f = m::source_location_of(decl).file_name();
    int found = -1;
    for (int i = 0; i < kPackageCount; ++i)
    {
        if (path_has_component(f, kPackages[i].tag))
        {
            if (found >= 0)
            {
                return -2;
            }
            found = i;
        }
    }
    return found;
}

// Shard filter: which of a unit's components this TU actually emits. Hashing the type
// name (rather than counting) keeps the split stable as components are added or removed,
// so an unrelated new component doesn't shuffle every other component into a different
// shard and rewrite the whole generated tree.
consteval bool in_shard(std::string_view name)
{
    if constexpr (DEKI_CODEGEN_SHARD_COUNT <= 1)
    {
        return true;
    }
    else
    {
        uint64_t h = 1469598103934665603ull;
        for (char c : name)
        {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ull;
        }
        return static_cast<int>(h % static_cast<uint64_t>(DEKI_CODEGEN_SHARD_COUNT)) == DEKI_CODEGEN_SHARD_INDEX;
    }
}

// (see rel_src below, defined after src_rel_include)

// ---- annotation readers ----
consteval bool is_exported(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::exported)
        {
            return true;
        }
    }
    return false;
}
consteval bool is_editor_only(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::editor_only)
        {
            return true;
        }
    }
    return false;
}
consteval bool is_serializable(m::info type)
{
    for (m::info a : m::annotations_of(type))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::serializable)
        {
            return true;
        }
    }
    return false;
}
consteval bool has_no_serialize(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::no_serialize)
        {
            return true;
        }
    }
    return false;
}
// nlohmann j.value() default literal per scalar type
consteval const char* json_default(m::info t)
{
    if (is_string(t))
    {
        return "\"\"";
    }
    if (is_bool(t))
    {
        return "false";
    }
    if (is_float(t) || is_double(t))
    {
        return "0.0f";
    }
    return "0";
}
consteval std::vector<m::info> ser_members(m::info cls)
{
    std::vector<m::info> out;
    for (m::info mem : m::nonstatic_data_members_of(cls, m::access_context::unchecked()))
    {
        if (!has_no_serialize(mem))
        {
            out.push_back(mem);
        }
    }
    return out;
}
// basename of a path (after the last / or \)
inline std::string_view path_basename(std::string_view p)
{
    size_t s = p.find_last_of("/\\");
    return s == std::string_view::npos ? p : p.substr(s + 1);
}

// Write only when the bytes differ from what is already on disk. Every package source
// includes the package's .gen.h files, so an unconditional rewrite bumps their mtime and
// invalidates hundreds of objects on every codegen re-run - even when the generated text
// is identical, which is the normal case when the re-run was triggered by an unrelated
// engine header.
inline void write_if_changed(const std::string& path, const std::string& content)
{
    {
        std::ifstream in(path);
        if (in)
        {
            std::string cur((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (cur == content)
            {
                return;
            }
        }
    }
    std::ofstream out(path);
    out << content;
    if (!out)
    {
        std::cerr << "reflection codegen: failed to write " << path << "\n";
        std::exit(1);
    }
}
// component #include: path relative to the package's source dir (so subdir components
// like settings/Foo.h get "settings/Foo.h"); falls back to basename.
inline std::string src_rel_include(std::string_view full, std::string_view sd)
{
    if (!sd.empty() && full.size() > sd.size() && full.substr(0, sd.size()) == sd)
    {
        std::string_view r = full.substr(sd.size());
        while (!r.empty() && (r.front() == '/' || r.front() == '\\'))
        {
            r.remove_prefix(1);
        }
        return std::string(r);
    }
    return std::string(path_basename(full));
}

// The "// Source:" path for a generated file's header comment: relative to the
// package's source dir, with forward slashes on every host.
//
// This used to emit the absolute path using the host's native separator, so the
// same component produced
//   // Source: C:\work\...\deki-engine\src\SetupComponent.h            (Windows)
//   // Source: /home/deck/...//deki-engine/src/SetupComponent.h      (Linux)
// Generated files are committed - embedded builds glob them and never run the
// codegen - so every build on a different machine rewrote all of them and the
// churn landed in every diff. Both halves matter: making the path relative
// removes the machine prefix, and normalising the separator removes the
// remaining Windows/POSIX difference.
inline std::string rel_src(std::string_view full, std::string_view sd)
{
    std::string s = src_rel_include(full, sd);
    for (char& c : s)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    return s;
}

consteval const char* unit_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::unit)
        {
            return m::extract<Deki::deki_refl::unit>(a).name;
        }
    }
    return nullptr;
}
consteval const char* vw_prop(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::visible_when)
        {
            return m::extract<Deki::deki_refl::visible_when>(a).prop;
        }
    }
    return nullptr;
}
consteval const char* vw_vals(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::visible_when)
        {
            return m::extract<Deki::deki_refl::visible_when>(a).values;
        }
    }
    return nullptr;
}
consteval const char* tooltip_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::tooltip)
        {
            return m::extract<Deki::deki_refl::tooltip>(a).text;
        }
    }
    return nullptr;
}
consteval const char* group_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::group)
        {
            return m::extract<Deki::deki_refl::group>(a).name;
        }
    }
    return nullptr;
}
consteval const char* toggle_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::toggle_object)
        {
            return m::extract<Deki::deki_refl::toggle_object>(a).field;
        }
    }
    return nullptr;
}
// DEKI_OBJECT_NAME on a std::string field: the picker's component filter
// ("" = any object). Non-null is what marks the field as an object name.
consteval const char* objname_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::object_name)
        {
            return m::extract<Deki::deki_refl::object_name>(a).component;
        }
    }
    return nullptr;
}
// DEKI_VALUE_OF on a std::string field: the ::Deki::PropertyRef field whose target
// types this literal's editor control.
consteval const char* valueof_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::value_of)
        {
            return m::extract<Deki::deki_refl::value_of>(a).prop;
        }
    }
    return nullptr;
}
// DEKI_RENAMED_FROM: the key this member used to be serialised under.
consteval const char* renamed_from_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::renamed_from)
        {
            return m::extract<Deki::deki_refl::renamed_from>(a).name;
        }
    }
    return nullptr;
}
consteval bool has_range(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::range)
        {
            return true;
        }
    }
    return false;
}
consteval const char* range_lo(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::range)
        {
            return m::extract<Deki::deki_refl::range>(a).lo;
        }
    }
    return nullptr;
}
consteval const char* range_hi(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::range)
        {
            return m::extract<Deki::deki_refl::range>(a).hi;
        }
    }
    return nullptr;
}
consteval bool range_slider(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
    {
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::range)
        {
            return m::extract<Deki::deki_refl::range>(a).slider;
        }
    }
    return false;
}
// Skip sentinel enumerators (COUNT/MAX/NUM/SIZE/LAST, NUM_*/MAX_*), like Python's scan_for_enums.
consteval bool is_sentinel(std::string_view n)
{
    std::string u;
    for (char c : n)
    {
        u += (c >= 'a' && c <= 'z') ? char(c - 32) : c;
    }
    if (u == "COUNT" || u == "MAX" || u == "NUM" || u == "SIZE" || u == "LAST")
    {
        return true;
    }
    if (u.size() >= 4 && (u.substr(0, 4) == "NUM_" || u.substr(0, 4) == "MAX_"))
    {
        return true;
    }
    return false;
}
// runtime helpers for DEKI_RANGE/DEKI_SLIDER (stringized args), matching Python:
// only simple numeric literals produce a range; floats format like Python str(float).
inline std::string _trim_f(std::string s)
{
    while (!s.empty() && s.front() == ' ')
    {
        s.erase(s.begin());
    }
    while (!s.empty() && s.back() == ' ')
    {
        s.pop_back();
    }
    if (!s.empty() && (s.back() == 'f' || s.back() == 'F'))
    {
        s.pop_back();
    }
    return s;
}
inline bool is_simple_num(const char* lit)
{
    std::string s = _trim_f(lit);
    if (s.empty())
    {
        return false;
    }
    size_t i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (i >= s.size())
    {
        return false;
    }
    for (; i < s.size(); ++i)
    {
        if (!((s[i] >= '0' && s[i] <= '9') || s[i] == '.'))
        {
            return false;
        }
    }
    return true;
}
inline std::string py_float(const char* lit)
{
    double v = std::stod(_trim_f(lit));
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), v);  // shortest, may be scientific
    std::string s(buf, r.ptr);
    auto epos = s.find('e');
    if (epos == std::string::npos)
    {  // already fixed
        if (s.find('.') == std::string::npos)
        {
            s += ".0";
        }
        return s;
    }
    // expand scientific -> fixed (Python str(float) uses fixed for normal magnitudes)
    bool neg = s[0] == '-';
    std::string m = s.substr(neg ? 1 : 0, epos - (neg ? 1 : 0));
    int exp = std::stoi(s.substr(epos + 1));
    auto dot = m.find('.');
    std::string digits = (dot == std::string::npos) ? m : m.substr(0, dot) + m.substr(dot + 1);
    int intlen = (dot == std::string::npos) ? (int)m.size() : (int)dot;
    int pointpos = intlen + exp;
    std::string out;
    if (pointpos <= 0)
    {
        out = "0." + std::string(-pointpos, '0') + digits;
    }
    else if (pointpos >= (int)digits.size())
    {
        out = digits + std::string(pointpos - (int)digits.size(), '0') + ".0";
    }
    else
    {
        out = digits.substr(0, pointpos) + "." + digits.substr(pointpos);
    }
    if (out.find('.') == std::string::npos)
    {
        out += ".0";
    }
    return (neg ? "-" : "") + out;
}
consteval int enum_count(m::info t)
{
    int n = 0;
    for (m::info e : m::enumerators_of(bare(t)))
    {
        if (!is_sentinel(m::identifier_of(e)))
        {
            ++n;
        }
    }
    return n;
}

// ---- visibility ----
consteval m::info member_type_by_name(m::info cls, std::string_view name)
{
    for (m::info mem : m::nonstatic_data_members_of(cls, m::access_context::unchecked()))
    {
        if (m::identifier_of(mem) == name)
        {
            return m::type_of(mem);
        }
    }
    return ^^void;
}
consteval int enum_ordinal(m::info enumType, std::string_view name)
{
    int i = 0;
    for (m::info e : m::enumerators_of(bare(enumType)))
    {
        if (is_sentinel(m::identifier_of(e)))
        {
            continue;
        }
        if (m::identifier_of(e) == name)
        {
            return i;
        }
        ++i;
    }
    return -1;
}
consteval bool is_numeric_tok(std::string_view t)
{
    size_t i = 0;
    if (i < t.size() && t[i] == '-')
    {
        ++i;
    }
    if (i >= t.size())
    {
        return false;
    }
    for (; i < t.size(); ++i)
    {
        if (t[i] < '0' || t[i] > '9')
        {
            return false;
        }
    }
    return true;
}
consteval int atoi_tok(std::string_view t)
{
    int sign = 1;
    size_t i = 0;
    if (i < t.size() && t[i] == '-')
    {
        sign = -1;
        ++i;
    }
    int v = 0;
    for (; i < t.size(); ++i)
    {
        v = v * 10 + (t[i] - '0');
    }
    return v * sign;
}
struct VisInfo
{
    int min;
    uint32_t mask;
};
consteval VisInfo resolve_vis(m::info ctrlType, std::string_view vals)
{
    int resolved[16];
    int n = 0;
    bool hasEnum = false;
    size_t i = 0;
    while (i < vals.size())
    {
        while (i < vals.size() && (vals[i] == ' ' || vals[i] == ','))
        {
            ++i;
        }
        size_t s = i;
        while (i < vals.size() && vals[i] != ',')
        {
            ++i;
        }
        size_t e = i;
        while (e > s && vals[e - 1] == ' ')
        {
            --e;
        }
        if (e > s)
        {
            std::string_view tok = vals.substr(s, e - s);
            if (is_numeric_tok(tok))
            {
                resolved[n++] = atoi_tok(tok);
            }
            else
            {
                hasEnum = true;
                resolved[n++] = enum_ordinal(ctrlType, tok);
            }
        }
    }
    VisInfo vi{ 0, 0 };
    if (n == 1 && !hasEnum)
    {
        vi.min = resolved[0];
        vi.mask = 0;
    }
    else
    {
        for (int k = 0; k < n; ++k)
        {
            vi.mask |= (1u << resolved[k]);
        }
    }
    return vi;
}

// ---- markers above the class (DEKI_CATEGORY, DEKI_DESCRIPTION, ...) ----
// Under DEKI_REFLECT each marker expands to a namespace-scope constant of a
// deki_refl tag type. A marker belongs to the first component declared after it
// in the same file; one that no component follows is reported as an error.
enum TagKind
{
    TAG_NONE = 0,
    TAG_CATEGORY,
    TAG_DESCRIPTION,
    TAG_DISPLAY_NAME,
    TAG_SETTINGS_SECTION,
    TAG_FORMER_NAME
};
consteval int tag_kind(m::info x)
{
    if (!m::is_variable(x))
    {
        return TAG_NONE;
    }
    m::info t = bare(m::type_of(x));
    if (t == ^^Deki::deki_refl::category_tag)
    {
        return TAG_CATEGORY;
    }
    if (t == ^^Deki::deki_refl::description_tag)
    {
        return TAG_DESCRIPTION;
    }
    if (t == ^^Deki::deki_refl::display_name_tag)
    {
        return TAG_DISPLAY_NAME;
    }
    if (t == ^^Deki::deki_refl::settings_section_tag)
    {
        return TAG_SETTINGS_SECTION;
    }
    if (t == ^^Deki::deki_refl::former_name_tag)
    {
        return TAG_FORMER_NAME;
    }
    return TAG_NONE;
}
consteval const char* tag_text(m::info x)
{
    switch (tag_kind(x))
    {
        case TAG_CATEGORY: return m::extract<Deki::deki_refl::category_tag>(x).text;
        case TAG_DESCRIPTION: return m::extract<Deki::deki_refl::description_tag>(x).text;
        case TAG_DISPLAY_NAME: return m::extract<Deki::deki_refl::display_name_tag>(x).text;
        case TAG_SETTINGS_SECTION: return m::extract<Deki::deki_refl::settings_section_tag>(x).text;
        case TAG_FORMER_NAME: return m::extract<Deki::deki_refl::former_name_tag>(x).text;
        default: return nullptr;
    }
}
// The walk is expensive and consteval functions are not memoised, so the lists
// every lookup needs are materialised once.
constexpr std::span<const m::info> kScope = std::define_static_array(reflected_scope_members());
consteval std::vector<m::info> all_components()
{
    std::vector<m::info> out;
    for (m::info x : kScope)
    {
        if (m::is_type(x) && is_component(x))
        {
            out.push_back(x);
        }
    }
    return out;
}
consteval std::vector<m::info> all_tags()
{
    std::vector<m::info> out;
    for (m::info x : kScope)
    {
        if (tag_kind(x) != TAG_NONE)
        {
            out.push_back(x);
        }
    }
    return out;
}
constexpr std::span<const m::info> kComponents = std::define_static_array(all_components());
constexpr std::span<const m::info> kTags = std::define_static_array(all_tags());

consteval bool same_file(m::info a, m::info b)
{
    return std::string_view(m::source_location_of(a).file_name()) ==
           std::string_view(m::source_location_of(b).file_name());
}
// The component a marker attaches to (null reflection when none follows it).
consteval m::info tag_owner(m::info tag)
{
    m::info best{};
    unsigned bestLine = ~0u;
    const unsigned tl = m::source_location_of(tag).line();
    for (m::info c : kComponents)
    {
        if (!same_file(tag, c))
        {
            continue;
        }
        const unsigned cl = m::source_location_of(c).line();
        if (cl > tl && cl < bestLine)
        {
            bestLine = cl;
            best = c;
        }
    }
    return best;
}
consteval const char* tag_text_of(m::info comp, int kind)
{
    for (m::info t : kTags)
    {
        if (tag_kind(t) == kind && tag_owner(t) == comp)
        {
            return tag_text(t);
        }
    }
    return nullptr;
}

// Every marker of one kind on a component, in source order. DEKI_FORMER_NAME is
// repeatable: a class renamed twice keeps one line per previous name, so a scene
// written before either rename still resolves.
consteval std::vector<const char*> tag_texts_of(m::info comp, int kind)
{
    std::vector<const char*> out;
    for (m::info t : kTags)
    {
        if (tag_kind(t) == kind && tag_owner(t) == comp)
        {
            out.push_back(tag_text(t));
        }
    }
    return out;
}

// ---- member collection ----
consteval std::vector<m::info> own_exported(m::info cls)
{
    std::vector<m::info> out;
    for (m::info mem : m::nonstatic_data_members_of(cls, m::access_context::unchecked()))
    {
        if (is_exported(mem))
        {
            out.push_back(mem);
        }
    }
    return out;
}
consteval void collect_base_exported(m::info cls, std::vector<m::info>& out)
{
    for (m::info base : m::bases_of(cls, m::access_context::unchecked()))
    {
        m::info bt = m::type_of(base);
        if (is_component(bt))
        {
            for (m::info mem : m::nonstatic_data_members_of(bt, m::access_context::unchecked()))
            {
                if (is_exported(mem))
                {
                    out.push_back(mem);
                }
            }
            collect_base_exported(bt, out);
        }
    }
}
consteval std::vector<m::info> deser_members(m::info cls)
{
    std::vector<m::info> out = own_exported(cls);
    collect_base_exported(cls, out);
    return out;
}
consteval bool any_editor_only(m::info cls)
{
    for (m::info mem : deser_members(cls))
    {
        if (is_editor_only(mem))
        {
            return true;
        }
    }
    return false;
}
consteval bool has_component_base(m::info cls)
{
    return is_component(component_base(cls));
}
consteval int count_assetref(m::info cls)
{
    int n = 0;
    for (m::info mem : m::nonstatic_data_members_of(cls, m::access_context::unchecked()))
    {
        if (is_exported(mem) && is_assetref(m::type_of(mem)))
        {
            ++n;
        }
    }
    return n;
}
consteval int count_objectref(m::info cls)
{
    int n = 0;
    for (m::info mem : m::nonstatic_data_members_of(cls, m::access_context::unchecked()))
    {
        if (is_exported(mem) && is_objref_like(m::type_of(mem)))
        {
            ++n;
        }
    }
    return n;
}

template <m::info MEM>
void emit_case(std::ostream& out)
{
    constexpr m::info t = m::type_of(MEM);
    constexpr const char* nm = std::define_static_string(m::identifier_of(MEM));
    const std::string fld = std::string("self.") + nm;  // the field, reached from the free function
    constexpr const char* rf = renamed_from_of(MEM);
    if constexpr (rf != nullptr)
    {
        if constexpr (std::string_view(rf) == std::string_view(nm))
        {
            out << "#error DEKI_RENAMED_FROM on " << nm << " names its current identifier\n";
        }
        // Stacked label: the old key falls into the same case body as the new one.
        out << "            case ::Deki::HashString(\"" << rf << "\"):  // DEKI_RENAMED_FROM\n";
    }
    if constexpr (is_assetref(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t mapSize;\n"
            << "                if (parser.ReadMapSize(mapSize)) {\n"
            << "                    for (uint32_t i = 0; i < mapSize; i++) {\n"
            << "                        const char* k; uint16_t kLen;\n"
            << "                        if (!parser.ReadString(k, kLen)) break;\n"
            << "                        uint32_t kHash = ::Deki::HashStringLen(k, kLen);\n"
            << "                        switch (kHash) {\n"
            << "                            case ::Deki::HashString(\"guid\"): { const char* s; uint16_t len; if "
               "(parser.ReadString(s, len)) "
            << fld << ".guid = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"source\"): { const char* s; uint16_t len; if "
               "(parser.ReadString(s, len)) "
            << fld << ".source = std::string(s, len); break; }\n"
            << "                            default: parser.SkipValue(); break;\n"
            << "                        }\n"
            << "                    }\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_propertyref(t))
    {
        // {object, component, field} as authored; the ids are derived once by Rehash.
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t mapSize;\n"
            << "                if (parser.ReadMapSize(mapSize)) {\n"
            << "                    for (uint32_t i = 0; i < mapSize; i++) {\n"
            << "                        const char* k; uint16_t kLen;\n"
            << "                        if (!parser.ReadString(k, kLen)) break;\n"
            << "                        uint32_t kHash = ::Deki::HashStringLen(k, kLen);\n"
            << "                        switch (kHash) {\n"
            << "                            case ::Deki::HashString(\"object\"): { const char* s; uint16_t len; if "
               "(parser.ReadString(s, len)) "
            << fld << ".object = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"component\"): { const char* s; uint16_t len; if "
               "(parser.ReadString(s, len)) "
            << fld << ".component = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"field\"): { const char* s; uint16_t len; if "
               "(parser.ReadString(s, len)) "
            << fld << ".field = std::string(s, len); break; }\n"
            << "                            default: parser.SkipValue(); break;\n"
            << "                        }\n"
            << "                    }\n"
            << "                    " << fld << ".Rehash();\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_objectref(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint8_t _pb;\n"
            << "                if (parser.PeekByte(_pb) && ((_pb & 0xf0) == 0x90 || _pb == 0xdc || _pb == 0xdd)) {\n"
            << "                    uint32_t _as; if (parser.ReadArraySize(_as) && _as >= 2) {\n"
            << "                        int32_t _id, _tid; parser.ReadInt(_id); parser.ReadInt(_tid);\n"
            << "                        " << fld << ".id = static_cast<uint32_t>(_id); " << fld
            << ".componentTypeId = static_cast<uint32_t>(_tid);\n"
            << "                        for (uint32_t _s = 2; _s < _as; _s++) parser.SkipValue();\n"
            << "                    }\n"
            << "                } else {\n"
            << "                    int32_t v; if (parser.ReadInt(v)) { " << fld << ".id = static_cast<uint32_t>(v); "
            << fld << ".componentTypeId = 0; }\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_vector(t))
    {
        constexpr m::info e = vec_elem(t);
        if constexpr (is_objectref(e))
        {
            out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    " << fld << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) {\n"
                << "                        decltype(" << fld << ")::value_type ref;\n"
                << "                        uint8_t _pb;\n"
                << "                        if (parser.PeekByte(_pb) && ((_pb & 0xf0) == 0x90 || _pb == 0xdc || _pb == "
                   "0xdd)) {\n"
                << "                            uint32_t _as; if (parser.ReadArraySize(_as) && _as >= 2) {\n"
                << "                                int32_t _id, _tid; parser.ReadInt(_id); parser.ReadInt(_tid);\n"
                << "                                ref.id = static_cast<uint32_t>(_id); ref.componentTypeId = "
                   "static_cast<uint32_t>(_tid);\n"
                << "                                for (uint32_t _s = 2; _s < _as; _s++) parser.SkipValue();\n"
                << "                            }\n"
                << "                        } else {\n"
                << "                            int32_t v;\n"
                << "                            if (parser.ReadInt(v)) { ref.id = static_cast<uint32_t>(v); "
                   "ref.componentTypeId = 0; }\n"
                << "                        }\n"
                << "                        " << fld << ".push_back(ref);\n"
                << "                    }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
        else if constexpr (is_string(e))
        {
            out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    " << fld << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) {\n"
                << "                        const char* s; uint16_t len;\n"
                << "                        if (parser.ReadString(s, len)) " << fld
                << ".push_back(std::string(s, len));\n"
                << "                    }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
        else if constexpr (is_float(e))
        {
            out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    " << fld << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) {\n"
                << "                        float v;\n"
                << "                        if (parser.ReadFloat(v)) " << fld << ".push_back(v);\n"
                << "                    }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
        else
        {
            out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    " << fld << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) {\n"
                << "                        int32_t v;\n"
                << "                        if (parser.ReadInt(v)) " << fld << ".push_back(static_cast<decltype(" << fld
                << ")::value_type>(v));\n"
                << "                    }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
    }
    else if constexpr (is_vec3(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t arrSize;\n"
            << "                if (parser.ReadArraySize(arrSize) && arrSize >= 3) {\n"
            << "                    float x, y, z;\n"
            << "                    if (parser.ReadFloat(x) && parser.ReadFloat(y) && parser.ReadFloat(z)) {\n"
            << "                        " << fld << " = ::Deki::Vector3(x, y, z);\n"
            << "                    }\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_vec2(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t arrSize;\n"
            << "                if (parser.ReadArraySize(arrSize) && arrSize >= 2) {\n"
            << "                    float x, y;\n"
            << "                    if (parser.ReadFloat(x) && parser.ReadFloat(y)) {\n"
            << "                        " << fld << " = ::Deki::Vector2(x, y);\n"
            << "                    }\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_color(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) " << fld
            << " = Deki::Color::FromRGBA8888(static_cast<uint32_t>(v)); break; }\n";
    }
    else if constexpr (is_enum(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) " << fld
            << " = static_cast<decltype(" << fld << ")>(v); break; }\n";
    }
    else if constexpr (is_float(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { float v; if (parser.ReadFloat(v)) " << fld
            << " = v; break; }\n";
    }
    else if constexpr (is_double(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { float v; if (parser.ReadFloat(v)) " << fld
            << " = static_cast<double>(v); break; }\n";
    }
    else if constexpr (is_bool(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { bool v; if (parser.ReadBool(v)) " << fld
            << " = v; break; }\n";
    }
    else if constexpr (is_string(t))
    {
        out << "            case ::Deki::HashString(\"" << nm
            << "\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) " << fld
            << " = std::string(s, len); break; }\n";
    }
    else if constexpr (is_int_like(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) " << fld
            << " = static_cast<decltype(" << fld << ")>(v); break; }\n";
    }
}

template <m::info COMP>
void emit_component(std::ostream& out, std::string_view srcdir, bool pluginBuild)
{
    constexpr const char* CLS = std::define_static_string(m::identifier_of(COMP));  // Health
    constexpr const char* QN = qualified_name(COMP);                                // Game::Health
    constexpr m::info BASE = component_base(COMP);
    constexpr const char* BQN = qualified_name(BASE);
    const std::string G = std::string("DekiGen_") + symbol_stem(COMP);  // symbol prefix

    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Source: " << rel_src(m::source_location_of(COMP).file_name(), srcdir) << "\n\n";
    out << "#include \"" << src_rel_include(m::source_location_of(COMP).file_name(), srcdir) << "\"\n";
    out << "#include <deki/SceneMessagePack.h>\n";
    out << "#include <deki/Scene.h>\n";
    out << "#include <deki/reflection/Property.h>\n";
    out << "#include <unordered_map>\n\n";

    // What a component must look like, reported where the author will read it.
    if constexpr (!scope_is_reflectable(COMP))
    {
        out << "#error \"" << QN
            << ": a component must be declared in a named namespace (or at global scope), not nested in a class or an "
               "anonymous namespace\"\n";
    }
    if constexpr (!component_base_is_public(COMP))
    {
        out << "#error \"" << QN << ": a component must derive PUBLICLY from Deki::Component (write `class " << CLS
            << " : public ...`)\"\n";
    }
    template for (constexpr auto mem : std::define_static_array(deser_members(COMP)))
    {
        if constexpr (!m::is_public(mem))
        {
            out << "#error \"" << QN << "::" << m::identifier_of(mem)
                << ": a DEKI_EXPORT field must be public (the generated code reaches it from outside the class)\"\n";
        }
    }

    out << "extern const ::Deki::ComponentTypeInfo " << G << "_TypeInfo;\n\n";

    out << SEP << "\n// Runtime Deserialization (all platforms)\n" << SEP << "\n\n";
    out << "static bool " << G
        << "_Deserialize(::Deki::Component* _c, ::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize) {\n";
    out << "    " << QN << "& self = *static_cast<" << QN << "*>(_c); (void)self;\n";
    out << "    for (uint32_t i = 0; i < mapSize; i++) {\n";
    out << "        const char* key; uint16_t keyLen;\n";
    out << "        if (!parser.ReadString(key, keyLen)) return false;\n";
    if constexpr (deser_members(COMP).empty())
    {
        out << "        if (!parser.SkipValue()) return false;\n";
    }
    else
    {
        out << "\n        uint32_t keyHash = ::Deki::HashStringLen(key, keyLen);\n";
        out << "        switch (keyHash) {\n";
        template for (constexpr auto mem : std::define_static_array(
                          deser_members(COMP))) if constexpr (!is_editor_only(mem)) emit_case<mem>(out);
        if constexpr (any_editor_only(COMP))
        {
            out << "#ifdef DEKI_EDITOR\n";
            template for (constexpr auto mem : std::define_static_array(
                              deser_members(COMP))) if constexpr (is_editor_only(mem)) emit_case<mem>(out);
            out << "#else\n";
            template for (constexpr auto mem :
                          std::define_static_array(deser_members(COMP))) if constexpr (is_editor_only(mem))
            {
                if constexpr (renamed_from_of(mem) != nullptr)
                {
                    out << "            case ::Deki::HashString(\"" << renamed_from_of(mem)
                        << "\"):  // DEKI_RENAMED_FROM\n";
                }
                out << "            case ::Deki::HashString(\"" << m::identifier_of(mem)
                    << "\"): parser.SkipValue(); break;\n";
            }
            out << "#endif\n";
        }
        out << "            default: parser.SkipValue(); break;\n";
        out << "        }\n";
    }
    out << "    }\n    return true;\n}\n\n";

    // Own and inherited reference fields, flattened: the class's entry answers
    // for the whole object, there is no chain to a base's function.
    out << SEP << "\n// ObjectRef/AssetRef Resolution (all platforms) - Zero Overhead\n" << SEP << "\n\n";
    out << "static void " << G << "_ResolveObjectRefs(::Deki::Component* _c, ::Deki::Scene* scene) {\n";
    out << "    " << QN << "& self = *static_cast<" << QN << "*>(_c); (void)self; (void)scene;\n";
    template for (constexpr auto mem : std::define_static_array(deser_members(COMP)))
    {
        constexpr m::info t = m::type_of(mem);
        constexpr const char* nm = std::define_static_string(m::identifier_of(mem));
        if constexpr (is_objectref(t))
        {
            out << "    if (self." << nm << ".id != 0 && self." << nm << ".resolved == nullptr)\n"
                << "        self." << nm << ".resolved = scene->FindDekiObject(self." << nm << ".id);\n";
        }
        else if constexpr (is_vector(t) && is_objectref(vec_elem(t)))
        {
            out << "    for (auto& ref : self." << nm << ") {\n"
                << "        if (ref.id != 0 && ref.resolved == nullptr)\n"
                << "            ref.resolved = scene->FindDekiObject(ref.id);\n"
                << "    }\n";
        }
    }
    template for (constexpr auto mem : std::define_static_array(deser_members(COMP)))
    {
        constexpr m::info t = m::type_of(mem);
        if constexpr (is_assetref(t))
        {
            constexpr const char* nm = std::define_static_string(m::identifier_of(mem));
            constexpr const char* at = asset_arg(t);
            if constexpr (std::string_view(at) == "Scene")
            {
                out << "    // AssetRef<Scene> \"" << nm
                    << "\" - deferred (non-cacheable, loaded on-demand via Get())\n";
            }
            else
            {
                out << "    if (auto* _loaded_" << nm << " = self." << nm << ".Get()) {\n"
                    << "        const char* _frameGuid = self." << nm << ".source.empty() ? self." << nm
                    << ".guid.c_str() : self." << nm << ".source.c_str();\n"
                    << "        self.OnAssetRefResolved(\"" << nm << "\", _loaded_" << nm << ", _frameGuid);\n"
                    << "    }\n";
            }
        }
    }
    out << "}\n\n";

    out << SEP << "\n// ::Deki::ObjectRef ID Remapping (all platforms)\n" << SEP << "\n\n";
    out << "static void " << G
        << "_RemapObjectRefs(::Deki::Component* _c, const std::unordered_map<uint32_t, uint32_t>& idRemap) {\n";
    out << "    " << QN << "& self = *static_cast<" << QN << "*>(_c); (void)self; (void)idRemap;\n";
    template for (constexpr auto mem : std::define_static_array(deser_members(COMP)))
    {
        constexpr m::info t = m::type_of(mem);
        constexpr const char* nm = std::define_static_string(m::identifier_of(mem));
        if constexpr (is_objectref(t))
        {
            out << "    { auto it = idRemap.find(self." << nm << ".id);\n"
                << "      if (it != idRemap.end()) { self." << nm << ".id = it->second; self." << nm
                << ".resolved = nullptr; } }\n";
        }
        else if constexpr (is_vector(t) && is_objectref(vec_elem(t)))
        {
            out << "    for (auto& ref : self." << nm << ") {\n"
                << "        auto it = idRemap.find(ref.id);\n"
                << "        if (it != idRemap.end()) { ref.id = it->second; ref.resolved = nullptr; }\n"
                << "    }\n";
        }
    }
    out << "}\n\n";

    // ---- Compact field table (ALL platforms, embedded included) ----
    // Flattened: own + inherited DEKI_EXPORT fields, each offset computed in THIS
    // class so no base-subobject layout is assumed. 8 bytes per entry, the only
    // reflection a device build gets. See ::Deki::FieldRef / ComponentRegistry.
    out << SEP << "\n// Compact Field Table (all platforms)\n" << SEP << "\n\n";
    int fieldCount = 0;
    if constexpr (!deser_members(COMP).empty())
    {
        // FieldRef::offset is a uint16_t, to keep the device's field table at 8
        // bytes per entry in flash. The casts below are C-style and so silent:
        // a component over 65535 bytes would have its later fields truncated to
        // an offset pointing somewhere arbitrary inside the object, and every
        // data-driven write through FindField (an FSM action, a tween target)
        // would corrupt whatever sits there instead of failing. Nothing shipped
        // is anywhere near the limit, but a user with a big inline buffer would
        // meet it with no diagnostic at all. Turn it into a compile error that
        // names the component.
        out << "static_assert(sizeof(" << QN << ") <= 0xFFFF,\n"
            << "    \"" << QN << " is larger than 65535 bytes. Deki's field table stores a \"\n"
            << "    \"16-bit field offset, so this component's fields cannot be addressed. \"\n"
            << "    \"Hold the bulk of the data behind a pointer or a Deki::Buffer instead of \"\n"
            << "    \"inline in the component.\");\n";
        out << "static const ::Deki::FieldRef " << G << "_Fields[] = {\n";
        template for (constexpr auto mem : std::define_static_array(deser_members(COMP)))
        {
            constexpr m::info t = m::type_of(mem);
            ++fieldCount;
            out << "    { ::Deki::HashString(\"" << m::identifier_of(mem) << "\"), "
                << "(uint16_t)offsetof(" << QN << ", " << m::identifier_of(mem) << "), "
                << "(uint8_t)::Deki::PropertyType::" << deki_type_name(t) << ", ";
            if constexpr (is_enum(t))
            {
                out << sizeof(typename[:bare(t):]);
            }
            else
            {
                out << "0";
            }
            out << " },\n";
            // DEKI_RENAMED_FROM: the old key resolves to the same field on device
            // (FindField by name for ::Deki::PropertyRef, FSM and tween targets).
            if constexpr (renamed_from_of(mem) != nullptr)
            {
                ++fieldCount;
                out << "    { ::Deki::HashString(\"" << renamed_from_of(mem) << "\"), "
                    << "(uint16_t)offsetof(" << QN << ", " << m::identifier_of(mem) << "), "
                    << "(uint8_t)::Deki::PropertyType::" << deki_type_name(t) << ", ";
                if constexpr (is_enum(t))
                {
                    out << sizeof(typename[:bare(t):]);
                }
                else
                {
                    out << "0";
                }
                out << " },  // DEKI_RENAMED_FROM\n";
            }
        }
        out << "};\n\n";
    }

    out << SEP << "\n// Editor Reflection (editor-only)\n" << SEP << "\n\n";
    out << "#ifdef DEKI_EDITOR\n\n";

    bool anyEnum = false;
    std::vector<std::string> emittedEnums;  // dedupe: two members of the same enum type share one table
    template for (constexpr auto mem : std::define_static_array(own_exported(COMP)))
    {
        if constexpr (is_enum(m::type_of(mem)))
        {
            const std::string en = enum_name(m::type_of(mem));
            if (std::find(emittedEnums.begin(), emittedEnums.end(), en) == emittedEnums.end())
            {
                emittedEnums.push_back(en);
                anyEnum = true;
                // `emittedEnums` only dedupes within ONE type's body. Two components sharing
                // an enum each emit their own table, which was invisible while every body was
                // its own translation unit and is a redefinition now that a package's bodies are
                // concatenated. Guard rather than dedupe globally: each body stays independent
                // of what was emitted before it, so order and grouping never matter.
                out << "#ifndef DEKI_GEN_ENUM_" << en << "\n";
                out << "#define DEKI_GEN_ENUM_" << en << "\n";
                out << "static const char* const s_" << en << "_Values[] = { ";
                bool f = true;
                template for (constexpr auto e : std::define_static_array(m::enumerators_of(bare(m::type_of(mem)))))
                {
                    if constexpr (!is_sentinel(m::identifier_of(e)))
                    {
                        if (!f)
                        {
                            out << ", ";
                        }
                        f = false;
                        out << "\"" << m::identifier_of(e) << "\"";
                    }
                }
                out << " };\n";
                // The underlying value of each enumerator, parallel to _Values.
                // Emitted because a position in that table is not the number
                // stored in the field: an enum may start off zero, leave gaps,
                // or be a flags set. Serialisation writes the name and resolves
                // it back through here, so reordering an enum cannot silently
                // change what an already-saved scene means.
                out << "static const int32_t s_" << en << "_Ints[] = { ";
                f = true;
                template for (constexpr auto e : std::define_static_array(m::enumerators_of(bare(m::type_of(mem)))))
                {
                    if constexpr (!is_sentinel(m::identifier_of(e)))
                    {
                        if (!f)
                        {
                            out << ", ";
                        }
                        f = false;
                        constexpr int32_t ev = static_cast<int32_t>([:e:]);
                        out << ev;
                    }
                }
                out << " };\n";
                out << "#endif\n";
            }
        }
    }
    if (anyEnum)
    {
        out << "\n";
    }

    int count = 0;
    if constexpr (!own_exported(COMP).empty())
    {
        out << "static const ::Deki::PropertyInfo " << G << "_Props[] = {\n";
        bool first = true;
        std::string curGroup;  // DEKI_GROUP is sticky: applies to subsequent fields until changed
        template for (constexpr auto mem : std::define_static_array(own_exported(COMP)))
        {
            constexpr m::info t = m::type_of(mem);
            if (!first)
            {
                out << ",";
            }
            first = false;
            ++count;
            out << "    { \"" << m::identifier_of(mem) << "\", nullptr, ::Deki::PropertyType::" << deki_type_name(t)
                << ", offsetof(" << QN << ", " << m::identifier_of(mem) << "),\n";
            out << "      ";
            constexpr const char* tip = tooltip_of(mem);
            if constexpr (tip != nullptr)
            {
                out << "\"" << tip << "\", ";
            }
            else
            {
                out << "nullptr, ";
            }
            if constexpr (has_range(mem))
            {
                constexpr const char* rlo = range_lo(mem);
                constexpr const char* rhi = range_hi(mem);
                if (is_simple_num(rlo) && is_simple_num(rhi))
                {
                    out << py_float(rlo) << ", " << py_float(rhi) << ", true, ";
                }
                else
                {
                    out << "0, 0, false, ";
                }
            }
            else
            {
                out << "0, 0, false, ";
            }
            if constexpr (is_enum(t))
            {
                constexpr size_t esz = sizeof(typename[:bare(t):]);
                out << "nullptr, s_" << enum_name(t) << "_Values, " << enum_count(t) << ", " << esz
                    << ", ::Deki::PropertyType::Int32, ";
            }
            else
            {
                constexpr const char* a = asset_arg(t);
                if constexpr (a != nullptr)
                {
                    out << "\"" << a << "\", ";
                }
                else
                {
                    out << "nullptr, ";
                }
                out << "nullptr, 0, 0, ::Deki::PropertyType::";
                if constexpr (is_vector(t))
                {
                    out << elem_type_name(vec_elem(t));
                }
                else
                {
                    out << "Int32";
                }
                out << ", ";
            }
            constexpr const char* vp = vw_prop(mem);
            if constexpr (vp != nullptr)
            {
                constexpr auto vis = resolve_vis(member_type_by_name(COMP, vp), vw_vals(mem));
                out << "\"" << vp << "\", " << vis.min << ", " << vis.mask << ", ";
            }
            else
            {
                out << "nullptr, 0, 0, ";
            }
            // componentRefType: the ::Deki::ObjectRef<T> argument, or DEKI_OBJECT_NAME's filter
            // on a String field (the editor's object-name picker; "" = any object).
            constexpr const char* cr = component_ref(t);
            constexpr const char* on = objname_of(mem);
            if constexpr (cr != nullptr)
            {
                out << "\"" << cr << "\", ";
            }
            else if constexpr (on != nullptr)
            {
                out << "\"" << on << "\", ";
            }
            else
            {
                out << "nullptr, ";
            }
            out << "false, false, nullptr, ";                       // isMultiline, triggersAssetRegen, assetRegenType
            out << (range_slider(mem) ? "true" : "false") << ", ";  // useSlider
            constexpr const char* grp = group_of(mem);
            if constexpr (grp != nullptr)
            {
                curGroup = grp;  // sticky
            }
            if (!curGroup.empty())
            {
                out << "\"" << curGroup << "\", ";
            }
            else
            {
                out << "nullptr, ";
            }
            constexpr const char* tog = toggle_of(mem);
            if constexpr (tog != nullptr)
            {
                out << "\"" << tog << "\", ";
            }
            else
            {
                out << "nullptr, ";
            }
            out << "::Deki::PhysicalUnit::";
            constexpr const char* u = unit_of(mem);
            if constexpr (u != nullptr)
            {
                out << u;
            }
            else
            {
                out << "None";
            }
            // The trailing optionals, in struct order: valueOfProperty,
            // renamedFrom, enumInts. They are positional, so emitting a later
            // one means spelling out every earlier one even where it is absent.
            constexpr const char* vo = valueof_of(mem);        // DEKI_VALUE_OF
            constexpr const char* rfm = renamed_from_of(mem);  // DEKI_RENAMED_FROM
            constexpr bool wantsEnumInts = is_enum(t);
            if constexpr (vo != nullptr)
            {
                out << ", \"" << vo << "\"";
            }
            else if constexpr (rfm != nullptr || wantsEnumInts)
            {
                out << ", nullptr";
            }
            if constexpr (rfm != nullptr)
            {
                out << ", \"" << rfm << "\"";
            }
            else if constexpr (wantsEnumInts)
            {
                out << ", nullptr";
            }
            if constexpr (wantsEnumInts)
            {
                out << ", s_" << enum_name(t) << "_Ints";
            }
            out << " }";
        }
        out << "\n};\n\n";
    }
    out << "extern const ::Deki::ComponentMeta " << G << "_Meta;\n";
    out << "#endif // DEKI_EDITOR\n\n";

    // ---- The class entry. Constant-initialised (every initializer is a constant
    // expression), so it exists before any static initializer runs and nothing
    // can observe it half-built.
    constexpr bool ABSTRACT = m::is_abstract_type(COMP);
    out << SEP << "\n// Type entry (all platforms)\n" << SEP << "\n\n";
    out << "const ::Deki::ComponentTypeInfo " << G << "_TypeInfo = {\n";
    out << "    ::Deki::TypeName<" << QN << ">(),\n";
    out << "    ::Deki::TypeShortName<" << QN << ">(),\n";
    out << "    ::Deki::TypeId<" << QN << ">(),\n";
    out << "    ::Deki::TypeId<" << BQN << ">(),\n";
    out << "    " << (ABSTRACT ? "true" : "false") << ",   // isAbstract\n";
    out << "    " << (declares_update_rec(COMP) ? "true" : "false") << ",   // declaresUpdate\n";
    if constexpr (ABSTRACT)
    {
        out << "    nullptr,\n";
    }
    else
    {
        out << "    []() -> ::Deki::Component* { auto* c = new " << QN << "(); c->SetTypeInfo(&" << G
            << "_TypeInfo); return c; },\n";
    }
    out << "    &" << G << "_Deserialize,\n";
    out << "    &" << G << "_ResolveObjectRefs,\n";
    out << "    &" << G << "_RemapObjectRefs,\n";
    if (fieldCount > 0)
    {
        out << "    " << G << "_Fields, " << fieldCount << ",\n";
    }
    else
    {
        out << "    nullptr, 0,\n";
    }
    out << "#ifdef DEKI_EDITOR\n    &" << G << "_Meta\n#else\n    nullptr\n#endif\n";
    out << "};\n\n";

    out << "#ifdef DEKI_EDITOR\n";
    // DEKI_FORMER_NAME table, emitted before the meta that points at it.
    constexpr auto kFormerNames = std::define_static_array(tag_texts_of(COMP, TAG_FORMER_NAME));
    if constexpr (kFormerNames.size() > 0)
    {
        out << "static const char* const " << G << "_FormerNames[] = { ";
        bool firstName = true;
        for (const char* formerName : kFormerNames)
        {
            if (!firstName)
            {
                out << ", ";
            }
            firstName = false;
            out << "\"" << formerName << "\"";
        }
        out << " };\n";
    }
    out << "const ::Deki::ComponentMeta " << G << "_Meta = {\n";
    out << "    \"" << CLS << "\",\n";
    out << "    ::Deki::TypeName<" << QN << ">(),  // serializedName: the scene file's \"type\"\n";
    constexpr const char* category = tag_text_of(COMP, TAG_CATEGORY);
    if constexpr (category != nullptr)
    {
        out << "    \"" << category << "\",  // DEKI_CATEGORY\n";
    }
    else
    {
        out << "    \"" << (pluginBuild ? "Game" : "General") << "\",  // category (default)\n";
    }
    constexpr const char* description = tag_text_of(COMP, TAG_DESCRIPTION);
    if constexpr (description != nullptr)
    {
        out << "    \"" << description << "\",  // DEKI_DESCRIPTION\n";
    }
    else
    {
        out << "    \"\",      // description (none declared)\n";
    }
    out << "    ::Deki::TypeId<" << QN << ">(),\n";
    out << "    ::Deki::TypeId<" << BQN << ">(),\n";
    out << "    " << (ABSTRACT ? "true" : "false") << ",   // isAbstract\n";
    out << "    false,   // isUnique\n";
    out << "    " << (std::string(CLS) == "PlatformSetupComponent" ? "true" : "false") << ",   // bootSceneOnly\n";
    out << "    false,   // platformSceneOnly\n";
    out << "    sizeof(" << QN << "),\n";
    if constexpr (ABSTRACT)
    {
        out << "    nullptr,\n";
    }
    else
    {
        out << "    " << G << "_TypeInfo.create,\n";
    }
    if (count > 0)
    {
        out << "    " << G << "_Props, " << count << ",\n";
    }
    else
    {
        out << "    nullptr, 0,\n";
    }
    out << "    {}, false,\n";
    constexpr const char* displayName = tag_text_of(COMP, TAG_DISPLAY_NAME);
    if constexpr (displayName != nullptr)
    {
        out << "    \"" << displayName << "\",   // displayNameOverride (DEKI_DISPLAY_NAME)\n";
    }
    else
    {
        out << "    nullptr,   // displayNameOverride\n";
    }
    constexpr const char* section = tag_text_of(COMP, TAG_SETTINGS_SECTION);
    if constexpr (section != nullptr)
    {
        out << "    \"" << section << "\",   // settingsSection (DEKI_SETTINGS_SECTION)\n";
    }
    else
    {
        out << "    nullptr,   // settingsSection\n";
    }
    out << "    &" << G << "_TypeInfo,\n";
    // Newest first: the qualified names a saved scene may still carry for this
    // class, so the loader can resolve a type it no longer recognises.
    if constexpr (kFormerNames.size() > 0)
    {
        out << "    " << G << "_FormerNames, " << kFormerNames.size() << "   // DEKI_FORMER_NAME\n";
    }
    else
    {
        out << "    nullptr, 0   // formerNames (none declared)\n";
    }
    out << "};\n";
    out << "#endif // DEKI_EDITOR\n\n";
}

// ---- DEKI_SERIALIZABLE struct: one device (MessagePack) deserialize case ----
// Mirrors emit_case but (a) targets a free `obj.<field>` instead of a member of
// `this`, and (b) additionally handles nested DEKI_SERIALIZABLE struct fields and
// vectors of them. Data assets have those; components never do. Serializable
// structs serialize ALL non-static members (ser_members), not just DEKI_EXPORT.
template <m::info MEM>
void emit_msgpack_case(std::ostream& out)
{
    constexpr m::info t = m::type_of(MEM);
    constexpr const char* nm = std::define_static_string(m::identifier_of(MEM));
    constexpr const char* rf = renamed_from_of(MEM);
    if constexpr (rf != nullptr)
    {
        if constexpr (std::string_view(rf) == std::string_view(nm))
        {
            out << "#error DEKI_RENAMED_FROM on " << nm << " names its current identifier\n";
        }
        // Stacked label: the old key falls into the same case body as the new one.
        out << "            case ::Deki::HashString(\"" << rf << "\"):  // DEKI_RENAMED_FROM\n";
    }
    if constexpr (is_vector(t))
    {
        constexpr m::info e = vec_elem(t);
        if constexpr (is_string(e))
        {
            out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    obj." << nm << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) {\n"
                << "                        const char* s; uint16_t len;\n"
                << "                        if (parser.ReadString(s, len)) obj." << nm
                << ".push_back(std::string(s, len));\n"
                << "                    }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
        else if constexpr (is_float(e))
        {
            out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    obj." << nm << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) { float v; if (parser.ReadFloat(v)) obj."
                << nm << ".push_back(v); }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
        else if constexpr (m::is_class_type(bare(e)) && is_serializable(bare(e)))
        {
            out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    obj." << nm << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) {\n"
                << "                        decltype(obj." << nm << ")::value_type _elem{};\n"
                << "                        uint32_t _ms;\n"
                << "                        if (parser.ReadMapSize(_ms)) DeserializeMsgPack(_elem, parser, _ms);\n"
                << "                        obj." << nm << ".push_back(_elem);\n"
                << "                    }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
        else
        {
            out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    obj." << nm << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) { int32_t v; if (parser.ReadInt(v)) obj."
                << nm << ".push_back(static_cast<decltype(obj." << nm << ")::value_type>(v)); }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
    }
    else if constexpr (m::is_class_type(bare(t)) && is_serializable(bare(t)))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t _ms;\n"
            << "                if (parser.ReadMapSize(_ms)) DeserializeMsgPack(obj." << nm << ", parser, _ms);\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_vec3(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t arrSize;\n"
            << "                if (parser.ReadArraySize(arrSize) && arrSize >= 3) { float x, y, z; if "
               "(parser.ReadFloat(x) && parser.ReadFloat(y) && parser.ReadFloat(z)) obj."
            << nm << " = ::Deki::Vector3(x, y, z); }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_vec2(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t arrSize;\n"
            << "                if (parser.ReadArraySize(arrSize) && arrSize >= 2) { float x, y; if "
               "(parser.ReadFloat(x) && parser.ReadFloat(y)) obj."
            << nm << " = ::Deki::Vector2(x, y); }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_color(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) obj." << nm
            << " = Deki::Color::FromRGBA8888(static_cast<uint32_t>(v)); break; }\n";
    }
    else if constexpr (is_enum(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) obj." << nm
            << " = static_cast<decltype(obj." << nm << ")>(v); break; }\n";
    }
    else if constexpr (is_float(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { float v; if (parser.ReadFloat(v)) obj." << nm
            << " = v; break; }\n";
    }
    else if constexpr (is_double(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { float v; if (parser.ReadFloat(v)) obj." << nm
            << " = static_cast<double>(v); break; }\n";
    }
    else if constexpr (is_bool(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { bool v; if (parser.ReadBool(v)) obj." << nm
            << " = v; break; }\n";
    }
    else if constexpr (is_string(t))
    {
        out << "            case ::Deki::HashString(\"" << nm
            << "\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) obj." << nm
            << " = std::string(s, len); break; }\n";
    }
    else if constexpr (is_assetref(t))
    {
        // Same {guid, source} shape components use, so a node can hold an asset.
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t _ms;\n"
            << "                if (parser.ReadMapSize(_ms)) {\n"
            << "                    for (uint32_t i = 0; i < _ms; i++) {\n"
            << "                        const char* k; uint16_t kLen;\n"
            << "                        if (!parser.ReadString(k, kLen)) break;\n"
            << "                        switch (::Deki::HashStringLen(k, kLen)) {\n"
            << "                            case ::Deki::HashString(\"guid\"): { const char* s; uint16_t len; if "
               "(parser.ReadString(s, len)) obj."
            << nm << ".guid = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"source\"): { const char* s; uint16_t len; if "
               "(parser.ReadString(s, len)) obj."
            << nm << ".source = std::string(s, len); break; }\n"
            << "                            default: parser.SkipValue(); break;\n"
            << "                        }\n"
            << "                    }\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_noderef(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) obj." << nm
            << ".targetId = static_cast<uint32_t>(v); break; }\n";
    }
    else if constexpr (is_propertyref(t))
    {
        // {object, component, field} as authored; the ids are derived once by Rehash.
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t _ms;\n"
            << "                if (parser.ReadMapSize(_ms)) {\n"
            << "                    for (uint32_t i = 0; i < _ms; i++) {\n"
            << "                        const char* k; uint16_t kLen;\n"
            << "                        if (!parser.ReadString(k, kLen)) break;\n"
            << "                        switch (::Deki::HashStringLen(k, kLen)) {\n"
            << "                            case ::Deki::HashString(\"object\"): { const char* s; uint16_t len; if "
               "(parser.ReadString(s, len)) obj."
            << nm << ".object = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"component\"): { const char* s; uint16_t len; if "
               "(parser.ReadString(s, len)) obj."
            << nm << ".component = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"field\"): { const char* s; uint16_t len; if "
               "(parser.ReadString(s, len)) obj."
            << nm << ".field = std::string(s, len); break; }\n"
            << "                            default: parser.SkipValue(); break;\n"
            << "                        }\n"
            << "                    }\n"
            << "                    obj." << nm << ".Rehash();\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_int_like(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) obj." << nm
            << " = static_cast<decltype(obj." << nm << ")>(v); break; }\n";
    }
}

// ---- DEKI_SERIALIZABLE struct: Serialize/Deserialize free functions ----
template <m::info S>
void emit_serializable(std::ostream& out, std::string_view srcdir)
{
    // Qualified: this body is emitted at global scope, unlike the .gen.h below
    // which is included next to the type and so names it bare. A package that
    // keeps its types in a namespace needs the full name here or the
    // specialisation names something that does not exist.
    const char* NM = qualified_name(S);
    const char* BARE = std::define_static_string(m::identifier_of(S));
    std::string_view hdr = path_basename(m::source_location_of(S).file_name());
    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Source: " << rel_src(m::source_location_of(S).file_name(), srcdir) << "\n";
    out << "// DEKI_SERIALIZABLE struct: " << NM << "\n\n";
    out << "#include \"" << hdr << "\"\n\n";
    out << "#ifdef DEKI_EDITOR\n\n";
    out << "#include <nlohmann/json.hpp>\n";
    out << "#include <deki/reflection/Serialization.h>\n\n";
    out << "// Serialize free function\n";
    out << "template<>\n";
    out << "nlohmann::json Deki::Serialize<" << NM << ">(const " << NM << "& obj) {\n";
    out << "    nlohmann::json j;\n";
    template for (constexpr auto mem : std::define_static_array(ser_members(S)))
    {
        constexpr m::info t = m::type_of(mem);
        constexpr const char* fn = std::define_static_string(m::identifier_of(mem));
        if constexpr (is_vector(t) && m::is_class_type(bare(vec_elem(t))) && !is_string(vec_elem(t)))
        {
            out << "    {\n"
                << "        nlohmann::json arr = nlohmann::json::array();\n"
                << "        for (const auto& elem : obj." << fn << ") arr.push_back(Serialize(elem));\n"
                << "        j[\"" << fn << "\"] = arr;\n"
                << "    }\n";
        }
        else
        {
            out << "    j[\"" << fn << "\"] = obj." << fn << ";\n";
        }
    }
    out << "    return j;\n}\n\n";
    out << "// Deserialize free function\n";
    out << "template<>\n";
    out << NM << " Deki::Deserialize<" << NM << ">(const nlohmann::json& j) {\n";
    out << "    " << NM << " result{};\n";
    template for (constexpr auto mem : std::define_static_array(ser_members(S)))
    {
        constexpr m::info t = m::type_of(mem);
        constexpr const char* fn = std::define_static_string(m::identifier_of(mem));
        if constexpr (is_vector(t) && m::is_class_type(bare(vec_elem(t))) && !is_string(vec_elem(t)))
        {
            // Qualified: this body is emitted at global scope, so a nested
            // serializable struct in a package namespace needs its full name.
            constexpr const char* en = qualified_name(bare(vec_elem(t)));
            out << "    if (j.contains(\"" << fn << "\") && j[\"" << fn << "\"].is_array()) {\n"
                << "        result." << fn << ".clear();\n"
                << "        for (const auto& elem : j[\"" << fn << "\"]) {\n"
                << "            result." << fn << ".push_back(::Deki::Deserialize<" << en << ">(elem));\n"
                << "        }\n"
                << "    }\n";
        }
        else
        {
            out << "    result." << fn << " = j.value(\"" << fn << "\", " << json_default(t) << ");\n";
        }
    }
    out << "    return result;\n}\n\n";
    out << "#endif // DEKI_EDITOR\n\n";

    // Runtime MessagePack deserialization (all platforms) - mirrors the component
    // Deserialize path so data assets load on desktop AND embedded with no
    // hand-written mpack. Full field-name keys (hashed), same as components.
    out << SEP << "\n// Runtime MessagePack Deserialization (all platforms)\n" << SEP << "\n\n";
    out << "#include <deki/SceneMessagePack.h>\n";
    out << "#include <deki/Scene.h>\n\n";
    // Reopened so this definition lands in the same namespace as the
    // declaration the generated header put next to the type. Defining it at
    // global scope instead leaves two overloads and ADL cannot choose.
    constexpr const char* NS = enclosing_namespace(S);
    const bool namespaced = NS[0] != 0;
    if (namespaced)
    {
        out << "namespace " << NS << " {\n";
    }
    out << "bool DeserializeMsgPack(" << BARE
        << "& obj, ::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize) {\n";
    out << "    for (uint32_t i = 0; i < mapSize; i++) {\n";
    out << "        const char* key; uint16_t keyLen;\n";
    out << "        if (!parser.ReadString(key, keyLen)) return false;\n";
    if constexpr (ser_members(S).empty())
    {
        out << "        (void)key; (void)keyLen;\n";
        out << "        if (!parser.SkipValue()) return false;\n";
    }
    else
    {
        out << "        uint32_t keyHash = ::Deki::HashStringLen(key, keyLen);\n";
        out << "        switch (keyHash) {\n";
        template for (constexpr auto mem : std::define_static_array(ser_members(S))) emit_msgpack_case<mem>(out);
        out << "            default: parser.SkipValue(); break;\n";
        out << "        }\n";
    }
    out << "    }\n    return true;\n}\n";
    if (namespaced)
    {
        out << "}  // namespace " << NS << "\n";
    }
}
template <m::info S>
void emit_serializable_header(std::ostream& out, std::string_view srcdir)
{
    const char* NM = std::define_static_string(m::identifier_of(S));
    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Source: " << rel_src(m::source_location_of(S).file_name(), srcdir) << "\n";
    out << "// DEKI_SERIALIZABLE struct: " << NM << "\n";
    out << "#pragma once\n\n";
    out << "// Editor Serialize/Deserialize (nlohmann) are template specializations declared\n";
    out << "// in reflection/Serialization.h. The runtime MessagePack deserialize is a plain\n";
    out << "// free function declared here so parent structs and asset loaders can call it.\n";
    // Not declared here: these generated headers are included inside the
    // package namespace, so opening Deki::SceneFormat from there would make
    // Pkg::Deki::SceneFormat and hide the engine namespace. It is forward
    // declared in <deki/reflection/Property.h> instead, which every header
    // carrying DEKI_EXPORT already includes.

    out << "bool DeserializeMsgPack(" << NM
        << "& obj, ::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize);\n";
}

// ---- DEKI_NODE struct: node reflection + runtime deserialize + self-registration ----
// Node types ride the component reflection machinery (a ::Deki::PropertyInfo table so
// the editor's node canvas draws them generically) but register into the SEPARATE
// NodeFactory / NodeTypeRegistry so nothing leaks into the component/scene systems.
// Fields use DEKI_EXPORT (own_exported), like components, for BOTH the property
// table and the msgpack deserialize (so the canvas-authored JSON round-trips).
// The property-table + enum-table blocks are copied verbatim from emit_component
// (they only need CLS/offsetof/annotations, none of the component-only GUID/base
// baggage); duplicating rather than refactoring keeps the proven component path
// untouched.
// Full DekiNodeMeta definition plus the REGISTER_RUNTIME_NODE / REGISTER_NODE macros;
// DekiProperty.h only forward-declares DekiNodeMeta.
static constexpr const char* kDekiNodeHeader = "deki-nodegraph/DekiNode.h";

template <m::info NODE>
void emit_node(std::ostream& out, std::string_view srcdir)
{
    const char* CLS = std::define_static_string(m::identifier_of(NODE));

    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Source: " << rel_src(m::source_location_of(NODE).file_name(), srcdir) << "\n";
    out << "// DEKI_NODE: " << CLS << "\n\n";
    out << "#include \"" << src_rel_include(m::source_location_of(NODE).file_name(), srcdir) << "\"\n";
    out << "#include <deki/SceneMessagePack.h>\n";
    out << "#include \"" << kDekiNodeHeader << "\"\n\n";

    // Everything below names the node type bare, so it is emitted inside the
    // namespace the node lives in. Left at global scope it would declare a
    // second set of free functions that ADL cannot choose between, and the
    // node meta would name a different type from the one in the header.
    constexpr const char* NS = enclosing_namespace(NODE);
    const bool namespaced = NS[0] != 0;
    if (namespaced)
    {
        out << "namespace " << NS << " {\n\n";
    }

    // Runtime MessagePack deserialization (all platforms): a free function so the
    // NodeFactory thunk and the generic NodeGraph loader can call it.
    out << SEP << "\n// Runtime MessagePack Deserialization (all platforms)\n" << SEP << "\n\n";
    out << "bool DeserializeMsgPack(" << CLS
        << "& obj, ::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize) {\n";
    out << "    for (uint32_t i = 0; i < mapSize; i++) {\n";
    out << "        const char* key; uint16_t keyLen;\n";
    out << "        if (!parser.ReadString(key, keyLen)) return false;\n";
    if constexpr (own_exported(NODE).empty())
    {
        out << "        (void)key; (void)keyLen;\n";
        out << "        if (!parser.SkipValue()) return false;\n";
    }
    else
    {
        out << "        uint32_t keyHash = ::Deki::HashStringLen(key, keyLen);\n";
        out << "        switch (keyHash) {\n";
        template for (constexpr auto mem : std::define_static_array(own_exported(NODE))) emit_msgpack_case<mem>(out);
        out << "            default: parser.SkipValue(); break;\n";
        out << "        }\n";
    }
    out << "    }\n    return true;\n}\n\n";

    // Editor reflection: enum value tables + property table + node meta.
    out << "#ifdef DEKI_EDITOR\n\n";

    bool anyEnum = false;
    std::vector<std::string> emittedEnums;  // dedupe: two members of the same enum type share one table
    template for (constexpr auto mem : std::define_static_array(own_exported(NODE)))
    {
        if constexpr (is_enum(m::type_of(mem)))
        {
            const std::string en = enum_name(m::type_of(mem));
            if (std::find(emittedEnums.begin(), emittedEnums.end(), en) == emittedEnums.end())
            {
                emittedEnums.push_back(en);
                anyEnum = true;
                // Same guard as the component path: two node types sharing an enum (an FSM
                // action set shares several) each emit the table into the same merged TU.
                out << "#ifndef DEKI_GEN_ENUM_" << en << "\n";
                out << "#define DEKI_GEN_ENUM_" << en << "\n";
                out << "static const char* const s_" << en << "_Values[] = { ";
                bool f = true;
                template for (constexpr auto e : std::define_static_array(m::enumerators_of(bare(m::type_of(mem)))))
                {
                    if constexpr (!is_sentinel(m::identifier_of(e)))
                    {
                        if (!f)
                        {
                            out << ", ";
                        }
                        f = false;
                        out << "\"" << m::identifier_of(e) << "\"";
                    }
                }
                out << " };\n";
                // The underlying value of each enumerator, parallel to _Values.
                // Emitted because a position in that table is not the number
                // stored in the field: an enum may start off zero, leave gaps,
                // or be a flags set. Serialisation writes the name and resolves
                // it back through here, so reordering an enum cannot silently
                // change what an already-saved scene means.
                out << "static const int32_t s_" << en << "_Ints[] = { ";
                f = true;
                template for (constexpr auto e : std::define_static_array(m::enumerators_of(bare(m::type_of(mem)))))
                {
                    if constexpr (!is_sentinel(m::identifier_of(e)))
                    {
                        if (!f)
                        {
                            out << ", ";
                        }
                        f = false;
                        constexpr int32_t ev = static_cast<int32_t>([:e:]);
                        out << ev;
                    }
                }
                out << " };\n";
                out << "#endif\n";
            }
        }
    }
    if (anyEnum)
    {
        out << "\n";
    }

    out << "const ::Deki::PropertyInfo* " << CLS << "::GetProperties() {\n";
    int count = 0;
    if constexpr (own_exported(NODE).empty())
    {
        out << "    return nullptr;\n}\n\n";
    }
    else
    {
        out << "    static const ::Deki::PropertyInfo props[] = {\n";
        bool first = true;
        std::string curGroup;  // DEKI_GROUP is sticky: applies to subsequent fields until changed
        template for (constexpr auto mem : std::define_static_array(own_exported(NODE)))
        {
            constexpr m::info t = m::type_of(mem);
            if (!first)
            {
                out << ",";
            }
            first = false;
            ++count;
            out << "    { \"" << m::identifier_of(mem) << "\", nullptr, ::Deki::PropertyType::" << deki_type_name(t)
                << ", offsetof(" << CLS << ", " << m::identifier_of(mem) << "),\n";
            out << "      ";
            constexpr const char* tip = tooltip_of(mem);
            if constexpr (tip != nullptr)
            {
                out << "\"" << tip << "\", ";
            }
            else
            {
                out << "nullptr, ";
            }
            if constexpr (has_range(mem))
            {
                constexpr const char* rlo = range_lo(mem);
                constexpr const char* rhi = range_hi(mem);
                if (is_simple_num(rlo) && is_simple_num(rhi))
                {
                    out << py_float(rlo) << ", " << py_float(rhi) << ", true, ";
                }
                else
                {
                    out << "0, 0, false, ";
                }
            }
            else
            {
                out << "0, 0, false, ";
            }
            if constexpr (is_enum(t))
            {
                constexpr size_t esz = sizeof(typename[:bare(t):]);
                out << "nullptr, s_" << enum_name(t) << "_Values, " << enum_count(t) << ", " << esz
                    << ", ::Deki::PropertyType::Int32, ";
            }
            else
            {
                constexpr const char* a = asset_arg(t);
                if constexpr (a != nullptr)
                {
                    out << "\"" << a << "\", ";
                }
                else
                {
                    out << "nullptr, ";
                }
                out << "nullptr, 0, 0, ::Deki::PropertyType::";
                if constexpr (is_vector(t))
                {
                    out << elem_type_name(vec_elem(t));
                }
                else
                {
                    out << "Int32";
                }
                out << ", ";
            }
            constexpr const char* vp = vw_prop(mem);
            if constexpr (vp != nullptr)
            {
                constexpr auto vis = resolve_vis(member_type_by_name(NODE, vp), vw_vals(mem));
                out << "\"" << vp << "\", " << vis.min << ", " << vis.mask << ", ";
            }
            else
            {
                out << "nullptr, 0, 0, ";
            }
            // componentRefType: the ::Deki::ObjectRef<T> argument, or DEKI_OBJECT_NAME's filter
            // on a String field (the editor's object-name picker; "" = any object).
            constexpr const char* cr = component_ref(t);
            constexpr const char* on = objname_of(mem);
            if constexpr (cr != nullptr)
            {
                out << "\"" << cr << "\", ";
            }
            else if constexpr (on != nullptr)
            {
                out << "\"" << on << "\", ";
            }
            else
            {
                out << "nullptr, ";
            }
            out << "false, false, nullptr, ";                       // isMultiline, triggersAssetRegen, assetRegenType
            out << (range_slider(mem) ? "true" : "false") << ", ";  // useSlider
            constexpr const char* grp = group_of(mem);
            if constexpr (grp != nullptr)
            {
                curGroup = grp;  // sticky
            }
            if (!curGroup.empty())
            {
                out << "\"" << curGroup << "\", ";
            }
            else
            {
                out << "nullptr, ";
            }
            constexpr const char* tog = toggle_of(mem);
            if constexpr (tog != nullptr)
            {
                out << "\"" << tog << "\", ";
            }
            else
            {
                out << "nullptr, ";
            }
            out << "::Deki::PhysicalUnit::";
            constexpr const char* u = unit_of(mem);
            if constexpr (u != nullptr)
            {
                out << u;
            }
            else
            {
                out << "None";
            }
            // Trailing optionals are positional. This path has no
            // DEKI_RENAMED_FROM, so reaching enumInts pads that slot too.
            constexpr const char* vo = valueof_of(mem);  // DEKI_VALUE_OF
            constexpr bool wantsEnumInts = is_enum(t);
            if constexpr (vo != nullptr)
            {
                out << ", \"" << vo << "\"";
            }
            else if constexpr (wantsEnumInts)
            {
                out << ", nullptr";
            }
            if constexpr (wantsEnumInts)
            {
                out << ", nullptr, s_" << enum_name(t) << "_Ints";
            }
            out << " }";
        }
        out << "\n    };\n    return props;\n}\n\n";
    }
    out << "int " << CLS << "::GetPropertyCount() { return " << count << "; }\n\n";

    out << "const ::DekiNodeGraph::DekiNodeMeta& " << CLS << "::GetNodeMeta() {\n";
    out << "    static const ::DekiNodeGraph::DekiNodeMeta meta = {\n";
    out << "        " << CLS << "::StaticNodeName,\n";
    out << "        " << CLS << "::StaticNodeCategory,   // \"Domain/MenuGroup\"\n";
    out << "        ::Deki::HashString(" << CLS << "::StaticNodeName),   // typeId\n";
    out << "        " << CLS << "::GetProperties(),\n";
    out << "        " << CLS << "::GetPropertyCount(),\n";
    // Pins are optional (DEKI_NODE_INPUTS / DEKI_NODE_OUTPUTS / DEKI_NODE_DYNAMIC_OUTPUTS).
    if constexpr (has_static_member(NODE, "StaticInputPins"))
    {
        out << "        " << CLS << "::StaticInputPins, " << CLS << "::StaticInputPinCount,\n";
    }
    else
    {
        out << "        nullptr, 0,   // no input pins\n";
    }
    if constexpr (has_static_member(NODE, "StaticOutputPins"))
    {
        out << "        " << CLS << "::StaticOutputPins, " << CLS << "::StaticOutputPinCount,\n";
    }
    else
    {
        out << "        nullptr, 0,   // no fixed output pins\n";
    }
    if constexpr (has_static_member(NODE, "StaticDynamicOutputsProperty"))
    {
        out << "        " << CLS << "::StaticDynamicOutputsProperty,\n";
    }
    else
    {
        out << "        nullptr,   // fixed outputs\n";
    }
    out << "        []() -> void* { return new " << CLS << "(); },\n";
    out << "        [](void* p) { delete static_cast<" << CLS << "*>(p); },\n";
    out << "        sizeof(" << CLS << "),\n";
    // Trailing fields are APPEND-ONLY (see DekiNodeMeta): displayName, then
    // childCategory, then titleProperty, then permanent.
    if constexpr (has_static_member(NODE, "StaticNodeDisplayName"))
    {
        out << "        " << CLS << "::StaticNodeDisplayName,\n";
    }
    else
    {
        out << "        nullptr,   // displayName (editor derives from name)\n";
    }
    if constexpr (has_static_member(NODE, "StaticNodeChildCategory"))
    {
        out << "        " << CLS << "::StaticNodeChildCategory,\n";
    }
    else
    {
        out << "        nullptr,   // childCategory (no child stack)\n";
    }
    if constexpr (has_static_member(NODE, "StaticNodeTitleProperty"))
    {
        out << "        " << CLS << "::StaticNodeTitleProperty,\n";
    }
    else
    {
        out << "        nullptr,   // titleProperty (title = display name)\n";
    }
    if constexpr (has_static_member(NODE, "StaticNodePermanent"))
    {
        out << "        " << CLS << "::StaticNodePermanent,\n";
    }
    else
    {
        out << "        false,   // permanent (freely addable/deletable)\n";
    }
    if constexpr (has_static_member(NODE, "StaticNodeVariables"))
    {
        out << "        " << CLS << "::StaticNodeVariables,\n";
    }
    else
    {
        out << "        false,   // declaresVariables (ordinary child stack)\n";
    }
    if constexpr (has_static_member(NODE, "StaticNodeSubgraphCategory"))
    {
        out << "        " << CLS << "::StaticNodeSubgraphCategory,\n";
    }
    else
    {
        out << "        nullptr,   // subgraphCategory (no inner graph)\n";
    }
    if constexpr (has_static_member(NODE, "StaticNodeSubgraphEntry"))
    {
        out << "        " << CLS << "::StaticNodeSubgraphEntry,\n";
    }
    else
    {
        out << "        nullptr,   // subgraphEntry (no seeded entry node)\n";
    }
    if constexpr (has_static_member(NODE, "StaticNodeDescription"))
    {
        out << "        " << CLS << "::StaticNodeDescription\n";
    }
    else
    {
        out << "        nullptr   // description (add picker shows the type name instead)\n";
    }
    out << "    };\n    return meta;\n}\n\n";

    out << "#endif // DEKI_EDITOR\n\n";

    // Self-registration, inline (build-config-agnostic; no PluginExports.cpp edit).
    // REGISTER_RUNTIME_NODE -> NodeFactory (all platforms); REGISTER_NODE -> NodeTypeRegistry (editor).
    out << "REGISTER_RUNTIME_NODE(" << CLS << ");\n";
    out << "#ifdef DEKI_EDITOR\n";
    out << "REGISTER_NODE(" << CLS << ");\n";
    out << "#endif\n";
    if (namespaced)
    {
        out << "\n}  // namespace " << NS << "\n";
    }
}

// ---- DEKI_NODE .gen.h stub ----
template <m::info NODE>
void emit_node_header(std::ostream& out, std::string_view srcdir)
{
    const char* CLS = std::define_static_string(m::identifier_of(NODE));
    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Source: " << rel_src(m::source_location_of(NODE).file_name(), srcdir) << "\n";
    out << "// DEKI_NODE: " << CLS << "\n";
    out << "#pragma once\n\n";
    out << "// GetProperties()/GetPropertyCount()/GetNodeMeta() are defined (editor-only) in\n";
    out << "// this package's merged *Reflection*.gen.cpp, not here.\n";
    out << "// The runtime MessagePack deserialize is a free function declared here so the\n";
    out << "// node factory and the generic NodeGraph loader can call it.\n";
    // Not declared here: these generated headers are included inside the
    // package namespace, so opening Deki::SceneFormat from there would make
    // Pkg::Deki::SceneFormat and hide the engine namespace. It is forward
    // declared in <deki/reflection/Property.h> instead, which every header
    // carrying DEKI_EXPORT already includes.

    out << "bool DeserializeMsgPack(" << CLS
        << "& obj, ::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize);\n";
}

// ---- package registration: {Prefix}Registration.gen.cpp ----
// comps = every component in the package (from reflection). pkg carries the prefix
// and the adapter list the CMake step scanned out of the .cpp files. Emitted from
// shard 0 only, and comps is the package's FULL component list (every shard
// collects it; only the emitting of per-component bodies is sharded).
struct CompInfo
{
    std::string name;  // unqualified identifier
    std::string qn;    // qualified name ("Game::Health")
    std::string stem;  // generated-symbol stem ("Game_Health")
    bool abstract;
    bool settings;    // DEKI_SETTINGS_SECTION: a Project Settings section
    std::string inc;  // header path relative to the package's source dir
};
inline bool by_name(const CompInfo& a, const CompInfo& b)
{
    return a.qn < b.qn;
}

// The shared body of every registration: externs for the generated entries,
// then a function that hands them to the factory, the field tables and (editor)
// the component and settings registries. `pluginRegistries` is false for the
// project plugin: PluginLoader registers a plugin's metas itself, marked for
// clearing on hot reload, and a registration from inside the DLL would leave
// unmarked pointers behind after FreeLibrary.
inline void emit_registration_body(std::ostream& out, std::vector<CompInfo> comps, const std::string& fnName,
                                   const char* const* adapters, int adapterCount, bool editorRegistries,
                                   bool pluginSettings)
{
    std::sort(comps.begin(), comps.end(), by_name);
    out << "#include <deki/reflection/ComponentFactory.h>\n";
    out << "#include <deki/reflection/ComponentRegistry.h>\n";
    if (editorRegistries)
    {
        out << "#ifdef DEKI_EDITOR\n#include <deki/reflection/SettingsRegistry.h>\n#endif\n";
    }
    out << "\n";
    for (auto& c : comps)
    {
        out << "extern const ::Deki::ComponentTypeInfo DekiGen_" << c.stem << "_TypeInfo;\n";
    }
    if (editorRegistries)
    {
        out << "#ifdef DEKI_EDITOR\n";
        for (auto& c : comps)
        {
            out << "extern const ::Deki::ComponentMeta DekiGen_" << c.stem << "_Meta;\n";
        }
        out << "#endif\n";
    }
    if (adapterCount > 0)
    {
        out << "\n// Interface adapter registrations (explicit calls for static-lib builds)\n";
        for (int i = 0; i < adapterCount; ++i)
        {
            out << "extern void " << adapters[i] << "();\n";
        }
    }
    out << "\nvoid " << fnName << "() {\n";
    // `done` alone is not enough: a shared library the platform did not really
    // unload (dlclose is often a no-op for GCC-built objects) keeps it set
    // across an editor reload that cleared the registries, and the package's
    // components were then never registered again. Register again whenever the
    // registries no longer hold this package's first type.
    out << "    static bool done = false;\n";
    if (comps.empty())
    {
        out << "    if (done) return;\n";
    }
    else
    {
        const std::string first = "DekiGen_" + comps.front().stem;
        out << "    if (done && ::Deki::ComponentFactory::Instance().Find(" << first << "_TypeInfo.typeId)";
        if (editorRegistries)
        {
            out << "\n#ifdef DEKI_EDITOR\n        && ::Deki::ComponentRegistry::Instance().GetMeta(" << first
                << "_TypeInfo.typeId)\n#endif\n       ";
        }
        out << ")\n        return;\n";
    }
    out << "    done = true;\n";
    if (adapterCount > 0)
    {
        out << "    // Register interface adapters first (stripped by linker in static-lib builds)\n";
        for (int i = 0; i < adapterCount; ++i)
        {
            out << "    " << adapters[i] << "();\n";
        }
        out << "\n";
    }
    out << "    auto& factory = ::Deki::ComponentFactory::Instance();\n";
    for (auto& c : comps)
    {
        out << "    factory.Register(&DekiGen_" << c.stem << "_TypeInfo);\n";
    }
    out << "\n    // Compact field tables (all platforms - see ::Deki::FieldRef)\n";
    out << "    auto& registry = ::Deki::ComponentRegistry::Instance();\n";
    for (auto& c : comps)
    {
        out << "    registry.RegisterFields(DekiGen_" << c.stem << "_TypeInfo.typeId, DekiGen_" << c.stem
            << "_TypeInfo.fields, DekiGen_" << c.stem << "_TypeInfo.fieldCount);\n";
    }
    if (editorRegistries)
    {
        out << "#ifdef DEKI_EDITOR\n";
        for (auto& c : comps)
        {
            out << "    registry.Register(&DekiGen_" << c.stem << "_Meta);\n";
        }
        for (auto& c : comps)
        {
            if (c.settings)
            {
                out << "    ::Deki::SettingsRegistry::Instance()." << (pluginSettings ? "RegisterPlugin" : "Register")
                    << "(DekiGen_" << c.stem << "_Meta.settingsSection, &DekiGen_" << c.stem << "_Meta);\n";
            }
        }
        out << "#endif\n";
    }
    out << "}\n\n";
}

inline void emit_meta_accessors(std::ostream& out, std::vector<CompInfo> comps, const std::string& prefix)
{
    std::sort(comps.begin(), comps.end(), by_name);
    std::vector<const CompInfo*> concrete;
    for (auto& c : comps)
    {
        if (!c.abstract)
        {
            concrete.push_back(&c);
        }
    }
    out << "int " << prefix << "_GetAutoComponentCount() {\n    return " << concrete.size() << ";\n}\n\n";
    out << "#ifdef DEKI_EDITOR\n";
    out << "const ::Deki::ComponentMeta* " << prefix << "_GetAutoComponentMeta(int index) {\n";
    out << "    switch (index) {\n";
    for (size_t i = 0; i < concrete.size(); ++i)
    {
        out << "        case " << i << ": return &DekiGen_" << concrete[i]->stem << "_Meta;\n";
    }
    out << "        default: return nullptr;\n";
    out << "    }\n}\n#endif\n";
}

void emit_registration(std::ostream& out, std::vector<CompInfo> comps, const CodegenPackage& pkg)
{
    size_t nAbstract = 0;
    for (auto& c : comps)
    {
        if (c.abstract)
        {
            ++nAbstract;
        }
    }
    out << "// AUTO-GENERATED by the Deki reflection codegen: " << pkg.prefix << " package registration\n";
    out << "// " << (comps.size() - nAbstract) << " concrete + " << nAbstract << " abstract component(s)\n\n";
    emit_registration_body(out, comps, std::string(pkg.prefix) + "_RegisterComponents", pkg.adapters, pkg.adapterCount,
                           /*editorRegistries=*/true, /*pluginSettings=*/true);
    emit_meta_accessors(out, comps, pkg.prefix);
}

// PluginRegistration.gen.cpp (project plugin): Plugin_RegisterComponents() for the
// static firmware/simulator builds, and the DekiPlugin_* component exports the
// editor's PluginLoader reads. The editor-side registries are left to the loader.
void emit_plugin_registration(std::ostream& out, std::vector<CompInfo> comps, const CodegenPackage& pkg)
{
    out << "// AUTO-GENERATED by the Deki reflection codegen: project plugin registration\n";
    out << "// " << comps.size() << " component(s)\n\n";
    emit_registration_body(out, comps, std::string(pkg.prefix) + "_RegisterComponents", pkg.adapters, pkg.adapterCount,
                           /*editorRegistries=*/false, /*pluginSettings=*/true);
    std::sort(comps.begin(), comps.end(), by_name);
    out << "#ifdef DEKI_EDITOR\n";
    for (auto& c : comps)
    {
        out << "extern const ::Deki::ComponentMeta DekiGen_" << c.stem << "_Meta;\n";
    }
    out << "#endif\n\n";
    emit_meta_accessors(out, comps, pkg.prefix);
    out << "\n#ifdef DEKI_EDITOR\n";
    out << "#ifdef _WIN32\n#define DEKI_GEN_PLUGIN_API __declspec(dllexport)\n#else\n#define DEKI_GEN_PLUGIN_API "
           "__attribute__((visibility(\"default\")))\n#endif\n";
    out << "extern \"C\" {\n";
    out << "DEKI_GEN_PLUGIN_API int DekiPlugin_GetComponentCount() { return " << pkg.prefix
        << "_GetAutoComponentCount(); }\n";
    out << "DEKI_GEN_PLUGIN_API const ::Deki::ComponentMeta* DekiPlugin_GetComponentMeta(int index) { return "
        << pkg.prefix << "_GetAutoComponentMeta(index); }\n";
    out << "}\n#endif // DEKI_EDITOR\n";
}

// CoreRegistration.gen.cpp (engine-core build): DekiCore_RegisterComponents(), run
// from a static initializer in the editor and from deki_register_core_components()
// on the runtime, where static initializers are not relied on.
void emit_core_registration(std::ostream& out, std::vector<CompInfo> comps)
{
    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Core engine component registration (" << comps.size() << " components)\n\n";
    emit_registration_body(out, comps, "DekiCore_RegisterComponents", nullptr, 0,
                           /*editorRegistries=*/true, /*pluginSettings=*/false);
    out << "#ifdef DEKI_EDITOR\n";
    out << "namespace {\nstruct DekiCoreRegistrar {\n    DekiCoreRegistrar() { DekiCore_RegisterComponents(); }\n} "
           "s_DekiCoreRegistrar;\n}  // namespace\n";
    out << "#else\n";
    out << "// Static initializers are unreliable on ESP-IDF, so the runtime registers explicitly.\n";
    out << "extern \"C\" void deki_register_core_components(void)\n{\n    DekiCore_RegisterComponents();\n}\n";
    out << "#endif\n";
}

// AllComponentRegistration.gen.cpp: the one deki_register_all_components() the engine calls.
// In-tree there are no component packages, so it registers the core components only.
void emit_all_component_registration(std::ostream& out, std::vector<CompInfo> comps)
{
    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Single registration function for the " << comps.size() << " core components.\n";
    out << "// DekiEngine calls this ONE function - it knows nothing about individual packages.\n\n";
    out << "void DekiCore_RegisterComponents();\n\n";
    out << "extern \"C\" void deki_register_all_components(void)\n{\n    DekiCore_RegisterComponents();\n}\n";
}

int main()
{
    int nc = 0, ns = 0, nn = 0;
    // Per-package component list. Collected by EVERY shard (it is cheap: name, abstract
    // flag and include path, no property emission) because the registration file needs
    // the package's complete list and is written by shard 0.
    std::vector<std::vector<CompInfo>> comps(kPackageCount);
    auto out_path = [](int mi, const char* nm, const char* ext)
    { return std::string(kPackages[mi].outdir) + "/" + nm + ext; };

    // One MERGED .gen.cpp per package instead of one per type. The .gen.h stubs stay
    // per type because hand-written headers include them BY NAME; nothing includes a
    // .gen.cpp, so their names are ours to choose - and choosing a FIXED one is what
    // makes adding a type a one-build operation. The package source list comes from a
    // file(GLOB CONFIGURE_DEPENDS) evaluated at configure time, so a per-type .gen.cpp
    // is a brand new path that only joins the build at the NEXT configure: build one
    // generates it, build two compiles it. A file that always exists under the same
    // name changes only in CONTENT, which the normal dependency scan already handles.
    //
    // Nothing is lost by merging: the package DLLs are unity-built (UNITY_BUILD_BATCH_SIZE
    // 8), so these files were already being concatenated into a handful of translation
    // units. Each emitted body carries its own #include lines and every header is
    // #pragma once, so concatenation is safe.
    std::vector<std::string> mergedCpp(kPackageCount);
    // The `template for` variable is only usable as a template argument in the loop body
    // itself (a lambda would capture it and lose its constant-expression-ness), so each
    // emit renders into a buffer here and hands the finished text to write_if_changed.
    std::ostringstream buf;
    auto take = [&buf]
    {
        std::string s = buf.str();
        buf.str({});
        buf.clear();
        return s;
    };
#if defined(DEKI_NO_REGISTRATION)
    constexpr bool kPluginBuild = true;
#else
    constexpr bool kPluginBuild = false;
#endif
    // Components: their bodies go to the package's merged .gen.cpp. There is no
    // per-type .gen.h any more: a component header includes nothing generated.
    template for (constexpr auto comp : kComponents)
    {
        if constexpr (package_index_of(comp) != -1)
        {
            constexpr int mi = package_index_of(comp);
            static_assert(mi >= 0, "reflection codegen: source path matches two package tags in this work unit");
            comps[mi].push_back({ std::string(m::identifier_of(comp)), std::string(qualified_name(comp)),
                                  std::string(symbol_stem(comp)), m::is_abstract_type(comp),
                                  tag_text_of(comp, TAG_SETTINGS_SECTION) != nullptr,
                                  src_rel_include(m::source_location_of(comp).file_name(), kPackages[mi].srcdir) });
            if constexpr (in_shard(qualified_name(comp)))
            {
                emit_component<comp>(buf, kPackages[mi].srcdir, kPluginBuild);
                mergedCpp[mi] += take();
                ++nc;
            }
        }
    }
    // A marker above nothing: DEKI_CATEGORY("...") with no component class after
    // it in that file. Reported in the package's generated unit, where the build
    // shows it with the file and line.
    template for (constexpr auto tg : kTags)
    {
        if constexpr (package_index_of(tg) != -1 && tag_owner(tg) == m::info{})
        {
            constexpr int mi = package_index_of(tg);
            constexpr auto loc = m::source_location_of(tg);
            mergedCpp[mi] += std::string("#error \"component marker at ") + loc.file_name() + ":" +
                             std::to_string(loc.line()) +
                             " is not followed by a component class in that file (DEKI_CATEGORY / DEKI_DESCRIPTION / "
                             "DEKI_DISPLAY_NAME / DEKI_SETTINGS_SECTION go right above the class)\"\n";
        }
    }
    // DEKI_SERIALIZABLE structs: .gen.cpp (Serialize/Deserialize) and .gen.h (stub)
    template for (constexpr auto s : kScope)
    {
        if constexpr (m::is_type(s) && m::is_class_type(s) && m::is_complete_type(s) && is_serializable(s) &&
                      package_index_of(s) != -1 && in_shard(m::identifier_of(s)))
        {
            constexpr int mi = package_index_of(s);
            static_assert(mi >= 0, "reflection codegen: source path matches two package tags in this work unit");
            const char* nm = std::define_static_string(m::identifier_of(s));
            emit_serializable<s>(buf, kPackages[mi].srcdir);
            mergedCpp[mi] += take();
            emit_serializable_header<s>(buf, kPackages[mi].srcdir);
            write_if_changed(out_path(mi, nm, ".gen.h"), take());
            ++ns;
        }
    }
    // DEKI_NODE structs: .gen.cpp (reflection + msgpack deserialize + self-registration) and .gen.h (stub)
    template for (constexpr auto n : kScope)
    {
        if constexpr (m::is_type(n) && is_node(n) && package_index_of(n) != -1 && in_shard(m::identifier_of(n)))
        {
            constexpr int mi = package_index_of(n);
            static_assert(mi >= 0, "reflection codegen: source path matches two package tags in this work unit");
            const char* nm = std::define_static_string(m::identifier_of(n));
            emit_node<n>(buf, kPackages[mi].srcdir);
            mergedCpp[mi] += take();
            emit_node_header<n>(buf, kPackages[mi].srcdir);
            write_if_changed(out_path(mi, nm, ".gen.h"), take());
            ++nn;
        }
    }
    // Orphan per-type .gen.cpp files: from the old one-file-per-type scheme, or from a
    // type that was deleted or renamed. They are still globbed into the package's source
    // list, so left alone they define every symbol a second time.
    //
    // EMPTIED, never deleted. A consumer may already have globbed the path into a target
    // earlier in the same CMake configure, and removing it then fails the configure with
    // "Cannot find source file". Content may change freely; the set of files may not.
    // An empty translation unit is legal and costs nothing, and the file disappears for
    // good on the next clean checkout.
    auto is_generator_output = [](const std::string& fn)
    {
        auto ends_with = [&fn](std::string_view s)
        { return fn.size() >= s.size() && fn.compare(fn.size() - s.size(), s.size(), s) == 0; };
        if (ends_with("Registration.gen.cpp"))
        {
            return true;  // {Prefix}/Core/AllComponent
        }
        if (fn == "AllPackageInit.gen.cpp")
        {
            return true;
        }
        // {Prefix}Reflection<shard>.gen.cpp, any shard (another shard owns the others).
        const std::string tail = ".gen.cpp";
        if (!ends_with(tail))
        {
            return false;
        }
        std::string stem = fn.substr(0, fn.size() - tail.size());
        size_t digits = 0;
        while (digits < stem.size() && std::isdigit(static_cast<unsigned char>(stem[stem.size() - 1 - digits])))
        {
            ++digits;
        }
        if (digits == 0)
        {
            return false;
        }
        stem.resize(stem.size() - digits);
        return stem.size() >= 10 && stem.compare(stem.size() - 10, 10, "Reflection") == 0;
    };
    for (int i = 0; i < kPackageCount; ++i)
    {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(kPackages[i].outdir, ec))
        {
            if (ec)
            {
                break;
            }
            if (!e.is_regular_file())
            {
                continue;
            }
            const std::string fn = e.path().filename().string();
            if (fn.size() < 9 || fn.compare(fn.size() - 8, 8, ".gen.cpp") != 0)
            {
                continue;
            }
            if (is_generator_output(fn))
            {
                continue;
            }
            write_if_changed(e.path().string(),
                             "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n"
                             "// Orphaned: this type's reflection now lives in the package's merged\n"
                             "// *Reflection*.gen.cpp, or the type itself is gone. Emptied rather than\n"
                             "// deleted because a CMake glob may already reference this path.\n");
        }
    }

    // The merged per-package .gen.cpp. Written even when this shard emitted nothing for
    // the package, so the file ALWAYS exists once configured: a glob that never changes
    // shape is the whole point, and an absent file would put the shape back in play.
    // The shard index is part of the name because parallel shards of one package each
    // own their own merged file (sharding is off by default, so this is normally 0).
    for (int i = 0; i < kPackageCount; ++i)
    {
        std::ostringstream head;
        head << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n"
             << "// Every reflected type of this package, in one translation unit.\n"
             << "// Per-type stubs are the .gen.h files next to this one.\n\n";
        if (mergedCpp[i].empty())
        {
            head << "// (no reflected types in this work unit)\n";
        }

        // The field tables take offsetof() of components, which have a vtable
        // and so are not standard-layout. That is "conditionally supported"
        // C++; GCC and Clang support it (offsets are fixed without virtual
        // bases, which components never have) and only warn. ESP-IDF 6.1 turns
        // the warning into an error, which stopped every firmware build at
        // these tables. Scoped with push/pop because package sources are
        // unity-built: a bare `ignored` would switch the warning off for
        // whichever hand-written file follows in the same translation unit.
        std::string body = mergedCpp[i];
        if (!body.empty())
        {
            body = "#if defined(__GNUC__)\n#pragma GCC diagnostic push\n"
                   "#pragma GCC diagnostic ignored \"-Winvalid-offsetof\"\n#endif\n\n" +
                   body + "\n#if defined(__GNUC__)\n#pragma GCC diagnostic pop\n#endif\n";
        }

        write_if_changed(std::string(kPackages[i].outdir) + "/" + kPackages[i].prefix + "Reflection" +
                             std::to_string(DEKI_CODEGEN_SHARD_INDEX) + ".gen.cpp",
                         head.str() + body);
    }

    // Registration: CoreRegistration (engine-core) or {Prefix}Registration (package DLL).
    // Shard 0 owns it - every shard has the full component list, but only one may write.
    if (DEKI_CODEGEN_SHARD_INDEX == 0)
    {
        for (int i = 0; i < kPackageCount; ++i)
        {
            const std::string dir = kPackages[i].outdir;
#if defined(DEKI_NO_REGISTRATION)
            emit_plugin_registration(buf, comps[i], kPackages[i]);
            write_if_changed(dir + "/" + kPackages[i].prefix + "Registration.gen.cpp", take());
#elif defined(DEKI_CORE_BUILD)
            emit_core_registration(buf, comps[i]);
            write_if_changed(dir + "/CoreRegistration.gen.cpp", take());
            emit_all_component_registration(buf, comps[i]);
            write_if_changed(dir + "/AllComponentRegistration.gen.cpp", take());
            buf << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n"
                << "// Package manifest for editor auto-registration\n#pragma once\n\n"
                << "#ifdef DEKI_EDITOR\n\nextern \"C\" {\n\n}\n\n"
                << "inline void DekiPackages_RegisterAll(void* imguiCtx)\n{\n\n}\n\n#endif // DEKI_EDITOR\n";
            write_if_changed(dir + "/PackageManifest.gen.h", take());
            buf << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n"
                << "// No packages with PACKAGE_HAS_SYSTEM_INIT found.\n\n"
                << "extern \"C\" void deki_init_package_systems(void) {}\n"
                << "extern \"C\" void deki_shutdown_package_systems(void) {}\n";
            write_if_changed(dir + "/AllPackageInit.gen.cpp", take());
#else
            emit_registration(buf, comps[i], kPackages[i]);
            write_if_changed(dir + "/" + kPackages[i].prefix + "Registration.gen.cpp", take());
#endif
        }
    }
    std::cout << "generated " << nc << " components, " << ns << " serializable structs, " << nn << " nodes"
              << " (shard " << DEKI_CODEGEN_SHARD_INDEX << "/" << DEKI_CODEGEN_SHARD_COUNT << ", " << kPackageCount
              << " package(s))\n";
    return 0;
}
