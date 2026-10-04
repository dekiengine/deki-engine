#pragma once

#include <cstdint>
#include <deki/Vector.h>  // for Mat4

namespace Deki
{

/// How a camera projects the world.
///
/// Orthographic shows a fixed height of world (the camera's ortho height);
/// the width follows the screen's shape. Perspective shows a vertical field
/// of view, which 3D passes (deki-3d) draw through. Sprites draw flat through
/// the orthographic mapping in both modes.
enum class ProjectionMode : uint8_t
{
    Orthographic = 0,
    Perspective = 1,
};

/// Units the inspector shows. Display only: storage, serialization, runtime
/// math and physics are always in meters. In Pixels mode the inspector
/// multiplies physical-unit fields (DEKI_UNIT(Distance|Velocity|Acceleration))
/// by the project's pixelsPerMeter.
enum class UnitMode : uint8_t
{
    Pixels = 0,  // Inspector shows m * pixelsPerMeter with px / px/s / px/s² suffix
    Meters = 1,  // Inspector shows raw meters with m / m/s / m/s² suffix
};

/// Temperature scale the inspector shows. Storage is Celsius. Separate from
/// UnitMode because the two preferences are independent.
enum class TemperatureUnit : uint8_t
{
    Celsius = 0,
    Fahrenheit = 1,
};

/// Camera interface for coordinate conversion.
///
/// Packages implement it on their camera components. The editor finds it
/// with Object::FindInterface<ICamera>() without knowing the concrete type.
///
/// Coordinate model:
///   screen pixels (float) = (world - camera) * pixelsPerMeter + buffer_center
/// with world in meters. pixelsPerMeter is the buffer's height over the
/// camera's ortho height (Deki::OrthoPixelsPerMeter, ScreenScale.h), so a
/// camera shows the same height of world on every screen whatever its
/// resolution; a wider screen shows more at the sides.
class ICamera
{
public:
    static constexpr uint32_t kInterfaceID = 0x43414D00;  // "CAM\0"

    // World <-> screen conversion. World is in meters. Both sides are float to
    // keep sub-pixel precision; renderers decide whether to snap to whole
    // pixels when they draw.
    virtual void ScreenToWorld(float screenX, float screenY, int bufferWidth, int bufferHeight, float& worldX,
                               float& worldY) const = 0;

    virtual void WorldToScreen(float worldX, float worldY, int bufferWidth, int bufferHeight, float& screenX,
                               float& screenY) const = 0;

    virtual void GetClearColor(uint8_t& r, uint8_t& g, uint8_t& b) const = 0;
    virtual void SetClearColor(uint8_t r, uint8_t g, uint8_t b) = 0;

    // Framebuffer pixels per world meter for a buffer of this size: the
    // buffer's height over the ortho height (or the fixed value set below).
    virtual float GetPixelsPerMeter(int bufferWidth, int bufferHeight) const = 0;

    // Orthographic: how much world the camera shows top to bottom, in meters.
    virtual float GetOrthoHeight() const = 0;
    virtual void SetOrthoHeight(float meters) = 0;

    // A fixed pixels-per-meter that replaces the ortho height; 0 clears it.
    // For views that are not a screen, such as the editor's scene view, which
    // shows the world at the project's own pixels per meter at any panel size.
    virtual void SetFixedPixelsPerMeter(float pixelsPerMeter) = 0;

    virtual ProjectionMode GetProjectionMode() const = 0;
    virtual void SetProjectionMode(ProjectionMode mode) = 0;

    // Perspective: vertical field of view in degrees, and the clip planes in
    // meters. A wider screen sees more at the sides.
    virtual float GetFieldOfView() const = 0;
    virtual float GetNearPlane() const = 0;
    virtual float GetFarPlane() const = 0;

    /// Projection matrix for a buffer of this size, orthographic or
    /// perspective per GetProjectionMode(). Standard2DRenderer maps through
    /// WorldToScreen instead.
    virtual Mat4 GetProjectionMatrix(int bufferWidth, int bufferHeight) const = 0;

    virtual ~ICamera() = default;
};

}  // namespace Deki
