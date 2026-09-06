// Self-contained reflection codegen tool: discovers every DEKI_COMPONENT in the
// included package headers and writes each one's .gen.cpp using C++26 static
// reflection. Build with: g++ -std=c++26 -freflection -DDEKI_REFLECT ...
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
#include <string_view>
#include <vector>
#include <utility>
#include <algorithm>
#include <charconv>

// kPackages / kPackageCount / the shard pair all come from the .inc. Missing means the
// CMake step and this file are out of sync — fail loudly rather than guess a default.
#if !defined(DEKI_CODEGEN_SHARD_INDEX) || !defined(DEKI_CODEGEN_SHARD_COUNT)
#error "deki_codegen_includes.inc must define DEKI_CODEGEN_SHARD_INDEX and DEKI_CODEGEN_SHARD_COUNT"
#endif

namespace m = std::meta;

// Reflected types are declared either at global scope (packages, projects) or in
// namespace Deki (the engine core, since engine 0.10); the generator looks in both.
consteval std::vector<m::info> reflected_scope_members()
{
    std::vector<m::info> all = m::members_of(^^::, m::access_context::unchecked());
    for (m::info x : m::members_of(^^Deki, m::access_context::unchecked()))
        all.push_back(x);
    return all;
}

// The namespace a reflected type is declared in ("" for the global namespace).
// Its generated definitions are emitted inside that namespace, and code that
// names the type from outside qualifies it. Component metadata and scene-file
// "class" hints keep the unqualified identifier.
consteval const char* namespace_of(m::info t)
{
    m::info p = m::parent_of(t);
    if (m::is_namespace(p) && p != ^^::)
        return std::define_static_string(m::identifier_of(p));
    return "";
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
    return d == m::dealias(^^int32_t) || d == m::dealias(^^uint32_t) ||
           d == m::dealias(^^int16_t) || d == m::dealias(^^uint16_t) ||
           d == m::dealias(^^int8_t) || d == m::dealias(^^uint8_t);
}
consteval const char* int_name(m::info d)
{
    if (d == m::dealias(^^int32_t)) return "Int32";
    if (d == m::dealias(^^uint32_t)) return "UInt32";
    if (d == m::dealias(^^int16_t)) return "Int16";
    if (d == m::dealias(^^uint16_t)) return "UInt16";
    if (d == m::dealias(^^int8_t)) return "Int8";
    if (d == m::dealias(^^uint8_t)) return "UInt8";
    return "Unknown";
}
consteval const char* deki_type_name(m::info t)
{
    if (is_assetref(t)) return "AssetRef";
    if (is_objectref(t)) return "ObjectRef";
    if (is_noderef(t)) return "NodeRef";
    if (is_propertyref(t)) return "PropertyRef";
    if (is_vector(t)) return "Array";
    if (is_color(t)) return "Color";
    if (is_vec3(t)) return "Vector3";
    if (is_vec2(t)) return "Vector2";
    if (is_enum(t)) return "Enum";
    if (is_float(t)) return "Float";
    if (is_double(t)) return "Double";
    if (is_bool(t)) return "Bool";
    if (is_string(t)) return "String";
    return int_name(bare(t));
}
consteval const char* elem_type_name(m::info e)
{
    if (is_objectref(e)) return "ObjectRef";
    if (is_string(e)) return "String";
    if (is_float(e)) return "Float";
    return int_name(bare(e));
}
consteval const char* asset_arg(m::info t)
{
    if (is_assetref(t)) return std::define_static_string(m::identifier_of(m::template_arguments_of(bare(t))[0]));
    return nullptr;
}
consteval const char* component_ref(m::info t)
{
    if (is_objectref(t)) return std::define_static_string(m::identifier_of(m::template_arguments_of(bare(t))[0]));
    if (is_vector(t) && is_objectref(vec_elem(t)))
        return std::define_static_string(m::identifier_of(m::template_arguments_of(bare(vec_elem(t)))[0]));
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
        if (m::has_identifier(mem) && m::identifier_of(mem) == name) return true;
    return false;
}
consteval bool has_static_guid(m::info type)
{
    return has_static_member(type, "StaticGuid");
}
// Does the class itself declare an Update() member (of any kind)? Direct
// members only: an inherited Update is reported by the base's own override.
consteval bool declares_update(m::info type)
{
    return has_static_member(type, "Update");
}
consteval bool is_component(m::info type)
{
    return m::is_class_type(type) && m::is_complete_type(type) && has_static_guid(type);
}
// DEKI_NODE struct: has a StaticNodeName marker and is NOT a component (no
// StaticGuid). Disjoint from both components and DEKI_SERIALIZABLE structs, so a
// node gets its own emit path (reflection + node-factory registration).
consteval bool is_node(m::info type)
{
    return m::is_class_type(type) && m::is_complete_type(type) && has_static_member(type, "StaticNodeName") && !has_static_guid(type);
}
// Index into kPackages of the package a reflected type belongs to, or -1 when the type
// came from outside this unit (engine headers, a sibling package pulled in transitively).
// Membership is "the package's tag appears in the type's source path" — the same rule the
// single-package build has always used, just applied to every package in the unit.
// -2 means the path matched two tags; the caller static_asserts on it.
consteval int package_index_of(m::info type)
{
    std::string_view f = m::source_location_of(type).file_name();
    int found = -1;
    for (int i = 0; i < kPackageCount; ++i)
    {
        if (f.find(kPackages[i].tag) != std::string_view::npos)
        {
            if (found >= 0) return -2;
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
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::exported) return true;
    return false;
}
consteval bool is_editor_only(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::editor_only) return true;
    return false;
}
consteval bool is_serializable(m::info type)
{
    for (m::info a : m::annotations_of(type))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::serializable) return true;
    return false;
}
consteval bool has_no_serialize(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::no_serialize) return true;
    return false;
}
// nlohmann j.value() default literal per scalar type
consteval const char* json_default(m::info t)
{
    if (is_string(t)) return "\"\"";
    if (is_bool(t)) return "false";
    if (is_float(t) || is_double(t)) return "0.0f";
    return "0";
}
consteval std::vector<m::info> ser_members(m::info cls)
{
    std::vector<m::info> out;
    for (m::info mem : m::nonstatic_data_members_of(cls, m::access_context::unchecked()))
        if (!has_no_serialize(mem)) out.push_back(mem);
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
// invalidates hundreds of objects on every codegen re-run — even when the generated text
// is identical, which is the normal case when the re-run was triggered by an unrelated
// engine header.
inline void write_if_changed(const std::string& path, const std::string& content)
{
    {
        std::ifstream in(path);
        if (in)
        {
            std::string cur((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (cur == content) return;
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
            r.remove_prefix(1);
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
// Generated files are committed — embedded builds glob them and never run the
// codegen — so every build on a different machine rewrote all of them and the
// churn landed in every diff. Both halves matter: making the path relative
// removes the machine prefix, and normalising the separator removes the
// remaining Windows/POSIX difference.
inline std::string rel_src(std::string_view full, std::string_view sd)
{
    std::string s = src_rel_include(full, sd);
    for (char& c : s)
        if (c == '\\') c = '/';
    return s;
}

consteval const char* unit_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::unit) return m::extract<Deki::deki_refl::unit>(a).name;
    return nullptr;
}
consteval const char* vw_prop(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::visible_when) return m::extract<Deki::deki_refl::visible_when>(a).prop;
    return nullptr;
}
consteval const char* vw_vals(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::visible_when) return m::extract<Deki::deki_refl::visible_when>(a).values;
    return nullptr;
}
consteval const char* tooltip_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::tooltip) return m::extract<Deki::deki_refl::tooltip>(a).text;
    return nullptr;
}
consteval const char* group_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::group) return m::extract<Deki::deki_refl::group>(a).name;
    return nullptr;
}
consteval const char* toggle_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::toggle_object) return m::extract<Deki::deki_refl::toggle_object>(a).field;
    return nullptr;
}
// DEKI_OBJECT_NAME on a std::string field: the picker's component filter
// ("" = any object). Non-null is what marks the field as an object name.
consteval const char* objname_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::object_name) return m::extract<Deki::deki_refl::object_name>(a).component;
    return nullptr;
}
// DEKI_VALUE_OF on a std::string field: the ::Deki::PropertyRef field whose target
// types this literal's editor control.
consteval const char* valueof_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::value_of) return m::extract<Deki::deki_refl::value_of>(a).prop;
    return nullptr;
}
// DEKI_RENAMED_FROM: the key this member used to be serialised under.
consteval const char* renamed_from_of(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::renamed_from) return m::extract<Deki::deki_refl::renamed_from>(a).name;
    return nullptr;
}
consteval bool has_range(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::range) return true;
    return false;
}
consteval const char* range_lo(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::range) return m::extract<Deki::deki_refl::range>(a).lo;
    return nullptr;
}
consteval const char* range_hi(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::range) return m::extract<Deki::deki_refl::range>(a).hi;
    return nullptr;
}
consteval bool range_slider(m::info mem)
{
    for (m::info a : m::annotations_of(mem))
        if (m::remove_cv(m::type_of(a)) == ^^Deki::deki_refl::range) return m::extract<Deki::deki_refl::range>(a).slider;
    return false;
}
// Skip sentinel enumerators (COUNT/MAX/NUM/SIZE/LAST, NUM_*/MAX_*), like Python's scan_for_enums.
consteval bool is_sentinel(std::string_view n)
{
    std::string u;
    for (char c : n)
        u += (c >= 'a' && c <= 'z') ? char(c - 32) : c;
    if (u == "COUNT" || u == "MAX" || u == "NUM" || u == "SIZE" || u == "LAST") return true;
    if (u.size() >= 4 && (u.substr(0, 4) == "NUM_" || u.substr(0, 4) == "MAX_")) return true;
    return false;
}
// runtime helpers for DEKI_RANGE/DEKI_SLIDER (stringized args), matching Python:
// only simple numeric literals produce a range; floats format like Python str(float).
inline std::string _trim_f(std::string s)
{
    while (!s.empty() && s.front() == ' ')
        s.erase(s.begin());
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    if (!s.empty() && (s.back() == 'f' || s.back() == 'F')) s.pop_back();
    return s;
}
inline bool is_simple_num(const char* lit)
{
    std::string s = _trim_f(lit);
    if (s.empty()) return false;
    size_t i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) return false;
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
        if (s.find('.') == std::string::npos) s += ".0";
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
        out = "0." + std::string(-pointpos, '0') + digits;
    else if (pointpos >= (int)digits.size())
        out = digits + std::string(pointpos - (int)digits.size(), '0') + ".0";
    else
        out = digits.substr(0, pointpos) + "." + digits.substr(pointpos);
    if (out.find('.') == std::string::npos) out += ".0";
    return (neg ? "-" : "") + out;
}
consteval int enum_count(m::info t)
{
    int n = 0;
    for (m::info e : m::enumerators_of(bare(t)))
        if (!is_sentinel(m::identifier_of(e))) ++n;
    return n;
}

