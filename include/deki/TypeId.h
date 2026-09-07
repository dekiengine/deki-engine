#pragma once

/**
 * @file TypeId.h
 * @brief Compile-time component identity derived from the class name.
 *
 * A component's identity is its fully qualified C++ name ("Game::Health",
 * "Deki::SetupComponent"). Nothing has to be declared for it: the compiler
 * spells the name out in the signature of a function template, and that text
 * is hashed at compile time. Scene files store the same name as text, so the
 * on-disk key, the runtime id and the reflection codegen's view of a class all
 * agree by construction.
 *
 * The consequence to know about: renaming a class (or moving it to another
 * namespace) changes its id. Scenes that reference the old name load it as a
 * MissingComponent until the editor's redirect for that rename is in place.
 */

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Deki
{

using ComponentType = uint32_t;

/**
 * @brief FNV-1a of a C string; the hash behind every type and property id.
 */
constexpr uint32_t HashString(const char* str, uint32_t hash = 2166136261u)
{
    return (*str == 0) ? hash : HashString(str + 1, (hash ^ static_cast<uint32_t>(*str)) * 16777619u);
}

/**
 * @brief Same hash over a length-delimited string (msgpack keys are not null-terminated).
 */
inline uint32_t HashStringLen(const char* str, uint16_t length)
{
    uint32_t hash = 2166136261u;
    for (uint16_t i = 0; i < length; i++)
    {
        hash = (hash ^ static_cast<uint32_t>(str[i])) * 16777619u;
    }
    return hash;
}

/**
 * @brief Same hash over a string_view, usable in constant expressions.
 */
constexpr uint32_t HashStringView(std::string_view s)
{
    uint32_t hash = 2166136261u;
    for (char c : s)
    {
        hash = (hash ^ static_cast<uint32_t>(c)) * 16777619u;
    }
    return hash;
}

namespace detail
{

template <class T>
constexpr std::string_view RawTypeSignature()
{
#if defined(__clang__) || defined(__GNUC__)
    return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
    return __FUNCSIG__;
#else
#error "Deki::TypeNameOf needs a compiler that exposes the enclosing function signature"
#endif
}

// Where the type name sits inside the signature, measured on a type that every
// compiler spells the same way. The prefix and suffix are the same for any T.
struct TypeNameProbe
{
    static constexpr std::string_view probe = RawTypeSignature<double>();
    static constexpr size_t prefix = probe.find("double");
    static constexpr size_t suffix = probe.size() - prefix - 6;
};

constexpr bool StartsWith(std::string_view s, std::string_view p)
{
    return s.size() >= p.size() && s.substr(0, p.size()) == p;
}

}  // namespace detail

/**
 * @brief The fully qualified name of T, e.g. "Game::Health", as a string_view.
 */
template <class T>
constexpr std::string_view TypeNameOf()
{
    static_assert(std::is_class<T>::value, "Deki::TypeNameOf: only class types have a component identity");
    std::string_view s = detail::RawTypeSignature<T>();
    s.remove_prefix(detail::TypeNameProbe::prefix);
    s.remove_suffix(detail::TypeNameProbe::suffix);
    // MSVC spells class types with their class-key.
    if (detail::StartsWith(s, "class "))
        s.remove_prefix(6);
    else if (detail::StartsWith(s, "struct "))
        s.remove_prefix(7);
    return s;
}

/**
 * @brief The identifier alone, e.g. "Health" for Game::Health.
 */
template <class T>
constexpr std::string_view TypeShortNameOf()
{
    std::string_view s = TypeNameOf<T>();
    const size_t sep = s.rfind("::");
    return sep == std::string_view::npos ? s : s.substr(sep + 2);
}

/**
 * @brief The component type id of T: HashString of its fully qualified name.
 *
 * Every lookup by type (GetComponent<T>, the factory, the field tables) keys
 * on this value, and a scene file's "type" string hashes to it on load.
 */
template <class T>
constexpr ComponentType TypeId()
{
    return HashStringView(TypeNameOf<T>());
}

namespace detail
{

template <class T, class Seq>
struct TypeNameStorage;
template <class T, size_t... I>
struct TypeNameStorage<T, std::index_sequence<I...>>
{
    static constexpr char value[sizeof...(I) + 1] = { TypeNameOf<T>()[I]..., '\0' };
};

template <class T, class Seq>
struct TypeShortNameStorage;
template <class T, size_t... I>
struct TypeShortNameStorage<T, std::index_sequence<I...>>
{
    static constexpr char value[sizeof...(I) + 1] = { TypeShortNameOf<T>()[I]..., '\0' };
};

}  // namespace detail

/**
 * @brief The fully qualified name of T as a null-terminated string with static storage.
 */
template <class T>
constexpr const char* TypeName()
{
    return detail::TypeNameStorage<T, std::make_index_sequence<TypeNameOf<T>().size()>>::value;
}

/**
 * @brief The identifier of T as a null-terminated string with static storage.
 */
template <class T>
constexpr const char* TypeShortName()
{
    return detail::TypeShortNameStorage<T, std::make_index_sequence<TypeShortNameOf<T>().size()>>::value;
}

}  // namespace Deki
