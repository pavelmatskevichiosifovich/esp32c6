#include "l298n.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <inttypes.h>

namespace l298n {
    #define ENA_GPIO 8
    #define IN1_GPIO 0
    #define IN2_GPIO 1
    #define LEDC_TIMER LEDC_TIMER_0
    #define LEDC_CHANNEL LEDC_CHANNEL_0
    #define LEDC_FREQ 150 // PWM frequency, Hz
    #define LEDC_RESOLUTION LEDC_TIMER_13_BIT  // PWM resolution
    #define FIXED_DUTY 6191
    static const char* TAG = "l298n";

    // Добавляем флаг для контроля работы задачи
    static volatile bool cycleTaskRunning = false;
    static TaskHandle_t cycleTaskHandle = NULL;

    void init() {
        gpio_set_direction(static_cast<gpio_num_t>(IN1_GPIO), GPIO_MODE_OUTPUT);
        gpio_set_direction(static_cast<gpio_num_t>(IN2_GPIO), GPIO_MODE_OUTPUT);
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = static_cast<ledc_timer_bit_t>(LEDC_RESOLUTION),
            .timer_num = static_cast<ledc_timer_t>(LEDC_TIMER),
            .freq_hz = LEDC_FREQ,
            .clk_cfg = LEDC_AUTO_CLK,
            .deconfigure = false
        };
        ledc_timer_config(&ledc_timer);
        ledc_channel_config_t ledc_channel = {
            .gpio_num = static_cast<gpio_num_t>(ENA_GPIO),
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = static_cast<ledc_channel_t>(LEDC_CHANNEL),
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = static_cast<ledc_timer_t>(LEDC_TIMER),
            .duty = 0,
            .hpoint = 0,
            .sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE,
            .flags = {}
        };
        ledc_channel_config(&ledc_channel);
        ESP_LOGI(TAG, "L298N initialized successfully.");
    }

    void forward() {
        gpio_set_level(static_cast<gpio_num_t>(IN1_GPIO), 1);
        gpio_set_level(static_cast<gpio_num_t>(IN2_GPIO), 0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(LEDC_CHANNEL), FIXED_DUTY);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(LEDC_CHANNEL));
    }

    void backward() {
        gpio_set_level(static_cast<gpio_num_t>(IN1_GPIO), 0);
        gpio_set_level(static_cast<gpio_num_t>(IN2_GPIO), 1);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(LEDC_CHANNEL), FIXED_DUTY);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(LEDC_CHANNEL));
    }

    void stop() {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(LEDC_CHANNEL), 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(LEDC_CHANNEL));
        gpio_set_level(static_cast<gpio_num_t>(IN1_GPIO), 0);
        gpio_set_level(static_cast<gpio_num_t>(IN2_GPIO), 0);
    }

    static void runCycleTask(void *pvParameters) {
        cycleTaskRunning = true;
        while (cycleTaskRunning) {
            forward();
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            // backward();
            vTaskDelay(300 / portTICK_PERIOD_MS);
            
            // Проверяем флаг на каждой итерации
            if (!cycleTaskRunning) break;
        }
        
        // Останавливаем мотор при завершении задачи
        stop();
        
        // Удаляем задачу
        cycleTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    void startCycleTask() {
        if (cycleTaskHandle == NULL) {
            xTaskCreate(runCycleTask, "l298n_cycle_task", 2048, NULL, 5, &cycleTaskHandle);
            ESP_LOGI(TAG, "L298N cycle task started");
        } else {
            ESP_LOGW(TAG, "Cycle task is already running");
        }
    }

    void stopCycleTask() {
        if (cycleTaskHandle != NULL) {
            cycleTaskRunning = false;
            
            // Даем задаче время завершиться корректно
            int timeout = 10000;
            while (cycleTaskHandle != NULL && timeout-- > 0) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
            
            // Если задача не завершилась, принудительно удаляем
            if (cycleTaskHandle != NULL) {
                vTaskDelete(cycleTaskHandle);
                cycleTaskHandle = NULL;
                ESP_LOGW(TAG, "Cycle task force stopped");
            }
            
            stop(); // Дополнительная страховка - останавливаем мотор
            ESP_LOGI(TAG, "L298N cycle task stopped");
        } else {
            ESP_LOGW(TAG, "No active cycle task to stop");
        }
    }
}