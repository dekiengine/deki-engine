// The file's own header comes first, so it is checked to compile on its own.
#include "CodeStyle.h"

#include <algorithm>
#include <utility>

namespace DekiStyleExample
{

int SlotTable::s_TablesCreated = 0;

// A variable used only in this file is static and s_ + PascalCase. A global
// that other files can see is g_ + PascalCase; avoid them.
static int s_ReadFailures = 0;

// Functions used only in this file go in an unnamed namespace.
namespace
{

bool IsUsable(const SlotInfo& slot)
{
    // Every if, for and while body has braces, even a single statement.
    if (slot.name.empty())
    {
        return false;
    }
    return slot.sizeBytes > 0;
}

}  // namespace

// Braces go on their own line. Indentation is 4 spaces, never tabs.
SlotTable::SlotTable(ISlotSource& source)
    : m_Source(source)
{
    ++s_TablesCreated;
}

int SlotTable::LoadAll()
{
    m_Slots.clear();
    for (int index = 0; index < kMaxSlots; ++index)
    {
        std::string text;
        if (!m_Source.Read(index, text))
        {
            ++s_ReadFailures;
            continue;
        }

        SlotInfo slot;
        slot.name = text;
        // Casts are C++ casts (static_cast and the others), never (int32_t)x.
        slot.sizeBytes = static_cast<int32_t>(text.size());
        slot.state = SlotState::Ready;
        if (IsUsable(slot))
        {
            m_Slots.push_back(std::move(slot));
        }
    }
    m_Loaded = true;
    return Count();
}

const SlotInfo* SlotTable::Find(const std::string& name) const
{
    // Use auto when the type is plain from the same line: iterators, lambdas,
    // casts, make_unique. Write the type out when it matters which one it is.
    const auto it =
        std::find_if(m_Slots.begin(), m_Slots.end(), [&name](const SlotInfo& slot) { return slot.name == name; });

    // nullptr, never NULL or 0.
    return it != m_Slots.end() ? &*it : nullptr;
}

FixedSlotSource::FixedSlotSource(std::vector<std::string> names)
    : m_Names(std::move(names))
{
}

bool FixedSlotSource::Read(int index, std::string& out)
{
    if (index < 0 || index >= static_cast<int>(m_Names.size()))
    {
        return false;
    }
    out = m_Names[static_cast<size_t>(index)];
    return true;
}

const char* SlotStateName(SlotState state)
{
    // A case that only returns may stay on one line.
    switch (state)
    {
        case SlotState::Empty: return "Empty";
        case SlotState::Loading: return "Loading";
        case SlotState::Ready: return "Ready";
    }
    return "Unknown";
}

std::unique_ptr<SlotTable> MakeLoadedTable(ISlotSource& source, const std::string& requiredName, int minimumCount)
{
    auto table = std::make_unique<SlotTable>(source);
    const int loaded = table->LoadAll();

    // Lines are at most 120 characters. clang-format decides where a longer
    // line wraps; do not wrap by hand to get another layout.
    if (loaded < ClampToSlots(minimumCount) || (!requiredName.empty() && table->Find(requiredName) == nullptr) ||
        !table->IsLoaded())
    {
        return nullptr;
    }
    return table;
}

}  // namespace DekiStyleExample
