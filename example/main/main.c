/**
 * @file main.c
 * @brief Main application entry point and demo tasks for ESP32 system monitoring.
 *
 * This file implements the main application for an ESP32-based system monitoring
 * demonstration. It initializes WiFi connectivity, system monitoring services,
 * and creates several demo tasks to exercise CPU usage, stack monitoring, and
 * task lifecycle tracking capabilities.
 *
 * Responsibilities:
 *   - Initialize WiFi connection and network stack.
 *   - Initialize system monitoring component.
 *   - Create and manage demo tasks for system monitoring validation:
 *     - Sine wave CPU load generator (core 0)
 *     - Task lifecycle manager (creates/deletes cycle tasks)
 *     - RGB LED strip color cycling task
 *   - Register all tasks with sysmon for stack usage tracking.
 *
 * Dependencies:
 *   - ESP-IDF WiFi, networking, and FreeRTOS APIs.
 *   - sysmon component for system resource monitoring.
 *   - led_strip component for WS2812 RGB LED control (optional, remove if you want)
 *
 * Usage:
 *   - Configure WiFi credentials in wifi_credentials.h
 *   - Build and flash to ESP32 device.
 *   - Access sysmon web interface via assigned IP address.
 */

// Project-specific includes
#include "sysmon.h"
#include "sysmon_stack.h"

// ESP-IDF includes
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs_flash.h"

// System includes
#include <math.h>

#define LOG_TAG "main"

// Set your own wifi credentials by either editing this header file or defining inline:
// #define WIFI_SSID     "YOUR_WIFI_SSID"
// #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#include "wifi_credentials.h"

/*
 * NO-OP FUNCTIONS FOR WHEN SYSMON COMPONENT IS DISABLED
 *
 * If the sysmon component is removed from the build, copy/paste these functions
 * into this file (after removing the #include "sysmon.h" lines) to prevent
 * compilation errors from unresolved function calls.
 * 
 * To fully remove the component, manually remove all sysmon_* function calls from your app.
 *
 * void sysmon_stack_register(TaskHandle_t task_handle, uint32_t stack_size_bytes)
 * {
 *     ESP_LOGI(LOG_TAG, "sysmon_stack_register: no-op (sysmon component disabled)");
 *     (void)task_handle;
 *     (void)stack_size_bytes;
 * }
 *
 * esp_err_t sysmon_init(void)
 * {
 *     ESP_LOGI(LOG_TAG, "sysmon_init: no-op (sysmon component disabled)");
 *     return ESP_OK;
 * }
 */

// LED strip setup
#define RGB_LED_GPIO 21

#define RGB_LED_TASK_STACK_SIZE (3 * 1024)
#define RGB_LED_TASK_PRIORITY   5

#define DEMO_SINE_WAVE_TASK_STACK_SIZE (2.5 * 1024)
#define DEMO_SINE_WAVE_TASK_PRIORITY   6
#define DEMO_SINE_WAVE_TASK_CYCLE_TIME 17000UL
#define DEMO_SINE_WAVE_TASK_MIN_LOAD   0.10f
#define DEMO_SINE_WAVE_TASK_MAX_LOAD   0.80f
#define DEMO_SINE_WAVE_TASK_CORE       0

#define DEMO_CYCLE_TASK_STACK_SIZE    (4 * 1024)
#define DEMO_CYCLE_TASK_PRIORITY      6
#define DEMO_CYCLE_TASK_CYCLE_TIME_MS 7000
#define DEMO_CYCLE_TASK_CORE          1

#define DEMO_TASK_MANAGER_STACK_SIZE  (5 * 1024)
#define DEMO_TASK_MANAGER_PRIORITY    3

/**
 * @brief Initialize and connect to WiFi network.
 *
 * Initializes NVS flash storage, network interface, event loop, and WiFi in station mode.
 * Connects to the network specified via WIFI_SSID and WIFI_PASSWORD in wifi_credentials.h
 * and waits for an IP address assignment. Logs the assigned IP address upon successful connection.
 *
 * @return ESP_OK if WiFi connected and IP address obtained, ESP_ERR_TIMEOUT otherwise
 * @note This function blocks until WiFi connection is established or timeout occurs (15 seconds).
 * @note If NVS flash contains incompatible data, it will be erased and reinitialized.
 */
