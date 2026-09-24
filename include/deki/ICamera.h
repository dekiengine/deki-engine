#pragma once

#include <cstdint>
#include <deki/Vector.h>  // for Mat4

/**
 * @brief How a camera projects the world.
 *
 * Orthographic shows the project's design area, scaled by the camera's zoom.
 * Perspective shows a field of view on the design shape; 3D passes (deki-3d)
 * draw through it. Sprites keep drawing flat through the orthographic mapping
 * under either mode until they can be placed in 3D.
 */

namespace Deki
{

enum class ProjectionMode : uint8_t
{
    Orthographic = 0,
    Perspective = 1,
};

/**
 * @brief What a screen whose shape differs from the design area shows.
 *
 * ShowAll keeps the whole design area visible and shows extra world along the
 * screen's longer side. Fill covers the whole screen and crops the design
 * area's edges instead. On a screen of the design shape the two are the same.
 */
enum class ScreenFit : uint8_t
{
    ShowAll = 0,
    Fill = 1,
};

/**
 * @brief Inspector display unit preference.
 *
 * This is purely a UI concern. Internal storage, serialization, runtime math
 * and physics are always in meters. The inspector multiplies stored values
 * by the project's pixelsPerMeter when displaying physical-unit fields
 * (DEKI_UNIT(Distance|Velocity|Acceleration)) under Pixels mode.
 */
enum class UnitMode : uint8_t
{
    Pixels = 0,  // Inspector shows m * pixelsPerMeter with px / px/s / px/s² suffix
    Meters = 1,  // Inspector shows raw meters with m / m/s / m/s² suffix
};

/**
 * @brief Which temperature scale the inspector shows. Storage is Celsius.
 *
 * Separate from UnitMode because the two preferences are independent: someone
 * laying out a scene in pixels may still want Fahrenheit, and a distance
 * preference has nothing to say about a thermometer.
 */
enum class TemperatureUnit : uint8_t
{
    Celsius = 0,
    Fahrenheit = 1,
};

/**
 * @brief Camera interface for coordinate conversion.
 *
 * Packages implement this interface on their camera components. The editor
 * queries it via Object::FindInterface<ICamera>() without knowing the
 * concrete camera type.
 *
 * Coordinate model:
 *   world (meters, float) → screen pixels (float) via:
 *     screen = (world - camera) * pixelsPerMeter + buffer_center
 *   pixelsPerMeter is resolved per target size: the project's design area is
 *   fitted to the buffer (Deki::ResolveScreenPixelsPerMeter, ScreenScale.h)
 *   and multiplied by the camera's zoom. The same camera therefore shows the
 *   same world on every screen, whatever its resolution.
 */
class ICamera
{
public:
    static constexpr uint32_t InterfaceID = 0x43414D00;  // "CAM\0"

    // World ↔ screen conversion. World is in meters; both sides are float
    // to preserve sub-pixel precision. Renderers decide whether to snap to
    // integer pixels at draw.
    virtual void ScreenToWorld(float screenX, float screenY, int bufferWidth, int bufferHeight,
                               float& worldX, float& worldY) const = 0;

    virtual void WorldToScreen(float worldX, float worldY, int bufferWidth, int bufferHeight,
                               float& screenX, float& screenY) const = 0;

    // Clear color (RGB)
    virtual void GetClearColor(uint8_t& r, uint8_t& g, uint8_t& b) const = 0;
    virtual void SetClearColor(uint8_t r, uint8_t g, uint8_t b) = 0;

    // Framebuffer pixels per world meter for a buffer of this size: the design
    // area fitted to the buffer, times zoom (or the fixed value set below).
    virtual float GetPixelsPerMeter(int bufferWidth, int bufferHeight) const = 0;

    // Orthographic zoom. 1 shows the project's design area; 2 is twice as close.
    virtual float GetZoom() const = 0;
    virtual void SetZoom(float zoom) = 0;

    // A fixed pixels-per-meter that replaces the design-area fit; 0 clears it.
    // For views that are not a screen, such as the editor's scene view, which
    // shows the world at the project's own pixels per meter at any panel size.
    virtual void SetFixedPixelsPerMeter(float pixelsPerMeter) = 0;

    virtual ProjectionMode GetProjectionMode() const = 0;
    virtual void SetProjectionMode(ProjectionMode mode) = 0;

    // Perspective: vertical field of view in degrees on the design shape, and
    // the clip planes in meters. The fit decides what other shapes see.
    virtual float GetFieldOfView() const = 0;
    virtual float GetNearPlane() const = 0;
    virtual float GetFarPlane() const = 0;

    /// Projection matrix for a buffer of this size, orthographic or
    /// perspective per GetProjectionMode(), with the screen fit applied.
    /// Standard2DRenderer does not use it; it maps through WorldToScreen.
    virtual Mat4 GetProjectionMatrix(int bufferWidth, int bufferHeight) const = 0;

    virtual ~ICamera() = default;
};

}  // namespace Deki
