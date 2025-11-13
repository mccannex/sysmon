#pragma once

// ESP-IDF includes
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/task.h"

// System includes
#include <stdint.h>
#include <stdbool.h>

// Configuration validation: Check required FreeRTOS configuration options
#ifndef CONFIG_FREERTOS_USE_TRACE_FACILITY
    #error "sysmon requires CONFIG_FREERTOS_USE_TRACE_FACILITY to be enabled. This is required for uxTaskGetSystemState(). Please enable it in sdkconfig."
#endif

#ifndef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    #error "sysmon requires CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS to be enabled. This is required for task runtime statistics. Please enable it in sdkconfig."
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Configuration directives (from Kconfig)
// These are defined in components/sysmon/Kconfig and available as CONFIG_SYSMON_*
// Fallback defaults if Kconfig is not used
#ifndef CONFIG_SYSMON_CPU_SAMPLING_INTERVAL_MS
#define CONFIG_SYSMON_CPU_SAMPLING_INTERVAL_MS 1000  // Sample CPU usage every second
#endif

#ifndef CONFIG_SYSMON_SAMPLE_COUNT
#define CONFIG_SYSMON_SAMPLE_COUNT 60
#endif

#ifndef CONFIG_SYSMON_HTTPD_SERVER_PORT
#define CONFIG_SYSMON_HTTPD_SERVER_PORT 8080
#endif

#ifndef CONFIG_SYSMON_HTTPD_CTRL_PORT
#define CONFIG_SYSMON_HTTPD_CTRL_PORT   32768
#endif


#define SYSMON_MONITOR_STACK_SIZE  4096
#define SYSMON_MONITOR_PRIORITY    7
#define SYSMON_MONITOR_CORE        0

#define SYSMON_MAX_TRACKED_TASKS        256
#define SYSMON_ZERO_THRESHOLD           0.0001f

// Strong reference to the actual embedded symbols present in your build
// Note that ESP IDF strips the directory names from the final symbol name, no subfolders
extern const uint8_t _binary_index_html_start[];
extern const uint8_t _binary_index_html_end[];
extern const uint8_t _binary_sysmon_theme_color_vars_css_start[];
extern const uint8_t _binary_sysmon_theme_color_vars_css_end[];
extern const uint8_t _binary_sysmon_theme_utility_classes_css_start[];
extern const uint8_t _binary_sysmon_theme_utility_classes_css_end[];
extern const uint8_t _binary_sysmon_theme_css_start[];
extern const uint8_t _binary_sysmon_theme_css_end[];
extern const uint8_t _binary_config_js_start[];
extern const uint8_t _binary_config_js_end[];
extern const uint8_t _binary_theme_js_start[];
extern const uint8_t _binary_theme_js_end[];
extern const uint8_t _binary_utils_js_start[];
extern const uint8_t _binary_utils_js_end[];
extern const uint8_t _binary_charts_js_start[];
extern const uint8_t _binary_charts_js_end[];
extern const uint8_t _binary_table_js_start[];
extern const uint8_t _binary_table_js_end[];
extern const uint8_t _binary_app_js_start[];
extern const uint8_t _binary_app_js_end[];

/**
 * @brief Stores usage samples and statistics for a single tracked FreeRTOS task.
 *
 * This struct contains both the time-series history buffers used by the sampler and metadata fields
 * populated from the FreeRTOS TaskStatus_t snapshot during each sampling interval.
 * Members are populated and updated by the monitor logic in sysmon.c.
 *
 * Members                       : 
 * - task_name                   : Fixed-length buffer holding the task name (matches t->pcTaskName from TaskStatus_t).
 * - usage_percent_history       : Array of per-sample CPU usage percentages for this task (cyclic buffer).
 * - stack_usage_bytes_history   : Array of per-sample stack usage in bytes (cyclic buffer).
 * - stack_usage_percent_history : Array of per-sample stack usage as a percentage of stack_size_bytes (cyclic buffer).
 * - write_index                 : Index for the next write into the rolling history buffers.
 * - is_active                   : Whether this entry represents a currently observed (alive) task.
 * - consecutive_zero_samples    : Number of consecutive samples this task's usage was zero (used to time out deleted tasks).
 * - task_id                     : RTOS-assigned numeric task ID (from TaskStatus_t.xTaskNumber).
 * - current_priority            : Current FreeRTOS priority of the task (from TaskStatus_t.uxCurrentPriority).
 * - base_priority               : Initial or base FreeRTOS priority for this task (from TaskStatus_t.uxBasePriority).
 * - total_run_time_ticks        : Cumulative run time as counted by FreeRTOS up to the latest sample (from TaskStatus_t.ulRunTimeCounter).
 * - stack_high_water_mark       : Minimum remaining stack (words) observed since task creation (from TaskStatus_t.usStackHighWaterMark).
 * - stack_size_bytes            : Stack size in bytes (as registered, see sysmon_stack API).
 * - core_id                     : The core number this task is running/pinned to (from TaskStatus_t.xCoreID).
 * - prev_run_time_ticks         : Logical copy of previous ulRunTimeCounter for this task since the last sample, used for delta calculations.
 *
 * The time series buffers have length = CONFIG_SYSMON_SAMPLE_COUNT and are maintained as circular buffers.
 * This structure is filled, tracked, and used internally by sysmon.c and exposed to JSON and telemetry handlers.
 */