// ---- visibility ----
consteval m::info member_type_by_name(m::info cls, std::string_view name)
{
    for (m::info mem : m::nonstatic_data_members_of(cls, m::access_context::unchecked()))
        if (m::identifier_of(mem) == name) return m::type_of(mem);
    return ^^void;
}
consteval int enum_ordinal(m::info enumType, std::string_view name)
{
    int i = 0;
    for (m::info e : m::enumerators_of(bare(enumType)))
    {
        if (is_sentinel(m::identifier_of(e))) continue;
        if (m::identifier_of(e) == name) return i;
        ++i;
    }
    return -1;
}
consteval bool is_numeric_tok(std::string_view t)
{
    size_t i = 0;
    if (i < t.size() && t[i] == '-') ++i;
    if (i >= t.size()) return false;
    for (; i < t.size(); ++i)
        if (t[i] < '0' || t[i] > '9') return false;
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
        v = v * 10 + (t[i] - '0');
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
            ++i;
        size_t s = i;
        while (i < vals.size() && vals[i] != ',')
            ++i;
        size_t e = i;
        while (e > s && vals[e - 1] == ' ')
            --e;
        if (e > s)
        {
            std::string_view tok = vals.substr(s, e - s);
            if (is_numeric_tok(tok))
                resolved[n++] = atoi_tok(tok);
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
            vi.mask |= (1u << resolved[k]);
    }
    return vi;
}

// ---- member collection ----
consteval std::vector<m::info> own_exported(m::info cls)
{
    std::vector<m::info> out;
    for (m::info mem : m::nonstatic_data_members_of(cls, m::access_context::unchecked()))
        if (is_exported(mem)) out.push_back(mem);
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
                if (is_exported(mem)) out.push_back(mem);
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
        if (is_editor_only(mem)) return true;
    return false;
}
consteval const char* base_name(m::info cls)
{
    for (m::info base : m::bases_of(cls, m::access_context::unchecked()))
        return std::define_static_string(m::identifier_of(m::type_of(base)));
    return nullptr;
}
consteval bool has_component_base(m::info cls)
{
    for (m::info base : m::bases_of(cls, m::access_context::unchecked()))
        if (is_component(m::type_of(base))) return true;
    return false;
}
consteval int count_assetref(m::info cls)
{
    int n = 0;
    for (m::info mem : m::nonstatic_data_members_of(cls, m::access_context::unchecked()))
        if (is_exported(mem) && is_assetref(m::type_of(mem))) ++n;
    return n;
}
consteval int count_objectref(m::info cls)
{
    int n = 0;
    for (m::info mem : m::nonstatic_data_members_of(cls, m::access_context::unchecked()))
        if (is_exported(mem) && is_objref_like(m::type_of(mem))) ++n;
    return n;
}

