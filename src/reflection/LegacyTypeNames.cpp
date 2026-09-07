#include "reflection/LegacyTypeNames.h"

#include <deki/TypeId.h>

#ifdef DEKI_EDITOR

namespace Deki
{

namespace
{

struct LegacyEntry
{
    const char* guid;
    const char* name;
};

// Every GUID a dekiengine package or the engine declared in 0.10, in GUID order.
// A project's own components resolve through the "class" hint in its scene
// files instead, so they need no entry here.
const LegacyEntry kLegacyTypes[] = {
    { "05f70a7a-334f-492f-9779-f3e351f64d9a", "AnimationComponent" },
    { "063a42d4-bfe1-49b9-b620-0cbf87ad720f", "ClipComponent" },
    { "0c34e973-6cd1-45d2-b2c6-f010ba320707", "ButtonStyleComponent" },
    { "146999a7-398f-4e52-a7c7-1e6a78bfb9c4", "CameraComponent" },
    { "1c8e7f24-3a9b-4d52-8e6c-f1a40b2d9e83", "Deki::DisplayProjectSettings" },
    { "2b4f8e91-7d36-4a50-9c18-4f6b2d8e1a35", "LSM6DS3IMUComponent" },
    { "2c7d4e83-1f5a-4b29-9e6d-3c8b0f1a4d72", "SystemClockRTCComponent" },
    { "2d1b84cb-8881-4457-8361-98dec7a84498", "ScrollComponent" },
    { "3836a84e-5cec-4849-bcc7-b524ad0baa5d", "SDCardComponent" },
    { "3e9b172c-4a5d-4c08-b5e7-91f3a0c2d74b", "DS3231RTCComponent" },
    { "3eaf9b46-5c1d-4f74-af8e-132d6f4abca5", "Deki::UnitProjectSettings" },
    { "3f8a61c9-7b2e-4d5a-9c14-8e6f2a0b5d73", "FsmComponent" },
    { "4d8c9f12-2a7b-4e35-a869-5b1f3e7c4d82", "MAX98357AudioComponent" },
    { "5447ea24-d11f-4161-ae10-2f11e0a18d09", "TextComponent" },
    { "5b1d8f4e-3a02-4c7d-9f56-1e8a2b9c3d40", "I2CBusComponent" },
    { "5c1f9a47-8e2b-4d73-b6a0-9d3c47f1e820", "CurlHttpClientComponent" },
    { "5e2a9d61-4f8c-4b73-a902-1d6b7e4c8a3f", "DesktopGPSComponent" },
    { "6b2f4d17-8c93-4a5e-b0d6-27e91fa3c845", "Deki::CoreProjectSettings" },
    { "6f3c2a51-8d4e-4b0a-9c1f-2e7a5b8d9c01", "Walker" },
    { "72e6d6c3-ec5d-4c8f-8411-4d31824b944e", "RollerComponent" },
    { "7c3a5e12-8b49-4f26-a735-d089e4b1c6f2", "NEO6MGPSComponent" },
    { "7c9a1d20-3e44-4b8a-9f12-6d2c5e3a8b71", "TilemapComponent" },
    { "8b3a7f12-4d6e-4c95-a814-2f9e0d5b8c3a", "WinHttpClientComponent" },
    { "8d743475-7905-4cca-b12c-9c397162851d", "SortingGroupComponent" },
    { "8d84cca3-7eb9-4b92-ba49-968ceec203c8", "GradientComponent" },
    { "91ce8c51-680b-4ff7-aa74-b44026097d74", "Bobber" },
    { "9604fa26-8be9-428a-9c29-e67c2d52c913", "RendererComponent" },
    { "9c79bf8d-6ba3-48b8-80da-1e775d818ad3", "LGFXTouchPanel" },
    { "9e113121-59df-4786-9752-0099935f1378", "TweenComponent" },
    { "a1b2c3d4-e5f6-7890-abcd-ef1234567890", "InputCollider" },
    { "a4f2b6c8-5d10-4e93-bc77-2f8a9d3c4e15", "TilemapObjectSpawner" },
    { "a7e1d3f0-8b4c-4e2a-9f61-3c8d2b5a7e90", "LGFXDisplayPanel" },
    { "b1e0e1a0-1111-4002-9002-000000000010", "ParticleEmitterComponent" },
    { "c3a8f1d0-5b7e-4a2c-8d91-6e3f4c7b2a50", "SDL3DisplaySetup" },
    { "c9d4eaf6-5081-4c3d-bf27-a04f9e6d8c51", "ESP32SerialSetup" },
    { "ca17ba76-f46d-484d-97ff-6e8e5497ef8d", "SpriteComponent" },
    { "d4b9e2f1-6c8a-4d3b-9e72-7f4g5d8c3b61", "SDL3InputSetup" },
    { "dcc9b892-ec9b-4eae-90d5-561b5591860a", "ScrollElement" },
    { "e1b3c909-aaa1-49f4-8d62-04145f79f2a0", "Deki::PlatformSetupComponent" },
    { "e2b8c4a1-9f30-4d65-8c11-7a4b3e2d1f88", "TilemapColliderComponent" },
    { "e335b7e0-e04f-4921-b3b0-b88faef7f519", "Spinner" },
    { "e6bb6f32-be31-4af1-a152-32cd21f490f3", "ButtonComponent" },
    { "f2c936db-c42a-47f3-bf97-931cab7e44df", "Deki::SetupComponent" },
    { "f8a3c891-9b4d-4e2a-9f81-3c5b2d8e4a17", "RenderingProjectSettings" },
};

}  // namespace

bool LooksLikeComponentGuid(const std::string& s)
{
    return s.size() == 36 && s[8] == '-' && s[13] == '-' && s[18] == '-' && s[23] == '-';
}

const char* LegacyComponentTypeName(const std::string& guid)
{
    if (!LooksLikeComponentGuid(guid)) return nullptr;
    for (const LegacyEntry& e : kLegacyTypes)
        if (guid == e.guid) return e.name;
    return nullptr;
}

uint32_t LegacyComponentTypeId(uint32_t typeId)
{
    if (typeId == 0) return 0;
    for (const LegacyEntry& e : kLegacyTypes)
        if (HashString(e.guid) == typeId) return HashString(e.name);
    return typeId;
}

}  // namespace Deki

#endif  // DEKI_EDITOR
