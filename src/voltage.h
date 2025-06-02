#ifndef VOLTAGE_H
#define VOLTAGE_H

#include "esp_adc/adc_oneshot.h"

namespace voltage {
    void init();  // Без аргументов, по умолчанию с делителем
    
    float readVoltage(bool useDivider);  // Оставляем аргументы для выбора режима
    
    adc_oneshot_unit_handle_t getAdcHandle();
}

#endif