template <m::info MEM>
void emit_case(std::ostream& out)
{
    constexpr m::info t = m::type_of(MEM);
    constexpr const char* nm = std::define_static_string(m::identifier_of(MEM));
    constexpr const char* rf = renamed_from_of(MEM);
    if constexpr (rf != nullptr)
    {
        if constexpr (std::string_view(rf) == std::string_view(nm))
            out << "#error DEKI_RENAMED_FROM on " << nm << " names its current identifier\n";
        // Stacked label: the old key falls into the same case body as the new one.
        out << "            case ::Deki::HashString(\"" << rf << "\"):  // DEKI_RENAMED_FROM\n";
    }
    if constexpr (is_assetref(t))
    {
        out
            << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t mapSize;\n"
            << "                if (parser.ReadMapSize(mapSize)) {\n"
            << "                    for (uint32_t i = 0; i < mapSize; i++) {\n"
            << "                        const char* k; uint16_t kLen;\n"
            << "                        if (!parser.ReadString(k, kLen)) break;\n"
            << "                        uint32_t kHash = ::Deki::HashStringLen(k, kLen);\n"
            << "                        switch (kHash) {\n"
            << "                            case ::Deki::HashString(\"guid\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) " << nm << ".guid = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"source\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) " << nm << ".source = std::string(s, len); break; }\n"
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
        out
            << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t mapSize;\n"
            << "                if (parser.ReadMapSize(mapSize)) {\n"
            << "                    for (uint32_t i = 0; i < mapSize; i++) {\n"
            << "                        const char* k; uint16_t kLen;\n"
            << "                        if (!parser.ReadString(k, kLen)) break;\n"
            << "                        uint32_t kHash = ::Deki::HashStringLen(k, kLen);\n"
            << "                        switch (kHash) {\n"
            << "                            case ::Deki::HashString(\"object\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) " << nm << ".object = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"component\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) " << nm << ".component = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"field\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) " << nm << ".field = std::string(s, len); break; }\n"
            << "                            default: parser.SkipValue(); break;\n"
            << "                        }\n"
            << "                    }\n"
            << "                    " << nm << ".Rehash();\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_objectref(t))
    {
        out
            << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint8_t _pb;\n"
            << "                if (parser.PeekByte(_pb) && ((_pb & 0xf0) == 0x90 || _pb == 0xdc || _pb == 0xdd)) {\n"
            << "                    uint32_t _as; if (parser.ReadArraySize(_as) && _as >= 2) {\n"
            << "                        int32_t _id, _tid; parser.ReadInt(_id); parser.ReadInt(_tid);\n"
            << "                        " << nm << ".id = static_cast<uint32_t>(_id); " << nm << ".componentTypeId = static_cast<uint32_t>(_tid);\n"
            << "                        for (uint32_t _s = 2; _s < _as; _s++) parser.SkipValue();\n"
            << "                    }\n"
            << "                } else {\n"
            << "                    int32_t v; if (parser.ReadInt(v)) { " << nm << ".id = static_cast<uint32_t>(v); " << nm << ".componentTypeId = 0; }\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_vector(t))
    {
        constexpr m::info e = vec_elem(t);
        if constexpr (is_objectref(e))
        {
            out
                << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    " << nm << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) {\n"
                << "                        decltype(" << nm << ")::value_type ref;\n"
                << "                        uint8_t _pb;\n"
                << "                        if (parser.PeekByte(_pb) && ((_pb & 0xf0) == 0x90 || _pb == 0xdc || _pb == 0xdd)) {\n"
                << "                            uint32_t _as; if (parser.ReadArraySize(_as) && _as >= 2) {\n"
                << "                                int32_t _id, _tid; parser.ReadInt(_id); parser.ReadInt(_tid);\n"
                << "                                ref.id = static_cast<uint32_t>(_id); ref.componentTypeId = static_cast<uint32_t>(_tid);\n"
                << "                                for (uint32_t _s = 2; _s < _as; _s++) parser.SkipValue();\n"
                << "                            }\n"
                << "                        } else {\n"
                << "                            int32_t v;\n"
                << "                            if (parser.ReadInt(v)) { ref.id = static_cast<uint32_t>(v); ref.componentTypeId = 0; }\n"
                << "                        }\n"
                << "                        " << nm << ".push_back(ref);\n"
                << "                    }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
        else if constexpr (is_string(e))
        {
            out
                << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    " << nm << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) {\n"
                << "                        const char* s; uint16_t len;\n"
                << "                        if (parser.ReadString(s, len)) " << nm << ".push_back(std::string(s, len));\n"
                << "                    }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
        else if constexpr (is_float(e))
        {
            out
                << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    " << nm << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) {\n"
                << "                        float v;\n"
                << "                        if (parser.ReadFloat(v)) " << nm << ".push_back(v);\n"
                << "                    }\n"
                << "                }\n"
                << "                break;\n"
                << "            }\n";
        }
        else
        {
            out
                << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
                << "                uint32_t arrSize;\n"
                << "                if (parser.ReadArraySize(arrSize)) {\n"
                << "                    " << nm << ".clear();\n"
                << "                    for (uint32_t i = 0; i < arrSize; i++) {\n"
                << "                        int32_t v;\n"
                << "                        if (parser.ReadInt(v)) " << nm << ".push_back(static_cast<decltype(" << nm << ")::value_type>(v));\n"
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
            << "                        " << nm << " = ::Deki::Vector3(x, y, z);\n"
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
            << "                        " << nm << " = ::Deki::Vector2(x, y);\n"
            << "                    }\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_color(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) " << nm << " = Deki::Color::FromRGBA8888(static_cast<uint32_t>(v)); break; }\n";
    }
    else if constexpr (is_enum(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) " << nm << " = static_cast<decltype(" << nm << ")>(v); break; }\n";
    }
    else if constexpr (is_float(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { float v; if (parser.ReadFloat(v)) " << nm << " = v; break; }\n";
    }
    else if constexpr (is_double(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { float v; if (parser.ReadFloat(v)) " << nm << " = static_cast<double>(v); break; }\n";
    }
    else if constexpr (is_bool(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { bool v; if (parser.ReadBool(v)) " << nm << " = v; break; }\n";
    }
    else if constexpr (is_string(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) " << nm << " = std::string(s, len); break; }\n";
    }
    else if constexpr (is_int_like(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) " << nm << " = static_cast<decltype(" << nm << ")>(v); break; }\n";
    }
}

template <m::info COMP>
void emit_component(std::ostream& out, std::string_view srcdir)
{
    const char* CLS = std::define_static_string(m::identifier_of(COMP));

    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Source: " << rel_src(m::source_location_of(COMP).file_name(), srcdir) << "\n\n";
    out << "#include \"" << src_rel_include(m::source_location_of(COMP).file_name(), srcdir) << "\"\n";
    out << "#include <deki/SceneMessagePack.h>\n";
    out << "#include <deki/Scene.h>\n";
    out << "#include <unordered_map>\n\n";

    // Definitions must live in the class's own namespace (engine components are
    // in namespace Deki; package and project components are global).
    constexpr const char* NS = namespace_of(COMP);
    if constexpr (NS[0] != '\0')
        out << "namespace " << NS << " {\n\n";

    out << SEP << "\n// Runtime Deserialization (all platforms)\n"
        << SEP << "\n\n";
    out << "bool " << CLS << "::Deserialize(::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize) {\n";
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
        template for (constexpr auto mem : std::define_static_array(deser_members(COMP))) if constexpr (!is_editor_only(mem)) emit_case<mem>(out);
        if constexpr (any_editor_only(COMP))
        {
            out << "#ifdef DEKI_EDITOR\n";
            template for (constexpr auto mem : std::define_static_array(deser_members(COMP))) if constexpr (is_editor_only(mem)) emit_case<mem>(out);
            out << "#else\n";
            template for (constexpr auto mem : std::define_static_array(deser_members(COMP))) if constexpr (is_editor_only(mem))
            {
                if constexpr (renamed_from_of(mem) != nullptr)
                    out << "            case ::Deki::HashString(\"" << renamed_from_of(mem) << "\"):  // DEKI_RENAMED_FROM\n";
                out << "            case ::Deki::HashString(\"" << m::identifier_of(mem) << "\"): parser.SkipValue(); break;\n";
            }
            out << "#endif\n";
        }
        out << "            default: parser.SkipValue(); break;\n";
        out << "        }\n";
    }
    out << "    }\n    return true;\n}\n\n";

    constexpr bool HB = has_component_base(COMP);
    constexpr int NOBJ = count_objectref(COMP);
    constexpr int NASSET = count_assetref(COMP);
    out << SEP << "\n// ObjectRef/AssetRef Resolution (all platforms) - Zero Overhead\n"
        << SEP << "\n\n";
    out << "void " << CLS << "::ResolveObjectRefs(::Deki::Scene* scene) {\n";
    if constexpr (NOBJ == 0 && NASSET == 0)
    {
        if constexpr (HB)
            out << "    " << base_name(COMP) << "::ResolveObjectRefs(scene);\n";
        else
            out << "    (void)scene; // No ::Deki::ObjectRef or AssetRef properties\n";
    }
    else
    {
        if constexpr (HB) out << "    " << base_name(COMP) << "::ResolveObjectRefs(scene);\n";
        if constexpr (NOBJ == 0 && !HB) out << "(void)scene; // AssetRef-only, no ::Deki::ObjectRef resolution needed\n";
        template for (constexpr auto mem : std::define_static_array(own_exported(COMP)))
        {
            constexpr m::info t = m::type_of(mem);
            constexpr const char* nm = std::define_static_string(m::identifier_of(mem));
            if constexpr (is_objectref(t))
            {
                out << "    if (" << nm << ".id != 0 && " << nm << ".resolved == nullptr)\n"
                    << "        " << nm << ".resolved = scene->FindDekiObject(" << nm << ".id);\n";
            }
            else if constexpr (is_vector(t) && is_objectref(vec_elem(t)))
            {
                out << "    for (auto& ref : " << nm << ") {\n"
                    << "        if (ref.id != 0 && ref.resolved == nullptr)\n"
                    << "            ref.resolved = scene->FindDekiObject(ref.id);\n"
                    << "    }\n";
            }
        }
        template for (constexpr auto mem : std::define_static_array(own_exported(COMP)))
        {
            constexpr m::info t = m::type_of(mem);
            if constexpr (is_assetref(t))
            {
                constexpr const char* nm = std::define_static_string(m::identifier_of(mem));
                constexpr const char* at = asset_arg(t);
                if constexpr (std::string_view(at) == "Scene")
                {
                    out << "    // AssetRef<Scene> \"" << nm << "\" — deferred (non-cacheable, loaded on-demand via Get())\n";
                }
                else
                {
                    out
                        << "    if (auto* _loaded_" << nm << " = this->" << nm << ".Get()) {\n"
                        << "        const char* _frameGuid = this->" << nm << ".source.empty() ? this->" << nm << ".guid.c_str() : this->" << nm << ".source.c_str();\n"
                        << "        OnAssetRefResolved(\"" << nm << "\", _loaded_" << nm << ", _frameGuid);\n"
                        << "    }\n";
                }
            }
        }
    }
    out << "}\n\n";

    // Whether LifecycleManager should call Update() on this type at all. True
    // when the class declares Update itself, else the reflected base's answer,
    // else false (::Deki::Behaviour's own Update is the empty default).
    out << SEP << "\n// Update declaration (all platforms)\n"
        << SEP << "\n\n";
    out << "bool " << CLS << "::DeclaresUpdate() const {\n";
    if constexpr (declares_update(COMP))
        out << "    return true;\n";
    else if constexpr (HB)
        out << "    return " << base_name(COMP) << "::DeclaresUpdate();\n";
    else
        out << "    return false;\n";
    out << "}\n\n";

    out << SEP << "\n// ::Deki::ObjectRef ID Remapping (all platforms)\n"
        << SEP << "\n\n";
    out << "void " << CLS << "::RemapObjectRefs(const std::unordered_map<uint32_t, uint32_t>& idRemap) {\n";
    if constexpr (NOBJ == 0)
    {
        if constexpr (HB)
            out << "    " << base_name(COMP) << "::RemapObjectRefs(idRemap);\n";
        else
            out << "    (void)idRemap; // No ::Deki::ObjectRef properties\n";
    }
    else
    {
        if constexpr (HB) out << "    " << base_name(COMP) << "::RemapObjectRefs(idRemap);\n";
        template for (constexpr auto mem : std::define_static_array(own_exported(COMP)))
        {
            constexpr m::info t = m::type_of(mem);
            constexpr const char* nm = std::define_static_string(m::identifier_of(mem));
            if constexpr (is_objectref(t))
            {
                out << "    { auto it = idRemap.find(" << nm << ".id);\n"
                    << "      if (it != idRemap.end()) { " << nm << ".id = it->second; " << nm << ".resolved = nullptr; } }\n";
            }
            else if constexpr (is_vector(t) && is_objectref(vec_elem(t)))
            {
                out << "    for (auto& ref : " << nm << ") {\n"
                    << "        auto it = idRemap.find(ref.id);\n"
                    << "        if (it != idRemap.end()) { ref.id = it->second; ref.resolved = nullptr; }\n"
                    << "    }\n";
            }
        }
    }
    out << "}\n\n";

    // ---- Compact field table (ALL platforms, embedded included) ----
    // Flattened: own + inherited DEKI_EXPORT fields, each offset computed in THIS
    // class so no base-subobject layout is assumed. 8 bytes per entry, the only
    // reflection a device build gets. See ::Deki::FieldRef / ComponentRegistry.
    out << SEP << "\n// Compact Field Table (all platforms)\n"
        << SEP << "\n\n";
    out << "const ::Deki::FieldRef* " << CLS << "::GetFields() {\n";
    int fieldCount = 0;
    if constexpr (deser_members(COMP).empty())
    {
        out << "    return nullptr;\n}\n\n";
    }
    else
    {
        out << "    static const ::Deki::FieldRef fields[] = {\n";
        template for (constexpr auto mem : std::define_static_array(deser_members(COMP)))
        {
            constexpr m::info t = m::type_of(mem);
            ++fieldCount;
            out << "        { ::Deki::HashString(\"" << m::identifier_of(mem) << "\"), "
                << "(uint16_t)offsetof(" << CLS << ", " << m::identifier_of(mem) << "), "
                << "(uint8_t)::Deki::PropertyType::" << deki_type_name(t) << ", ";
            if constexpr (is_enum(t))
                out << sizeof(typename[:bare(t):]);
            else
                out << "0";
            out << " },\n";
            // DEKI_RENAMED_FROM: the old key resolves to the same field on device
            // (FindField by name for ::Deki::PropertyRef, FSM and tween targets).
            if constexpr (renamed_from_of(mem) != nullptr)
            {
                ++fieldCount;
                out << "        { ::Deki::HashString(\"" << renamed_from_of(mem) << "\"), "
                    << "(uint16_t)offsetof(" << CLS << ", " << m::identifier_of(mem) << "), "
                    << "(uint8_t)::Deki::PropertyType::" << deki_type_name(t) << ", ";
                if constexpr (is_enum(t))
                    out << sizeof(typename[:bare(t):]);
                else
                    out << "0";
                out << " },  // DEKI_RENAMED_FROM\n";
            }
        }
        out << "    };\n    return fields;\n}\n\n";
    }
    out << "int " << CLS << "::GetFieldCount() { return " << fieldCount << "; }\n\n";

    out << SEP << "\n// Editor Reflection (editor-only)\n"
        << SEP << "\n\n";
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
                        if (!f) out << ", ";
                        f = false;
                        out << "\"" << m::identifier_of(e) << "\"";
                    }
                }
                out << " };\n";
                out << "#endif\n";
            }
        }
    }
    if (anyEnum) out << "\n";

    out << "const ::Deki::PropertyInfo* " << CLS << "::GetProperties() {\n";
    int count = 0;
    if constexpr (own_exported(COMP).empty())
    {
        out << "    return nullptr;\n}\n\n";
    }
    else
    {
        out << "    static const ::Deki::PropertyInfo props[] = {\n";
        bool first = true;
        std::string curGroup;  // DEKI_GROUP is sticky: applies to subsequent fields until changed
        template for (constexpr auto mem : std::define_static_array(own_exported(COMP)))
        {
            constexpr m::info t = m::type_of(mem);
            if (!first) out << ",";
            first = false;
            ++count;
            out << "    { \"" << m::identifier_of(mem) << "\", nullptr, ::Deki::PropertyType::"
                << deki_type_name(t) << ", offsetof(" << CLS << ", " << m::identifier_of(mem) << "),\n";
            out << "      ";
            constexpr const char* tip = tooltip_of(mem);
            if constexpr (tip != nullptr)
                out << "\"" << tip << "\", ";
            else
                out << "nullptr, ";
            if constexpr (has_range(mem))
            {
                constexpr const char* rlo = range_lo(mem);
                constexpr const char* rhi = range_hi(mem);
                if (is_simple_num(rlo) && is_simple_num(rhi))
                    out << py_float(rlo) << ", " << py_float(rhi) << ", true, ";
                else
                    out << "0, 0, false, ";
            }
            else
                out << "0, 0, false, ";
            if constexpr (is_enum(t))
            {
                constexpr size_t esz = sizeof(typename[:bare(t):]);
                out << "nullptr, s_" << enum_name(t) << "_Values, " << enum_count(t) << ", " << esz << ", ::Deki::PropertyType::Int32, ";
            }
            else
            {
                constexpr const char* a = asset_arg(t);
                if constexpr (a != nullptr)
                    out << "\"" << a << "\", ";
                else
                    out << "nullptr, ";
                out << "nullptr, 0, 0, ::Deki::PropertyType::";
                if constexpr (is_vector(t))
                    out << elem_type_name(vec_elem(t));
                else
                    out << "Int32";
                out << ", ";
            }
            constexpr const char* vp = vw_prop(mem);
            if constexpr (vp != nullptr)
            {
                constexpr auto vis = resolve_vis(member_type_by_name(COMP, vp), vw_vals(mem));
                out << "\"" << vp << "\", " << vis.min << ", " << vis.mask << ", ";
            }
            else
                out << "nullptr, 0, 0, ";
            // componentRefType: the ::Deki::ObjectRef<T> argument, or DEKI_OBJECT_NAME's filter
            // on a String field (the editor's object-name picker; "" = any object).
            constexpr const char* cr = component_ref(t);
            constexpr const char* on = objname_of(mem);
            if constexpr (cr != nullptr)
                out << "\"" << cr << "\", ";
            else if constexpr (on != nullptr)
                out << "\"" << on << "\", ";
            else
                out << "nullptr, ";
            out << "false, false, nullptr, ";                       // isMultiline, triggersAssetRegen, assetRegenType
            out << (range_slider(mem) ? "true" : "false") << ", ";  // useSlider
            constexpr const char* grp = group_of(mem);
            if constexpr (grp != nullptr) curGroup = grp;  // sticky
            if (!curGroup.empty())
                out << "\"" << curGroup << "\", ";
            else
                out << "nullptr, ";
            constexpr const char* tog = toggle_of(mem);
            if constexpr (tog != nullptr)
                out << "\"" << tog << "\", ";
            else
                out << "nullptr, ";
            out << "::Deki::PhysicalUnit::";
            constexpr const char* u = unit_of(mem);
            if constexpr (u != nullptr)
                out << u;
            else
                out << "None";
            constexpr const char* vo = valueof_of(mem);  // DEKI_VALUE_OF
            if constexpr (vo != nullptr) out << ", \"" << vo << "\"";
            constexpr const char* rfm = renamed_from_of(mem);  // DEKI_RENAMED_FROM
            if constexpr (rfm != nullptr)
            {
                if constexpr (vo == nullptr) out << ", nullptr";
                out << ", \"" << rfm << "\"";
            }
            out << " }";
        }
        out << "\n    };\n    return props;\n}\n\n";
    }
    out << "int " << CLS << "::GetPropertyCount() { return " << count << "; }\n\n";

    out << "const ::Deki::ComponentMeta& " << CLS << "::GetComponentMeta() {\n";
    out << "    static const ::Deki::ComponentMeta meta = {\n";
    out << "        \"" << CLS << "\",\n";
    out << "        \"" << [:COMP:] ::StaticGuid << "\",  // serializedName (GUID)\n";
    out << "        \"" << [:COMP:] ::ComponentCategory << "\",  // Category\n";
    if constexpr (has_static_member(COMP, "StaticDescription"))
        out << "        " << CLS << "::StaticDescription,   // Description (DEKI_DESCRIPTION)\n";
    else
        out << "        \"\",      // Description (none declared)\n";
    out << "        " << CLS << "::StaticType,\n";
    out << "        " << base_name(COMP) << "::StaticType,\n";
    out << "        " << (m::is_abstract_type(COMP) ? "true" : "false") << ",   // isAbstract\n";
    out << "        false,   // isUnique\n";
    out << "        " << (std::string(CLS) == "PlatformSetupComponent" ? "true" : "false") << ",   // bootSceneOnly\n";
    out << "        false,   // platformSceneOnly\n";
    out << "        sizeof(" << CLS << "),\n";
    if constexpr (m::is_abstract_type(COMP))
        out << "        nullptr,\n";
    else
        out << "        []() -> ::Deki::Component* { return new " << CLS << "(); },\n";
    out << "        " << CLS << "::GetProperties(),\n";
    out << "        " << CLS << "::GetPropertyCount(),\n";
    out << "        {}, false";
    if constexpr (has_static_member(COMP, "StaticDisplayName"))
        out << ",\n        \"" << [:COMP:] ::StaticDisplayName << "\"   // displayNameOverride (from DEKI_DISPLAY_NAME)";
    out << "\n    };\n    return meta;\n}\n\n";

    out << "#endif // DEKI_EDITOR\n";

    if constexpr (NS[0] != '\0')
        out << "\n}  // namespace " << NS << "\n";
}