esp_err_t wifi_connect(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW("MAIN", "NVS incompatible, erasing flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(sta_netif ? ESP_OK : ESP_FAIL);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config =
    {
        .sta =
        {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI("MAIN", "Connecting to WiFi: %s", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Wait for IPv4 address
    esp_netif_ip_info_t ip_info = { 0 };
    for (int retries = 0; retries < 30; retries++)
    {
        if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0)
        {
            ESP_LOGI("MAIN", "WiFi connected, IP: %d.%d.%d.%d",
                     IP2STR(&ip_info.ip));
            break;
        }
        uint32_t wait_time_ms = (retries + 1) * 500;
        ESP_LOGI("MAIN", "Waiting for IP address... (%lu.%lus)", 
                 wait_time_ms / 1000, (wait_time_ms % 1000) / 100);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    if (ip_info.ip.addr == 0)
    {
        ESP_LOGW("MAIN", "WiFi connection timeout - no IP address assigned");
        ESP_LOGW("MAIN", "Attempted to connect with SSID: \"%s\" and password: \"%s\"",
            (const char*)wifi_config.sta.ssid,
            (const char*)wifi_config.sta.password);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/**
 * @brief Task to cycle the ESP32's onboard RGB LED through red, green, blue, and white colors.
 *
 * This FreeRTOS task initializes a WS2812 LED strip on the configured GPIO pin
 * and repeatedly cycles through red, green, blue, and white colors with a
 * 1-second delay between each color transition.
 *
 * @param param Unused task parameter.
 */
static void rgb_led_cycle_task(void *param)
{
    ESP_LOGI(LOG_TAG, "RGB LED cycle task started");
    
    led_strip_config_t strip_config =
    {
        .strip_gpio_num         = RGB_LED_GPIO,
        .max_leds               = 1,
        .led_model              = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGBW,
        .flags = 
        {
            .invert_out = false,
        }
    };

    led_strip_rmt_config_t rmt_config =
    {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = 
        {
            .with_dma = false,
        }
    };
    led_strip_handle_t led_strip = NULL;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    uint8_t colors[][4] = {
        {255,   0,   0,   0},   // Red
        {  0, 255,   0,   0},   // Green
        {  0,   0, 255,   0},   // Blue
        {255, 255, 255, 255}    // White
    };
    const int color_count = sizeof(colors) / sizeof(colors[0]);
    while (1)
    {
        for (int i = 0; i < color_count; ++i)
        {
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, colors[i][0], colors[i][1], colors[i][2]));
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/**
 * @brief Demo task that generates a sine wave CPU load pattern.
 *
 * Creates a variable CPU load that follows a sine wave pattern between
 * minimum and maximum load percentages over a configurable cycle time.
 * The task alternates between busy-wait loops and idle delays to simulate
 * realistic CPU usage patterns. Used for testing system monitoring capabilities.
 *
 * @param param Unused task parameter.
 *
 * @note This task is pinned to core 0 as specified by DEMO_SINE_WAVE_TASK_CORE.
 */
static void demo_sine_wave_task(void *param)
{
    const uint32_t cycleMs = DEMO_SINE_WAVE_TASK_CYCLE_TIME;
    const float minLoad = DEMO_SINE_WAVE_TASK_MIN_LOAD;
    const float maxLoad = DEMO_SINE_WAVE_TASK_MAX_LOAD;
    const uint32_t stepMs = 200;

    ESP_LOGI(LOG_TAG, "Demo sine wave task: Core %d, sine-wave fake load (%.0f–%.0f%%, %lus cycle)",
             xPortGetCoreID(), minLoad * 100.0f, maxLoad * 100.0f, cycleMs / 1000);

    TickType_t startCycle = xTaskGetTickCount();

    for (;;)
    {
        TickType_t now = xTaskGetTickCount();
        uint32_t elapsed = pdTICKS_TO_MS((now - startCycle) % pdMS_TO_TICKS(cycleMs));

        // Sine wave 0→2π
        float phase = (2.0f * M_PI * elapsed) / cycleMs;
        float loadFrac = minLoad + (maxLoad - minLoad) * (0.5f * (sinf(phase) + 1.0f));

        uint32_t busyMs = (uint32_t)(stepMs * loadFrac);
        uint32_t idleMs = stepMs - busyMs;

        TickType_t t0 = xTaskGetTickCount();
        while ((pdTICKS_TO_MS(xTaskGetTickCount() - t0)) < busyMs)
        {
            volatile float f = 0.0f;
            for (int i = 0; i < 500; i++)
            {
                f += 3.14f / 2.71f;
            }
            if (f < 0)
            {
                ESP_LOGI(LOG_TAG, "Fake Load 0!");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(idleMs));
    }
}

/**
 * @brief Demo task that consumes stack and CPU resources for testing.
 *
 * This task allocates local arrays on the stack (320 bytes total) and performs
 * CPU-intensive operations for a configurable duration. It is periodically
 * created and deleted by the demo_task_manager to test task lifecycle monitoring
 * and stack usage tracking capabilities.
 *
 * @param param Unused task parameter.
 *
 * @note This task blocks indefinitely after completing its cycle, waiting to be
 *       deleted by the demo_task_manager.
 */
static void demo_cycle_task(void *param)
{
    ESP_LOGI(LOG_TAG, "Demo cycle task started");

    volatile uint32_t stack_array[192] = { 0 };
    volatile uint8_t additional_stack[128] = { 0 };
    for (int i = 0; i < 192; i++)
    {
        stack_array[i] = i;
    }
    for (int i = 0; i < 128; i++)
    {
        additional_stack[i] = i;
    }

    TickType_t start_time = xTaskGetTickCount();
    const TickType_t run_duration = pdMS_TO_TICKS(DEMO_CYCLE_TASK_CYCLE_TIME_MS);

    volatile uint32_t counter = 0;
    while ((xTaskGetTickCount() - start_time) < run_duration)
    {
        TickType_t t0 = xTaskGetTickCount();
        while (pdTICKS_TO_MS(xTaskGetTickCount() - t0) < 10)
        {
            for (int i = 0; i < 1000; i++)
            {
                counter += (i * 13) % 1237;
                counter ^= (counter << 2);
                counter += (i * 23) % 907;
                counter ^= (counter >> 4);
                stack_array[i % 192] = counter;
                additional_stack[i % 128] = (uint8_t)counter;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(LOG_TAG, "Demo cycle task finished, counter: %lu", counter);

    volatile uint32_t dummy = stack_array[0] + additional_stack[0];
    (void)dummy;

    // Block until deleted by manager
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Task manager that periodically creates and deletes demo cycle tasks.
 *
 * Continuously creates a demo_cycle_task, registers it with sysmon for stack
 * tracking, waits for it to complete its cycle, then deletes it. This pattern
 * repeats indefinitely to test task lifecycle monitoring and stack usage tracking.
 *
 * @param param Unused task parameter.
 *
 * @note Each created task is automatically registered with sysmon_stack_register()
 *       to enable accurate stack usage reporting.
 */
static void demo_task_manager(void *param)
{
    ESP_LOGI(LOG_TAG, "Demo task manager started");
    
    while (1)
    {
        TaskHandle_t demo_cycle_task_handle = NULL;
        BaseType_t result = xTaskCreatePinnedToCore(
            demo_cycle_task,
            "demo_cycle_task",
            DEMO_CYCLE_TASK_STACK_SIZE,
            NULL,
            DEMO_CYCLE_TASK_PRIORITY,
            &demo_cycle_task_handle,
            DEMO_CYCLE_TASK_CORE);
        
        if (result == pdPASS && demo_cycle_task_handle != NULL)
        {
            ESP_LOGI(LOG_TAG, "Demo cycle task created, handle: %p", (void *)demo_cycle_task_handle);
            
            sysmon_stack_register(demo_cycle_task_handle, DEMO_CYCLE_TASK_STACK_SIZE);
            
            vTaskDelay(pdMS_TO_TICKS(DEMO_CYCLE_TASK_CYCLE_TIME_MS));
            
            eTaskState task_state = eTaskGetState(demo_cycle_task_handle);
            if (task_state != eDeleted && task_state != eInvalid)
            {
                ESP_LOGI(LOG_TAG, "Deleting demo cycle task (state: %d)", task_state);
                vTaskDelete(demo_cycle_task_handle);
            }
            else
            {
                ESP_LOGW(LOG_TAG, "Demo cycle task already deleted or invalid (state: %d)", task_state);
            }
            
            vTaskDelay(pdMS_TO_TICKS(100));
            
            ESP_LOGI(LOG_TAG, "Demo cycle task destroyed");
        }
        else
        {
            ESP_LOGE(LOG_TAG, "Failed to create demo cycle task");
        }
        
        vTaskDelay(pdMS_TO_TICKS(DEMO_CYCLE_TASK_CYCLE_TIME_MS));
    }
}

/**
 * @brief Main application entry point.
 *
 * Initializes WiFi connection, system monitor, and creates all demo tasks:
 * - Demo sine wave task (pinned to core 0)
 * - Demo task manager (creates/deletes cycle tasks)
 * - RGB LED cycle task
 *
 * All tasks are registered with the system monitor for stack usage tracking.
 * The function returns after all tasks are created; the application continues
 * running via the created FreeRTOS tasks.
 *
 * @note This is the entry point called by ESP-IDF after system initialization.
 */
void app_main(void)
{
    ESP_LOGI("MAIN", "Connecting to WiFi...");
    esp_err_t wifi_err = wifi_connect();
    if (wifi_err == ESP_OK)
    {
        ESP_LOGI("MAIN", "WiFi connected successfully");
    }
    else
    {
        ESP_LOGE("MAIN", "WiFi connection failed: %s (0x%x)", esp_err_to_name(wifi_err), wifi_err);
    }

    esp_err_t sysmon_err = sysmon_init();
    if (sysmon_err != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "sysmon_init() failed: %s (0x%x). Continuing without sysmon.", 
                 esp_err_to_name(sysmon_err), sysmon_err);
    }
    else
    {
        ESP_LOGI(LOG_TAG, "sysmon initialized successfully");
    }

    // Create demo sine wave task (pinned to core 0)
    TaskHandle_t demo_sine_wave_task_handle = NULL;
    BaseType_t demo_sine_wave_task_result = xTaskCreatePinnedToCore(
        demo_sine_wave_task,
        "demo_sine_task",
        DEMO_SINE_WAVE_TASK_STACK_SIZE,
        NULL,
        DEMO_SINE_WAVE_TASK_PRIORITY,
        &demo_sine_wave_task_handle,
        DEMO_SINE_WAVE_TASK_CORE);
    
    if (demo_sine_wave_task_result != pdPASS)
    {
        ESP_LOGE(LOG_TAG, "Failed to create demo sine wave task");
        return;
    }

    ESP_LOGI(LOG_TAG, "Demo sine wave task created");
    sysmon_stack_register(demo_sine_wave_task_handle, DEMO_SINE_WAVE_TASK_STACK_SIZE);

    // Create demo task manager
    TaskHandle_t demo_task_manager_handle = NULL;
    BaseType_t demo_task_manager_result = xTaskCreate(
        demo_task_manager,
        "demo_task_mgr",
        DEMO_TASK_MANAGER_STACK_SIZE,
        NULL,
        DEMO_TASK_MANAGER_PRIORITY,
        &demo_task_manager_handle);
    
    if (demo_task_manager_result != pdPASS)
    {
        ESP_LOGE(LOG_TAG, "Failed to create demo task manager");
        return;
    }

    ESP_LOGI(LOG_TAG, "Demo task manager created");
    sysmon_stack_register(demo_task_manager_handle, DEMO_TASK_MANAGER_STACK_SIZE);

    // Create RGB LED cycle task
    TaskHandle_t rgb_led_cycle_task_handle = NULL;
    BaseType_t rgb_led_cycle_task_result = xTaskCreate(
        rgb_led_cycle_task,
        "rgb_led_cycle_task",
        RGB_LED_TASK_STACK_SIZE,
        NULL,
        RGB_LED_TASK_PRIORITY,
        &rgb_led_cycle_task_handle);

    if (rgb_led_cycle_task_result != pdPASS)
    {
        ESP_LOGE(LOG_TAG, "Failed to create RGB LED cycle task");
        return;
    }

    ESP_LOGI(LOG_TAG, "RGB LED cycle task created");
    sysmon_stack_register(rgb_led_cycle_task_handle, RGB_LED_TASK_STACK_SIZE);

    ESP_LOGI(LOG_TAG, "App main completed");
}