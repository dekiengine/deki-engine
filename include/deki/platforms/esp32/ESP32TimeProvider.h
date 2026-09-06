#pragma once

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <deki/providers/ITimeProvider.h>

namespace Deki
{


class ESP32TimeProvider : public ITimeProvider
{
public:
    uint32_t GetTicksMs() const override
    {
        return (uint32_t)(esp_timer_get_time() / 1000);
    }

    void DelayMs(uint32_t ms) const override
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
};

}  // namespace Deki