// ---- component .gen.h stub ----
template <m::info COMP>
void emit_component_header(std::ostream& out, std::string_view srcdir)
{
    const char* CLS = std::define_static_string(m::identifier_of(COMP));
    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Source: " << rel_src(m::source_location_of(COMP).file_name(), srcdir) << "\n";
    out << "// NOTE: Function definitions are in this package's merged *Reflection*.gen.cpp\n";
    out << "#pragma once\n\n";
    out << "// GetProperties(), GetPropertyCount(), GetComponentMeta(), and ResolveObjectRefs()\n";
    out << "// are defined there, not here: one generated TU per package, one stub per type.\n";
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
            out << "#error DEKI_RENAMED_FROM on " << nm << " names its current identifier\n";
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
                << "                        if (parser.ReadString(s, len)) obj." << nm << ".push_back(std::string(s, len));\n"
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
                << "                    for (uint32_t i = 0; i < arrSize; i++) { float v; if (parser.ReadFloat(v)) obj." << nm << ".push_back(v); }\n"
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
                << "                    for (uint32_t i = 0; i < arrSize; i++) { int32_t v; if (parser.ReadInt(v)) obj." << nm << ".push_back(static_cast<decltype(obj." << nm << ")::value_type>(v)); }\n"
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
            << "                if (parser.ReadArraySize(arrSize) && arrSize >= 3) { float x, y, z; if (parser.ReadFloat(x) && parser.ReadFloat(y) && parser.ReadFloat(z)) obj." << nm << " = ::Deki::Vector3(x, y, z); }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_vec2(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): {\n"
            << "                uint32_t arrSize;\n"
            << "                if (parser.ReadArraySize(arrSize) && arrSize >= 2) { float x, y; if (parser.ReadFloat(x) && parser.ReadFloat(y)) obj." << nm << " = ::Deki::Vector2(x, y); }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_color(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) obj." << nm << " = Deki::Color::FromRGBA8888(static_cast<uint32_t>(v)); break; }\n";
    }
    else if constexpr (is_enum(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) obj." << nm << " = static_cast<decltype(obj." << nm << ")>(v); break; }\n";
    }
    else if constexpr (is_float(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { float v; if (parser.ReadFloat(v)) obj." << nm << " = v; break; }\n";
    }
    else if constexpr (is_double(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { float v; if (parser.ReadFloat(v)) obj." << nm << " = static_cast<double>(v); break; }\n";
    }
    else if constexpr (is_bool(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { bool v; if (parser.ReadBool(v)) obj." << nm << " = v; break; }\n";
    }
    else if constexpr (is_string(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) obj." << nm << " = std::string(s, len); break; }\n";
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
            << "                            case ::Deki::HashString(\"guid\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) obj." << nm << ".guid = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"source\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) obj." << nm << ".source = std::string(s, len); break; }\n"
            << "                            default: parser.SkipValue(); break;\n"
            << "                        }\n"
            << "                    }\n"
            << "                }\n"
            << "                break;\n"
            << "            }\n";
    }
    else if constexpr (is_noderef(t))
    {
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) obj." << nm << ".targetId = static_cast<uint32_t>(v); break; }\n";
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
            << "                            case ::Deki::HashString(\"object\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) obj." << nm << ".object = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"component\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) obj." << nm << ".component = std::string(s, len); break; }\n"
            << "                            case ::Deki::HashString(\"field\"): { const char* s; uint16_t len; if (parser.ReadString(s, len)) obj." << nm << ".field = std::string(s, len); break; }\n"
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
        out << "            case ::Deki::HashString(\"" << nm << "\"): { int32_t v; if (parser.ReadInt(v)) obj." << nm << " = static_cast<decltype(obj." << nm << ")>(v); break; }\n";
    }
}

