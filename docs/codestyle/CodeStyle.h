// The Deki C++ code style, shown by example.
//
// Each rule sits next to code that follows it. CodeStyle.cpp holds the rest.
// .clang-format checks the layout and .clang-tidy checks the names; both leave
// these two files unchanged. If you change a rule, change these files with it.
//
// Comments, here and in all code:
// - Say what the code does, and why when that is not obvious.
// - Use short sentences and common words.
// - Say what something does, not what it does not do. Mention what is left out
//   only when a reader would expect it and be surprised.
// - Do not repeat the next line in words.
//
// Commits and pull requests are described in README.md, next to this file.

// Headers start with #pragma once, never with an #ifndef guard.
#pragma once

// Includes, in this order: the file's own header (in a .cpp), Deki headers,
// third-party headers, then the standard library. Use <> for headers found
// through an include path and "" for headers next to this file.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Namespaces are PascalCase. Their contents are not indented.
namespace DekiStyleExample
{

// Constants are k + PascalCase.
constexpr int kMaxSlots = 8;

// Macros are DEKI_ + ALL_CAPS. Use a constant or a function instead whenever
// one can do the job.
#define DEKI_STYLE_EXAMPLE_VERSION 1

// Enums are enum class. The type and its values are PascalCase.
enum class SlotState : uint8_t
{
    Empty,
    Loading,
    Ready,
};

// An interface (a class with only pure virtual functions) is I + PascalCase.
class ISlotSource
{
public:
    virtual ~ISlotSource() = default;

    // Public functions get a /// comment: what the function does and what it
    // returns, in plain sentences. No Doxygen tags.
    /// Reads slot `index` into `out`. Returns false when there is no such slot.
    virtual bool Read(int index, std::string& out) = 0;
};

// Plain data is a struct with public fields. Fields are camelCase with no
// prefix, and each one has a default value.
struct SlotInfo
{
    std::string name;
    int32_t sizeBytes = 0;
    SlotState state = SlotState::Empty;
};

// Classes and structs are PascalCase. The public section comes first, then
// protected, then private.
class SlotTable
{
public:
    // Type aliases use `using`, never typedef.
    using List = std::vector<SlotInfo>;

    // A constructor that takes one argument is explicit.
    explicit SlotTable(ISlotSource& source);

    /// Reads every slot from the source. Returns how many were kept.
    int LoadAll();

    // A short function may be written on one line in the class.
    int Count() const { return static_cast<int>(m_Slots.size()); }
    bool IsLoaded() const { return m_Loaded; }

    /// The slot named `name`, or nullptr when there is none.
    const SlotInfo* Find(const std::string& name) const;

    // Static data members are s_ + PascalCase.
    static int s_TablesCreated;

private:
    // Private and protected data members are m_ + PascalCase.
    ISlotSource& m_Source;
    List m_Slots;
    bool m_Loaded = false;
};

// A class that overrides a virtual function says `override`, and nothing more
// (no `virtual` in front).
class FixedSlotSource : public ISlotSource
{
public:
    // A class that owns memory says so with std::unique_ptr or a container.
    // Raw pointers and references never own what they point to.
    explicit FixedSlotSource(std::vector<std::string> names);

    bool Read(int index, std::string& out) override;

private:
    std::vector<std::string> m_Names;
};

// Free functions are PascalCase, like member functions. Parameters and local
// variables are camelCase.
const char* SlotStateName(SlotState state);

// Template parameters are PascalCase. A single type parameter may be T.
template <typename T>
T ClampToSlots(T value)
{
    if (value < T{ 0 })
    {
        return T{ 0 };
    }
    if (value > T{ kMaxSlots })
    {
        return T{ kMaxSlots };
    }
    return value;
}

// Pointers and references attach to the type: `const SlotInfo* slot`,
// `const std::string& name`. `const` goes before the type.
std::unique_ptr<SlotTable> MakeLoadedTable(ISlotSource& source, const std::string& requiredName, int minimumCount);

}  // namespace DekiStyleExample
