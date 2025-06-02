#ifndef ACS712_H
#define ACS712_H

#include "esp_adc/adc_oneshot.h"

namespace acs712 {
    // Инициализация ADC для ACS712 (для измерения тока)
    void init();  
    
    // Чтение тока в амперах
    float readCurrent();  
    
    // Чтение мощности в ваттах (использует voltage, если инициализирован, иначе дефолтное напряжение 5V)
    float readPower();  
    
    // Получение handle ADC для других модулей
    adc_oneshot_unit_handle_t getAdcHandle();
}

#endif