// ---- DEKI_SERIALIZABLE struct: Serialize/Deserialize free functions ----
template <m::info S>
void emit_serializable(std::ostream& out, std::string_view srcdir)
{
    const char* NM = std::define_static_string(m::identifier_of(S));
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
            constexpr const char* en = std::define_static_string(m::identifier_of(bare(vec_elem(t))));
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
    out << SEP << "\n// Runtime MessagePack Deserialization (all platforms)\n"
        << SEP << "\n\n";
    out << "#include <deki/SceneMessagePack.h>\n";
    out << "#include <deki/Scene.h>\n\n";
    out << "bool DeserializeMsgPack(" << NM << "& obj, ::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize) {\n";
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
        template for (constexpr auto mem : std::define_static_array(ser_members(S)))
            emit_msgpack_case<mem>(out);
        out << "            default: parser.SkipValue(); break;\n";
        out << "        }\n";
    }
    out << "    }\n    return true;\n}\n";
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
    out << "namespace Deki::SceneFormat { class SceneMsgPackParser; }\n";
    out << "bool DeserializeMsgPack(" << NM << "& obj, ::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize);\n";
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

    // Runtime MessagePack deserialization (all platforms): a free function so the
    // NodeFactory thunk and the generic NodeGraph loader can call it.
    out << SEP << "\n// Runtime MessagePack Deserialization (all platforms)\n"
        << SEP << "\n\n";
    out << "bool DeserializeMsgPack(" << CLS << "& obj, ::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize) {\n";
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
        template for (constexpr auto mem : std::define_static_array(own_exported(NODE)))
            emit_msgpack_case<mem>(out);
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
                        if (!f) out << ", ";
                        f = false;
                        out << "\"" << m::identifier_of(e) << "\"";
                    }
                }
                out << " };\n";
                out << "#endif\n";
            }
        }
    }
    if (anyEnum) out << "\n";

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
            if (!first) out << ",";
            first = false;
            ++count;
            out << "    { \"" << m::identifier_of(mem) << "\", nullptr, ::Deki::PropertyType::"
                << deki_type_name(t) << ", offsetof(" << CLS << ", " << m::identifier_of(mem) << "),\n";
            out << "      ";
            constexpr const char* tip = tooltip_of(mem);
            if constexpr (tip != nullptr)
                out << "\"" << tip << "\", ";
            else
                out << "nullptr, ";
            if constexpr (has_range(mem))
            {
                constexpr const char* rlo = range_lo(mem);
                constexpr const char* rhi = range_hi(mem);
                if (is_simple_num(rlo) && is_simple_num(rhi))
                    out << py_float(rlo) << ", " << py_float(rhi) << ", true, ";
                else
                    out << "0, 0, false, ";
            }
            else
                out << "0, 0, false, ";
            if constexpr (is_enum(t))
            {
                constexpr size_t esz = sizeof(typename[:bare(t):]);
                out << "nullptr, s_" << enum_name(t) << "_Values, " << enum_count(t) << ", " << esz << ", ::Deki::PropertyType::Int32, ";
            }
            else
            {
                constexpr const char* a = asset_arg(t);
                if constexpr (a != nullptr)
                    out << "\"" << a << "\", ";
                else
                    out << "nullptr, ";
                out << "nullptr, 0, 0, ::Deki::PropertyType::";
                if constexpr (is_vector(t))
                    out << elem_type_name(vec_elem(t));
                else
                    out << "Int32";
                out << ", ";
            }
            constexpr const char* vp = vw_prop(mem);
            if constexpr (vp != nullptr)
            {
                constexpr auto vis = resolve_vis(member_type_by_name(NODE, vp), vw_vals(mem));
                out << "\"" << vp << "\", " << vis.min << ", " << vis.mask << ", ";
            }
            else
                out << "nullptr, 0, 0, ";
            // componentRefType: the ::Deki::ObjectRef<T> argument, or DEKI_OBJECT_NAME's filter
            // on a String field (the editor's object-name picker; "" = any object).
            constexpr const char* cr = component_ref(t);
            constexpr const char* on = objname_of(mem);
            if constexpr (cr != nullptr)
                out << "\"" << cr << "\", ";
            else if constexpr (on != nullptr)
                out << "\"" << on << "\", ";
            else
                out << "nullptr, ";
            out << "false, false, nullptr, ";                       // isMultiline, triggersAssetRegen, assetRegenType
            out << (range_slider(mem) ? "true" : "false") << ", ";  // useSlider
            constexpr const char* grp = group_of(mem);
            if constexpr (grp != nullptr) curGroup = grp;  // sticky
            if (!curGroup.empty())
                out << "\"" << curGroup << "\", ";
            else
                out << "nullptr, ";
            constexpr const char* tog = toggle_of(mem);
            if constexpr (tog != nullptr)
                out << "\"" << tog << "\", ";
            else
                out << "nullptr, ";
            out << "::Deki::PhysicalUnit::";
            constexpr const char* u = unit_of(mem);
            if constexpr (u != nullptr)
                out << u;
            else
                out << "None";
            constexpr const char* vo = valueof_of(mem);  // DEKI_VALUE_OF
            if constexpr (vo != nullptr) out << ", \"" << vo << "\"";
            out << " }";
        }
        out << "\n    };\n    return props;\n}\n\n";
    }
    out << "int " << CLS << "::GetPropertyCount() { return " << count << "; }\n\n";

    out << "const DekiNodeMeta& " << CLS << "::GetNodeMeta() {\n";
    out << "    static const DekiNodeMeta meta = {\n";
    out << "        " << CLS << "::StaticNodeName,\n";
    out << "        " << CLS << "::StaticNodeCategory,   // \"Domain/MenuGroup\"\n";
    out << "        ::Deki::HashString(" << CLS << "::StaticNodeName),   // typeId\n";
    out << "        " << CLS << "::GetProperties(),\n";
    out << "        " << CLS << "::GetPropertyCount(),\n";
    // Pins are optional (DEKI_NODE_INPUTS / DEKI_NODE_OUTPUTS / DEKI_NODE_DYNAMIC_OUTPUTS).
    if constexpr (has_static_member(NODE, "StaticInputPins"))
        out << "        " << CLS << "::StaticInputPins, " << CLS << "::StaticInputPinCount,\n";
    else
        out << "        nullptr, 0,   // no input pins\n";
    if constexpr (has_static_member(NODE, "StaticOutputPins"))
        out << "        " << CLS << "::StaticOutputPins, " << CLS << "::StaticOutputPinCount,\n";
    else
        out << "        nullptr, 0,   // no fixed output pins\n";
    if constexpr (has_static_member(NODE, "StaticDynamicOutputsProperty"))
        out << "        " << CLS << "::StaticDynamicOutputsProperty,\n";
    else
        out << "        nullptr,   // fixed outputs\n";
    out << "        []() -> void* { return new " << CLS << "(); },\n";
    out << "        [](void* p) { delete static_cast<" << CLS << "*>(p); },\n";
    out << "        sizeof(" << CLS << "),\n";
    // Trailing fields are APPEND-ONLY (see DekiNodeMeta): displayName, then
    // childCategory, then titleProperty, then permanent.
    if constexpr (has_static_member(NODE, "StaticNodeDisplayName"))
        out << "        " << CLS << "::StaticNodeDisplayName,\n";
    else
        out << "        nullptr,   // displayName (editor derives from name)\n";
    if constexpr (has_static_member(NODE, "StaticNodeChildCategory"))
        out << "        " << CLS << "::StaticNodeChildCategory,\n";
    else
        out << "        nullptr,   // childCategory (no child stack)\n";
    if constexpr (has_static_member(NODE, "StaticNodeTitleProperty"))
        out << "        " << CLS << "::StaticNodeTitleProperty,\n";
    else
        out << "        nullptr,   // titleProperty (title = display name)\n";
    if constexpr (has_static_member(NODE, "StaticNodePermanent"))
        out << "        " << CLS << "::StaticNodePermanent,\n";
    else
        out << "        false,   // permanent (freely addable/deletable)\n";
    if constexpr (has_static_member(NODE, "StaticNodeVariables"))
        out << "        " << CLS << "::StaticNodeVariables,\n";
    else
        out << "        false,   // declaresVariables (ordinary child stack)\n";
    if constexpr (has_static_member(NODE, "StaticNodeSubgraphCategory"))
        out << "        " << CLS << "::StaticNodeSubgraphCategory,\n";
    else
        out << "        nullptr,   // subgraphCategory (no inner graph)\n";
    if constexpr (has_static_member(NODE, "StaticNodeSubgraphEntry"))
        out << "        " << CLS << "::StaticNodeSubgraphEntry,\n";
    else
        out << "        nullptr,   // subgraphEntry (no seeded entry node)\n";
    if constexpr (has_static_member(NODE, "StaticNodeDescription"))
        out << "        " << CLS << "::StaticNodeDescription\n";
    else
        out << "        nullptr   // description (add picker shows the type name instead)\n";
    out << "    };\n    return meta;\n}\n\n";

    out << "#endif // DEKI_EDITOR\n\n";

    // Self-registration, inline (build-config-agnostic; no PluginExports.cpp edit).
    // REGISTER_RUNTIME_NODE -> NodeFactory (all platforms); REGISTER_NODE -> NodeTypeRegistry (editor).
    out << "REGISTER_RUNTIME_NODE(" << CLS << ");\n";
    out << "#ifdef DEKI_EDITOR\n";
    out << "REGISTER_NODE(" << CLS << ");\n";
    out << "#endif\n";
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
    out << "namespace Deki::SceneFormat { class SceneMsgPackParser; }\n";
    out << "bool DeserializeMsgPack(" << CLS << "& obj, ::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize);\n";
}

