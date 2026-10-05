#pragma once

#include <cstdint>

#include <deki/Component.h>
#include <deki/reflection/Property.h>

namespace Deki
{

class ICamera;
class Scene;

/// A point on the screen: a corner, the middle of an edge, or the centre.
enum class ScreenPoint : uint8_t
{
    TopLeft,
    Top,
    TopRight,
    Left,
    Center,
    Right,
    BottomLeft,
    Bottom,
    BottomRight
};

/// Keeps its object at a point on the screen, such as the top-left corner, on
/// every screen size and while the camera moves. For a HUD: hearts at Top
/// Left, a score at Top Right.
///
/// The object sits at the anchor plus the offset, in meters, with Y up. Its
/// position in the Transform is set from these, so it is not edited there;
/// rotation and scale still come from the Transform, and children follow the
/// object as usual.
///
/// Placement runs once per frame, before drawing, from the scene's camera and
/// the screen size (PlaceAll). The object then has a normal world position,
/// so input and other scripts see it where it is drawn.
DEKI_CATEGORY("Core")
DEKI_DESCRIPTION("Keeps the object at a point on the screen, such as a corner, on every screen size.")
class ScreenAnchor : public Component
{
public:
    DEKI_TOOLTIP("The point on the screen the object stays at.")
    DEKI_EXPORT
    ScreenPoint anchor = ScreenPoint::TopLeft;

    DEKI_TOOLTIP("Distance from the anchor to the right. Negative goes left.")
    DEKI_UNIT(Distance)
    DEKI_EXPORT
    float offsetX = 0.0f;

    DEKI_TOOLTIP("Distance from the anchor upwards. Negative goes down.")
    DEKI_UNIT(Distance)
    DEKI_EXPORT
    float offsetY = 0.0f;

    ScreenAnchor();
    ~ScreenAnchor() override;

    /// Moves every anchored object of `scene`, and every persistent one, to
    /// its point on a screen `screenWidth` x `screenHeight` pixels seen
    /// through `camera`. The render system calls it before each frame; the
    /// editor calls it for the screen the Play view shows.
    static void PlaceAll(const Scene* scene, const ICamera& camera, int screenWidth, int screenHeight);

private:
    // Every live ScreenAnchor, linked through the instances themselves so
    // registering one allocates nothing.
    static ScreenAnchor* s_First;
    ScreenAnchor* m_Prev = nullptr;
    ScreenAnchor* m_Next = nullptr;
};

}  // namespace Deki
