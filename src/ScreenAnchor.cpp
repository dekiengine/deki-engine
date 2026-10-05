#include <deki/ScreenAnchor.h>

#include <deki/ICamera.h>
#include <deki/Object.h>

namespace Deki
{

ScreenAnchor* ScreenAnchor::s_First = nullptr;

ScreenAnchor::ScreenAnchor()
{
    m_Next = s_First;
    if (s_First)
    {
        s_First->m_Prev = this;
    }
    s_First = this;
}

ScreenAnchor::~ScreenAnchor()
{
    if (m_Prev)
    {
        m_Prev->m_Next = m_Next;
    }
    else
    {
        s_First = m_Next;
    }
    if (m_Next)
    {
        m_Next->m_Prev = m_Prev;
    }
}

#ifdef DEKI_TRANSFORM_2D
namespace
{

// True when `anchor` is one of its owner's components. The editor detaches a
// component to undo adding it and keeps it for redo; a detached one places
// nothing.
bool IsAttached(const ScreenAnchor& anchor)
{
    const Object* owner = anchor.GetOwner();
    if (!owner)
    {
        return false;
    }
    for (const Component* component : owner->GetComponents())
    {
        if (component == &anchor)
        {
            return true;
        }
    }
    return false;
}

}  // namespace
#endif

void ScreenAnchor::PlaceAll(const Scene* scene, const ICamera& camera, int screenWidth, int screenHeight)
{
#ifdef DEKI_TRANSFORM_2D
    if (screenWidth <= 0 || screenHeight <= 0)
    {
        return;
    }

    // The screen's left, centre and right, and top, middle and bottom, in
    // world meters. They go through the camera's own conversion, so a point
    // lands on the same pixel the renderer would draw it at.
    float xs[3], ys[3];
    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);
    camera.ScreenToWorld(0.0f, 0.0f, screenWidth, screenHeight, xs[0], ys[0]);
    camera.ScreenToWorld(w * 0.5f, h * 0.5f, screenWidth, screenHeight, xs[1], ys[1]);
    camera.ScreenToWorld(w, h, screenWidth, screenHeight, xs[2], ys[2]);

    for (ScreenAnchor* anchor = s_First; anchor; anchor = anchor->m_Next)
    {
        Object* owner = anchor->GetOwner();
        if (!owner || (owner->GetOwnerScene() != scene && !owner->IsPersistent()) || !IsAttached(*anchor))
        {
            continue;
        }

        // ScreenPoint runs left to right, then top to bottom.
        const int point = static_cast<int>(anchor->anchor);
        if (point > static_cast<int>(ScreenPoint::BottomRight))
        {
            continue;
        }
        const float x = xs[point % 3] + anchor->offsetX;
        const float y = ys[point / 3] + anchor->offsetY;
        if (owner->GetWorldX() != x || owner->GetWorldY() != y)
        {
            owner->SetWorldPosition(x, y);
        }
    }
#else
    (void)scene;
    (void)camera;
    (void)screenWidth;
    (void)screenHeight;
#endif
}

}  // namespace Deki
