#ifndef INC_VOLTAGE_DETECT_H
#define INC_VOLTAGE_DETECT_H

#include "esp_event.h"

void voltage_detect_init(void);

// Declare an event base
ESP_EVENT_DECLARE_BASE(VOLTAGE_EVENTS);        // declaration of the timer events family

enum {                                       // declaration of the specific events under the timer event family
    VOLTAGE_EVENT_5V_ON,                     // raised when the timer is first started
    VOLTAGE_EVENT_5V_OFF,                      // raised when a period of the timer has elapsed
};

#endif /* INC_VOLTAGE_DETECT_H */