typedef struct
{
    char task_name[24];
    float usage_percent_history[CONFIG_SYSMON_SAMPLE_COUNT];
    uint32_t stack_usage_bytes_history[CONFIG_SYSMON_SAMPLE_COUNT];
    float stack_usage_percent_history[CONFIG_SYSMON_SAMPLE_COUNT];
    int write_index;
    bool is_active;
    int consecutive_zero_samples;
    UBaseType_t task_id;
    UBaseType_t current_priority;
    UBaseType_t base_priority;
    uint32_t total_run_time_ticks;
    uint32_t stack_high_water_mark;
    uint32_t stack_size_bytes;
    int core_id;
    uint32_t prev_run_time_ticks;
} TaskUsageSample;

/**
 * @brief Stores global usage and state for the sysmon monitor.
 *
 * This struct encapsulates the memory and operational state required by the
 * sysmon monitoring subsystem, including task and memory history buffers,
 * handles to system resources, time-series CPU and memory statistics, and runtime metadata.
 *
 * Members:
 * - httpd                : Handle to the HTTP server providing sysmon telemetry endpoints.
 * - tasks                : Array of per-task usage samples (TaskUsageSample), dynamically allocated.
 * - task_status          : Array of TaskStatus_t used to query live FreeRTOS task states.
 * - task_capacity        : Capacity of the allocated tasks/task_status arrays (number of slots).
 * - prev_total_run_time  : Snapshot of the previous global runtime tick count (for usage delta calculation).
 * - monitor_task_handle  : RTOS task handle for the main sysmon monitor task.
 *
 * - cpu_overall_percent  : Ring buffer of overall CPU usage percentages.
 * - cpu_core_percent     : Ring buffer of per-core CPU usage percentages.
 * - dram_free            : Ring buffer of DRAM free bytes.
 * - dram_min_free        : Ring buffer of DRAM minimum free bytes.
 * - dram_largest_block   : Ring buffer of DRAM largest free block sizes.
 * - dram_total           : Ring buffer of total DRAM available.
 * - dram_used_percent    : Ring buffer of DRAM usage percent.
 * - psram_free           : Ring buffer of PSRAM free bytes.
 * - psram_total          : Ring buffer of PSRAM total bytes.
 * - psram_used_percent   : Ring buffer of PSRAM usage percent.
 *
 * - series_write_index   : Ring buffer write head for time-series data.
 * - psram_seen           : True if PSRAM is detected on this platform/session.
 * - log_decimator        : Used for periodic logging throttling.
 *
 * The structure is owned and manipulated exclusively by sysmon.c, but its
 * reference is provided by extern for certain operations in other modules.
 */
typedef struct
{
    httpd_handle_t httpd;
    TaskUsageSample *tasks;
    TaskStatus_t *task_status;
    int task_capacity;
    uint32_t prev_total_run_time;
    TaskHandle_t monitor_task_handle;

    // Lightweight time series (length = CONFIG_SYSMON_SAMPLE_COUNT)
    float cpu_overall_percent[CONFIG_SYSMON_SAMPLE_COUNT];
    float cpu_core_percent[2][CONFIG_SYSMON_SAMPLE_COUNT];
    uint32_t dram_free[CONFIG_SYSMON_SAMPLE_COUNT];
    uint32_t dram_min_free[CONFIG_SYSMON_SAMPLE_COUNT];
    uint32_t dram_largest_block[CONFIG_SYSMON_SAMPLE_COUNT];
    uint32_t dram_total[CONFIG_SYSMON_SAMPLE_COUNT];
    float dram_used_percent[CONFIG_SYSMON_SAMPLE_COUNT];
    uint32_t psram_free[CONFIG_SYSMON_SAMPLE_COUNT];
    uint32_t psram_total[CONFIG_SYSMON_SAMPLE_COUNT];
    float psram_used_percent[CONFIG_SYSMON_SAMPLE_COUNT];

    int series_write_index;
    bool psram_seen;
    int log_decimator;
} SysMonState;

// Shared module state (defined in sysmon.c)
extern SysMonState self;

/**
 * @brief Initialize System Monitor: start HTTP server on port 81 and task monitor.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t sysmon_init(void);

/**
 * @brief Stop System Monitor and free resources.
 */
void sysmon_deinit(void);

#ifdef __cplusplus
}
#endif