// ---- package registration: {Prefix}Registration.gen.cpp ----
// comps = (className, isAbstract) for every component in the package (from reflection).
// pkg carries the prefix and the adapter list the CMake step scanned out of the .cpp files.
// This is emitted from shard 0 only, and comps is the package's FULL component list (every
// shard collects it; only the emitting of per-component files is sharded).
struct CompInfo
{
    std::string name;   // unqualified identifier (also the header's stem)
    bool abstract;
    std::string inc;
    std::string ns;     // enclosing namespace, "" at global scope
};
inline std::string qualified_name(const CompInfo& c)
{
    return c.ns.empty() ? c.name : c.ns + "::" + c.name;
}
inline bool by_name(const CompInfo& a, const CompInfo& b)
{
    return a.name < b.name;
}
inline bool by_inc(const CompInfo& a, const CompInfo& b)
{
    return a.inc < b.inc;
}

void emit_registration(std::ostream& out, std::vector<CompInfo> comps, const CodegenPackage& pkg)
{
    const char* const PACKAGE_PREFIX = pkg.prefix;
    const char* const* kAdapters = pkg.adapters;
    const int kAdapterCount = pkg.adapterCount;
    std::sort(comps.begin(), comps.end(), by_name);
    std::vector<std::string> concrete, abstr;        // qualified: named from code at global scope
    std::vector<std::string> concreteInc, abstrInc;  // bare: the header is named after the class
    for (auto& c : comps)
    {
        (c.abstract ? abstr : concrete).push_back(qualified_name(c));
        (c.abstract ? abstrInc : concreteInc).push_back(c.name);
    }
    out << "// AUTO-GENERATED by the Deki reflection codegen: " << PACKAGE_PREFIX << " package registration\n";
    out << "// " << concrete.size() << " concrete + " << abstr.size() << " abstract component(s)\n\n";
    out << "#include <deki/reflection/ComponentFactory.h>\n";
    // ComponentRegistry is needed on ALL platforms now: the compact field tables
    // (RegisterFields) live there, only the rich metadata is editor-only.
    out << "#include <deki/reflection/ComponentRegistry.h>\n";
    for (auto& n : concreteInc)
        out << "#include \"" << n << ".h\"\n";
    for (auto& n : abstrInc)
        out << "#include \"" << n << ".h\"\n";
    out << "\n";
    if (kAdapterCount > 0)
    {
        out << "// Interface adapter registrations (explicit calls for static-lib builds)\n";
        for (int i = 0; i < kAdapterCount; ++i)
            out << "extern void " << kAdapters[i] << "();\n";
        out << "\n";
    }
    out << "void " << PACKAGE_PREFIX << "_RegisterComponents() {\n";
    if (kAdapterCount > 0)
    {
        out << "    // Register interface adapters first (stripped by linker in static-lib builds)\n";
        for (int i = 0; i < kAdapterCount; ++i)
            out << "    " << kAdapters[i] << "();\n";
        out << "\n";
    }
    out << "    auto& factory = ::Deki::ComponentFactory::Instance();\n";
    for (auto& n : concrete)
        out << "    factory.Register(" << n << "::StaticType, []() -> ::Deki::Component* { return new " << n << "(); });\n";
    out << "\n    // Compact field tables (all platforms — see ::Deki::FieldRef)\n";
    out << "    auto& registry = ::Deki::ComponentRegistry::Instance();\n";
    for (auto& n : concrete)
        out << "    registry.RegisterFields(" << n << "::StaticType, " << n << "::GetFields(), " << n << "::GetFieldCount());\n";
    out << "#ifdef DEKI_EDITOR\n";
    for (auto& n : concrete)
        out << "    registry.Register(&" << n << "::GetComponentMeta());\n";
    for (auto& n : abstr)
        out << "    registry.Register(&" << n << "::GetComponentMeta());\n";
    out << "#endif\n";
    out << "}\n\n";
    out << "int " << PACKAGE_PREFIX << "_GetAutoComponentCount() {\n    return " << concrete.size() << ";\n}\n\n";
    out << "#ifdef DEKI_EDITOR\n";
    out << "const ::Deki::ComponentMeta* " << PACKAGE_PREFIX << "_GetAutoComponentMeta(int index) {\n";
    out << "    switch (index) {\n";
    for (size_t i = 0; i < concrete.size(); ++i)
        out << "        case " << i << ": return &" << concrete[i] << "::GetComponentMeta();\n";
    out << "        default: return nullptr;\n";
    out << "    }\n}\n#endif\n";
}

