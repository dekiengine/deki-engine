#pragma once

#include <stdint.h>

// A pixel rectangle, half-open: [left, right) x [top, bottom).
//
// The render system uses it for the pixels a frame changed
// (IRenderSystem::GetPresentRects), and displays for the pixels to push
// (IDisplay::PresentRegions).

namespace Deki
{

struct Rect
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;

    int32_t Width() const { return right - left; }
    int32_t Height() const { return bottom - top; }
    bool Empty() const { return right <= left || bottom <= top; }
};

}  // namespace Deki
