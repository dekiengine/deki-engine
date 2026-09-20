#pragma once

#include <cstdint>
#include <deki/Vector.h>  // for Mat4 (passive forward-compat type)

/**
 * @brief Camera projection mode (forward-compat hook).
 *
 * Orthographic is the only path used by the 2D software renderer today;
 * Perspective is reserved for future 3D / external renderers. Standard2DRenderer
 * does NOT consume the projection matrix - it calls ICamera::WorldToScreen
 * directly. The enum + GetProjectionMatrix() exist only so that future 3D
 * code, debug tools, or third-party renderers can ask the camera for a matrix
 * without us having to retrofit the API later.
 */

namespace Deki
{

enum class ProjectionMode : uint8_t
{
    Orthographic = 0,
    Perspective = 1,
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
 *     screen = (world * pixelsPerMeter) + buffer_center
 *   The camera's pixelsPerMeter directly drives screen scale. Setting it
 *   equal to the project's pixelsPerMeter yields 1:1 visualization; larger
 *   values zoom in, smaller zoom out. The optional pixel-snap step lives
 *   in the renderer, not in the camera.
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

    // Framebuffer pixels per world meter. The single knob controlling
    // world→screen scale. Implementations may resolve a sentinel (e.g. 0)
    // to the project's default at runtime.
    virtual float GetPixelsPerMeter() const = 0;
    virtual void SetPixelsPerMeter(float pixelsPerMeter) = 0;

    // Projection mode (forward-compat hook). Standard2DRenderer ignores this
    // and uses WorldToScreen directly; future 3D / external renderers can
    // request a matrix.
    virtual ProjectionMode GetProjectionMode() const { return ProjectionMode::Orthographic; }
    virtual void SetProjectionMode(ProjectionMode /*mode*/) { /* default no-op */ }

    /// Build a projection matrix for the requested logical-buffer size.
    /// Default returns an orthographic matrix matching the camera's
    /// pixelsPerMeter. Perspective is the camera implementor's job (returns
    /// identity by default). NOT consumed by Standard2DRenderer.
    virtual Mat4 GetProjectionMatrix(int bufferWidth, int bufferHeight) const
    {
        const float effective = GetPixelsPerMeter();
        if (effective <= 0.0f || bufferWidth <= 0 || bufferHeight <= 0)
            return Mat4::Identity();
        const float halfW = (static_cast<float>(bufferWidth) * 0.5f) / effective;
        const float halfH = (static_cast<float>(bufferHeight) * 0.5f) / effective;
        return Mat4::Ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
    }

    virtual ~ICamera() = default;
};

}  // namespace Deki