// CoreRegistration.gen.cpp (engine-core build): REGISTER_* macros + ESP32 function,
// a different format from the per-package registration. Used when DEKI_CORE_BUILD is set.
void emit_core_registration(std::ostream& out, std::vector<CompInfo> comps)
{
    std::vector<CompInfo> byInc = comps;
    std::sort(byInc.begin(), byInc.end(), by_inc);
    std::vector<CompInfo> byNm = comps;
    std::sort(byNm.begin(), byNm.end(), by_name);
    std::vector<std::string> concrete;
    for (auto& c : byNm)
        if (!c.abstract) concrete.push_back(qualified_name(c));
    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Core engine component registration (" << comps.size() << " components)\n\n";
    out << "#include <deki/reflection/ComponentFactory.h>\n";
    // All platforms: the compact field tables live in ComponentRegistry.
    out << "#include <deki/reflection/ComponentRegistry.h>\n";
    for (auto& c : byInc)
        out << "#include \"" << c.inc << "\"\n";
    out << "\n"
        << SEP << "\n// Desktop/Editor: Static initializer registration\n"
        << SEP << "\n";
    out << "// REGISTER_RUNTIME_COMPONENT registers with ComponentFactory at program startup.\n";
    out << "// REGISTER_COMPONENT registers with ComponentRegistry (editor-only).\n\n";
    // REGISTER_* token-paste the class name, so they are emitted inside the
    // class's namespace with the bare identifier.
    auto registerMacros = [&](const char* macro, bool concreteOnly)
    {
        std::string open;
        for (auto& c : byNm)
        {
            if (concreteOnly && c.abstract) continue;
            if (c.ns != open)
            {
                if (!open.empty()) out << "}  // namespace " << open << "\n";
                if (!c.ns.empty()) out << "namespace " << c.ns << " {\n";
                open = c.ns;
            }
            out << macro << "(" << c.name << ");\n";
        }
        if (!open.empty()) out << "}  // namespace " << open << "\n";
    };
    registerMacros("REGISTER_RUNTIME_COMPONENT", true);
    // REGISTER_COMPONENT hits ComponentRegistry, which only exists in editor builds
    // (its header is included under DEKI_EDITOR above), so guard it the same way.
    out << "#ifdef DEKI_EDITOR\n";
    registerMacros("REGISTER_COMPONENT", false);
    out << "#endif\n";
    out << "\n"
        << SEP << "\n// ESP32/Embedded: Explicit registration function\n"
        << SEP << "\n";
    out << "// Static initializers are unreliable on ESP-IDF, so we register explicitly.\n\n";
    out << "#ifndef DEKI_EDITOR\n";
    out << "extern \"C\" void deki_register_core_components(void)\n{\n";
    out << "    static bool registered = false;\n    if (registered) return;\n    registered = true;\n";
    out << "    auto& factory = ::Deki::ComponentFactory::Instance();\n";
    for (auto& n : concrete)
        out << "    factory.Register(" << n << "::StaticType, []() -> ::Deki::Component* { return new " << n << "(); });\n";
    out << "\n    auto& registry = ::Deki::ComponentRegistry::Instance();\n";
    for (auto& n : concrete)
        out << "    registry.RegisterFields(" << n << "::StaticType, " << n << "::GetFields(), " << n << "::GetFieldCount());\n";
    out << "}\n#endif\n";
}

// AllComponentRegistration.gen.cpp: one deki_register_all_components() the engine calls.
// In-tree there are no component packages, so it registers the core concrete components only.
void emit_all_component_registration(std::ostream& out, std::vector<CompInfo> comps)
{
    std::vector<CompInfo> byInc = comps;
    std::sort(byInc.begin(), byInc.end(), by_inc);
    std::vector<CompInfo> byNm = comps;
    std::sort(byNm.begin(), byNm.end(), by_name);
    std::vector<std::string> concrete, incs;
    for (auto& c : byNm)
        if (!c.abstract) concrete.push_back(qualified_name(c));
    for (auto& c : byInc)
        if (!c.abstract) incs.push_back(c.inc);
    out << "// AUTO-GENERATED by the Deki reflection codegen - DO NOT EDIT\n";
    out << "// Single registration function for ALL " << concrete.size() << " concrete components.\n";
    out << "// DekiEngine calls this ONE function — it knows nothing about individual packages.\n\n\n";
    out << "#include <deki/reflection/ComponentFactory.h>\n";
    out << "#include <deki/reflection/ComponentRegistry.h>\n";
    for (auto& i : incs)
        out << "#include \"" << i << "\"\n";
    out << "\nextern \"C\" void deki_register_all_components(void)\n{\n";
    out << "    static bool registered = false;\n    if (registered) return;\n    registered = true;\n";
    out << "    auto& factory = ::Deki::ComponentFactory::Instance();\n";
    out << "#ifdef DEKI_EDITOR\n    // Editor: Register core components only (packages load separately via DLLs)\n";
    for (auto& n : concrete)
        out << "    factory.Register(" << n << "::StaticType, []() -> ::Deki::Component* { return new " << n << "(); });\n";
    out << "#else\n    // Runtime: Use feature guards for selective compilation\n";
    for (auto& n : concrete)
        out << "    factory.Register(" << n << "::StaticType, []() -> ::Deki::Component* { return new " << n << "(); });\n";
    out << "#endif\n";
    out << "\n    // Compact field tables — every platform (see ::Deki::FieldRef).\n";
    out << "    auto& registry = ::Deki::ComponentRegistry::Instance();\n";
    for (auto& n : concrete)
        out << "    registry.RegisterFields(" << n << "::StaticType, " << n << "::GetFields(), " << n << "::GetFieldCount());\n";
    out << "}\n";
}

int main()
{
    int nc = 0, ns = 0, nn = 0;
    // Per-package component list. Collected by EVERY shard (it is cheap: name, abstract
    // flag and include path, no property emission) because the registration file needs
    // the package's complete list and is written by shard 0.
    std::vector<std::vector<CompInfo>> comps(kPackageCount);
    auto out_path = [](int mi, const char* nm, const char* ext)
    {
        return std::string(kPackages[mi].outdir) + "/" + nm + ext;
    };

    // One MERGED .gen.cpp per package instead of one per type. The .gen.h stubs stay
    // per type because hand-written headers include them BY NAME; nothing includes a
    // .gen.cpp, so their names are ours to choose — and choosing a FIXED one is what
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
    { std::string s = buf.str(); buf.str({}); buf.clear(); return s; };
    // Components: .gen.cpp (reflection metadata + deserialize) and .gen.h (stub)
    template for (constexpr auto comp : std::define_static_array(reflected_scope_members()))
    {
        if constexpr (m::is_type(comp) && is_component(comp) && package_index_of(comp) != -1)
        {
            constexpr int mi = package_index_of(comp);
            static_assert(mi >= 0, "reflection codegen: source path matches two package tags in this work unit");
            const char* nm = std::define_static_string(m::identifier_of(comp));
            comps[mi].push_back({ std::string(nm), m::is_abstract_type(comp),
                                  src_rel_include(m::source_location_of(comp).file_name(), kPackages[mi].srcdir),
                                  std::string(namespace_of(comp)) });
            if constexpr (in_shard(m::identifier_of(comp)))
            {
                emit_component<comp>(buf, kPackages[mi].srcdir);
                mergedCpp[mi] += take();
                emit_component_header<comp>(buf, kPackages[mi].srcdir);
                write_if_changed(out_path(mi, nm, ".gen.h"), take());
                ++nc;
            }
        }
    }
    // DEKI_SERIALIZABLE structs: .gen.cpp (Serialize/Deserialize) and .gen.h (stub)
    template for (constexpr auto s : std::define_static_array(reflected_scope_members()))
    {
        if constexpr (m::is_type(s) && m::is_class_type(s) && m::is_complete_type(s) && is_serializable(s) && package_index_of(s) != -1 && in_shard(m::identifier_of(s)))
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
    template for (constexpr auto n : std::define_static_array(reflected_scope_members()))
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
        {
            return fn.size() >= s.size() && fn.compare(fn.size() - s.size(), s.size(), s) == 0;
        };
        if (ends_with("Registration.gen.cpp")) return true;  // {Prefix}/Core/AllComponent
        if (fn == "AllPackageInit.gen.cpp") return true;
        // {Prefix}Reflection<shard>.gen.cpp, any shard (another shard owns the others).
        const std::string tail = ".gen.cpp";
        if (!ends_with(tail)) return false;
        std::string stem = fn.substr(0, fn.size() - tail.size());
        size_t digits = 0;
        while (digits < stem.size() && std::isdigit(static_cast<unsigned char>(stem[stem.size() - 1 - digits])))
            ++digits;
        if (digits == 0) return false;
        stem.resize(stem.size() - digits);
        return stem.size() >= 10 && stem.compare(stem.size() - 10, 10, "Reflection") == 0;
    };
    for (int i = 0; i < kPackageCount; ++i)
    {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(kPackages[i].outdir, ec))
        {
            if (ec) break;
            if (!e.is_regular_file()) continue;
            const std::string fn = e.path().filename().string();
            if (fn.size() < 9 || fn.compare(fn.size() - 8, 8, ".gen.cpp") != 0) continue;
            if (is_generator_output(fn)) continue;
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
            head << "// (no reflected types in this work unit)\n";
        write_if_changed(std::string(kPackages[i].outdir) + "/" + kPackages[i].prefix +
                             "Reflection" + std::to_string(DEKI_CODEGEN_SHARD_INDEX) + ".gen.cpp",
                         head.str() + mergedCpp[i]);
    }

    // Registration: CoreRegistration (engine-core) or {Prefix}Registration (package DLL).
    // Shard 0 owns it — every shard has the full component list, but only one may write.
    if (DEKI_CODEGEN_SHARD_INDEX == 0)
    {
        for (int i = 0; i < kPackageCount; ++i)
        {
            const std::string dir = kPackages[i].outdir;
#if defined(DEKI_NO_REGISTRATION)
            (void)dir;  // plugin build: components only (registration is via the editor's PluginExports.cpp)
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
              << " (shard " << DEKI_CODEGEN_SHARD_INDEX << "/" << DEKI_CODEGEN_SHARD_COUNT
              << ", " << kPackageCount << " package(s))\n";
    return 0;
}
