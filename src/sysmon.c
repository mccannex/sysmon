/**
 * @file sysmon.c
 * @brief Task and system resource monitoring for ESP-IDF platforms.
 *
 * This file implements runtime monitoring and reporting of task CPU usage and system memory for ESP32 platforms, 
 * providing a lightweight RTOS task sampler with historical recording, telemetry, and a public interface for integration 
 * with HTTP/Web UI components via the sysmon_http module.
 *
 * Responsibilities:
 *   - Periodically sample FreeRTOS task execution statistics and memory usage.
 *   - Maintain a cyclic history buffer for use by UI and telemetry endpoints.
 *   - Compute and expose per-task and per-core CPU utilization metrics.
 *   - Track DRAM/PSRAM free/peak/fragmentation statistics.
 *   - Coordinate and manage the sampler/metrics monitoring task lifecycle.
 *
 * Dependencies:
 *   - FreeRTOS APIs for task/system state access.
 *   - ESP-IDF heap and logging facilities.
 *   - sysmon_http for HTTP telemetry service.
 *
 * Usage:
 *   - Use sysmon_init()/sysmon_deinit() to control global sampling and telemetry.
 *   - Call sysmon_stack_register() after creating tasks to enable accurate stack size reporting.
 *   - For consuming metrics, see sysmon_http.c.
 *
 */

// Project-specific includes
#include "sysmon.h"
#include "sysmon_http.h"
#include "sysmon_stack.h"
#include "sysmon_utils.h"

// ESP-IDF includes
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

// System includes
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// Logger tag for this module
static const char *LOG_TAG = "sysmon";

// Persistent module state (shared with sysmon_http.c)
// Stores current task info, stats buffers, task handle, and ringbuffer pointers.
SysMonState self = { 0 };

// ============================================================================
// Monitor Task Helper Functions
// ============================================================================

/**
 * @brief Ensure task storage capacity is sufficient for all active tasks.
 * 
 * Uses dynamic calculation based on actual task count with percentage-based growth buffer.
 * 
 * @return true if capacity is adequate, false on allocation failure.
 */
static bool _ensure_task_storage_capacity(void)
{
    // If we have existing capacity, try to get actual task count
    int actual_task_count = 0;
    bool buffer_was_full = false;
    if (self.task_capacity > 0 && self.task_status != NULL)
    {
        uint32_t total_run_time = 0;
        UBaseType_t num = uxTaskGetSystemState(self.task_status, self.task_capacity, &total_run_time);
        actual_task_count = (int)num;
        
        // If we got fewer tasks than capacity, we have enough space
        if (actual_task_count < self.task_capacity)
        {
            return true;
        }
        
        // If we got exactly capacity, buffer was full - might have more tasks
        buffer_was_full = (actual_task_count == self.task_capacity);
    }
    else
    {
        // No existing buffer, use uxTaskGetNumberOfTasks() as initial estimate
        actual_task_count = (int)uxTaskGetNumberOfTasks();
    }
    
    // Calculate required capacity with dynamic growth buffer
    // If buffer was full, grow more aggressively (50%) to avoid multiple iterations
    // Otherwise, use smaller growth (20%) for normal scaling
    int growth_percent = buffer_was_full ? 50 : 20;
    int growth_buffer = (actual_task_count * growth_percent) / 100;
    if (growth_buffer < 1)
    {
        growth_buffer = 1;  // Minimum 1 extra slot
    }
    int required_capacity = actual_task_count + growth_buffer;
    
    // Ensure we don't exceed maximum
    if (required_capacity > SYSMON_MAX_TRACKED_TASKS)
    {
        required_capacity = SYSMON_MAX_TRACKED_TASKS;
    }
    
    if (required_capacity <= self.task_capacity)
    {
        return true;
    }
    
    TaskUsageSample *new_tasks = (TaskUsageSample *)calloc(required_capacity, sizeof(TaskUsageSample));
    if (new_tasks == NULL)
    {
        return false;
    }
    
    TaskStatus_t *new_status = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * required_capacity);
    if (new_status == NULL)
    {
        free(new_tasks);
        return false;
    }
    
    // Copy existing active tasks
    if (self.tasks != NULL)
    {
        for (int j = 0; j < self.task_capacity; j++)
        {
            if (self.tasks[j].is_active)
            {
                new_tasks[j] = self.tasks[j];
            }
        }
    }
    
    // Ownership hand-off
    free(self.tasks);
    free(self.task_status);
    self.tasks         = new_tasks;
    self.task_status   = new_status;
    self.task_capacity = required_capacity;
    
    return true;
}

/**
 * @brief Sample current task states and calculate total runtime delta.
 * 
 * @param num_returned Output: number of tasks returned by uxTaskGetSystemState.
 * @param delta_total Output: calculated delta for total runtime.
 * @return true on success, false if sampling failed.
 */
static bool _sample_task_states(UBaseType_t *num_returned, uint32_t *delta_total)
{
    uint32_t total_run_time = 0;
    UBaseType_t num = uxTaskGetSystemState(self.task_status, self.task_capacity, &total_run_time);
    
    if (num == 0)
    {
        return false;
    }
    
    *num_returned = num;
    
    // Calculate delta_total, handling wrap-around
    if (total_run_time >= self.prev_total_run_time)
    {
        *delta_total = total_run_time - self.prev_total_run_time;
    }
    else
    {
        // Wrap-around case
        *delta_total = (UINT32_MAX - self.prev_total_run_time) + total_run_time + 1;
    }
    
    self.prev_total_run_time = total_run_time;
    
    return true;
}

/**
 * @brief Find or create task entry index for a given task name.
 * 
 * @param task_name Task name to find.
 * @return Task index on success, -1 if no slot available.
 */
static int _find_or_create_task_index(const char *task_name)
{
    // Try to find existing task entry
    for (int j = 0; j < self.task_capacity; j++)
    {
        if (self.tasks[j].is_active && 
            strncmp(self.tasks[j].task_name, task_name, sizeof(self.tasks[j].task_name)) == 0)
        {
            return j;
        }
    }
    
    // Allocate slot for new task
    for (int j = 0; j < self.task_capacity; j++)
    {
        if (!self.tasks[j].is_active)
        {
            memset(&self.tasks[j], 0, sizeof(TaskUsageSample));
            strncpy(self.tasks[j].task_name, task_name, sizeof(self.tasks[j].task_name) - 1);
            self.tasks[j].is_active = true;
            self.tasks[j].consecutive_zero_samples = 0;
            ESP_LOGI(LOG_TAG, "Discovered new task: '%s'", task_name);
            return j;
        }
    }
    
    return -1;
}

/**
 * @brief Update task usage history for a single task.
 * 
 * @param idx Task index.
 * @param task_status Task status from uxTaskGetSystemState.
 * @param delta_total Total runtime delta for CPU calculation.
 */
static void _update_task_history(int idx, const TaskStatus_t *task_status, uint32_t delta_total)
{
    // Compute delta runtime
    uint32_t delta_task = 0;
    if (task_status->ulRunTimeCounter >= self.tasks[idx].prev_run_time_ticks)
    {
        delta_task = task_status->ulRunTimeCounter - self.tasks[idx].prev_run_time_ticks;
    }
    self.tasks[idx].prev_run_time_ticks = task_status->ulRunTimeCounter;
    
    // Calculate CPU usage
    float usage = (delta_total > 0) ? ((float)delta_task / (float)delta_total) * 100.0f : 0.0f;
    self.tasks[idx].consecutive_zero_samples = 0;  // Task is present, reset counter
    self.tasks[idx].usage_percent_history[self.tasks[idx].write_index] = usage;
    
    // Calculate stack usage
    self.tasks[idx].stack_high_water_mark = task_status->usStackHighWaterMark;
    uint32_t stack_hwm_bytes = task_status->usStackHighWaterMark * sizeof(StackType_t);
    
    // Lookup registered stack size
    uint32_t stack_size_bytes = 0U;
    sysmon_stack_get_size(task_status->xHandle, &stack_size_bytes);
    
    self.tasks[idx].stack_size_bytes = stack_size_bytes;
    
    uint32_t stack_used_bytes = 0U;
    float stack_usage_percent = 0.0f;
    if (stack_size_bytes > 0U)
    {
        if (stack_size_bytes > stack_hwm_bytes)
        {
            stack_used_bytes = stack_size_bytes - stack_hwm_bytes;
        }
        stack_usage_percent = ((float)stack_used_bytes / (float)stack_size_bytes) * 100.0f;
    }
    
    // Store stack usage history
    self.tasks[idx].stack_usage_bytes_history[self.tasks[idx].write_index] = stack_used_bytes;
    self.tasks[idx].stack_usage_percent_history[self.tasks[idx].write_index] = stack_usage_percent;
    
    // Update task metadata
    self.tasks[idx].write_index = (self.tasks[idx].write_index + 1) % CONFIG_SYSMON_SAMPLE_COUNT;
    self.tasks[idx].task_id = task_status->xTaskNumber;
    self.tasks[idx].current_priority = task_status->uxCurrentPriority;
    self.tasks[idx].base_priority = task_status->uxBasePriority;
    self.tasks[idx].total_run_time_ticks = task_status->ulRunTimeCounter;
    self.tasks[idx].core_id = task_status->xCoreID;
}

/**
 * @brief Process deleted tasks (not seen in current sample).
 * 
 * @param tasks_seen Array indicating which tasks were seen in current sample.
 */
static void _process_deleted_tasks(const bool *tasks_seen)
{
    for (int j = 0; j < self.task_capacity; j++)
    {
        if (self.tasks[j].is_active && !tasks_seen[j])
        {
            self.tasks[j].consecutive_zero_samples++;
            
            // Record zero values
            self.tasks[j].usage_percent_history[self.tasks[j].write_index] = 0.0f;
            self.tasks[j].stack_usage_bytes_history[self.tasks[j].write_index] = 0U;
            self.tasks[j].stack_usage_percent_history[self.tasks[j].write_index] = 0.0f;
            self.tasks[j].write_index = (self.tasks[j].write_index + 1) % CONFIG_SYSMON_SAMPLE_COUNT;
            
            // Mark inactive after CONFIG_SYSMON_SAMPLE_COUNT consecutive zeros
            if (self.tasks[j].consecutive_zero_samples >= CONFIG_SYSMON_SAMPLE_COUNT)
            {
                self.tasks[j].is_active = false;
                self.tasks[j].consecutive_zero_samples = 0;
                ESP_LOGI(LOG_TAG, "Task removed after %d consecutive zero samples: '%s'", 
                         CONFIG_SYSMON_SAMPLE_COUNT, self.tasks[j].task_name);
            }
            else if (self.tasks[j].consecutive_zero_samples % 10 == 0)
            {
                ESP_LOGI(LOG_TAG, "Task not detected; logging zero for inactivity (sample %d of %d): '%s'", 
                         self.tasks[j].consecutive_zero_samples, CONFIG_SYSMON_SAMPLE_COUNT, 
                         self.tasks[j].task_name);
            }
        }
    }
}

/**
 * @brief Calculate per-core CPU usage from idle task deltas.
 * 
 * @param num_returned Number of tasks returned by uxTaskGetSystemState.
 * @param delta_total Total runtime delta.
 * @param core_usage_0 Output: CPU usage for core 0.
 * @param core_usage_1 Output: CPU usage for core 1.
 * @param overall_usage Output: Overall CPU usage.
 */
static void _calculate_cpu_metrics(UBaseType_t num_returned, uint32_t delta_total,
                                    float *core_usage_0, float *core_usage_1, float *overall_usage)
{
    TaskHandle_t idle_handle_0 = xTaskGetIdleTaskHandleForCore(0);
    TaskHandle_t idle_handle_1 = xTaskGetIdleTaskHandleForCore(1);
    
    uint32_t idle_ticks_0 = 0;
    uint32_t idle_ticks_1 = 0;
    for (UBaseType_t i = 0; i < num_returned; i++)
    {
        TaskStatus_t *t = &self.task_status[i];
        if (t->xHandle == idle_handle_0)
        {
            idle_ticks_0 = t->ulRunTimeCounter;
        }
        else if (t->xHandle == idle_handle_1)
        {
            idle_ticks_1 = t->ulRunTimeCounter;
        }
    }
    
    // Maintain state between iterations
    static uint32_t prev_idle_ticks_0 = 0;
    static uint32_t prev_idle_ticks_1 = 0;
    uint32_t delta_idle_0 = (idle_ticks_0 >= prev_idle_ticks_0) ? (idle_ticks_0 - prev_idle_ticks_0) : 0;
    uint32_t delta_idle_1 = (idle_ticks_1 >= prev_idle_ticks_1) ? (idle_ticks_1 - prev_idle_ticks_1) : 0;
    prev_idle_ticks_0 = idle_ticks_0;
    prev_idle_ticks_1 = idle_ticks_1;
    
    *core_usage_0 = 0.0f;
    *core_usage_1 = 0.0f;
    if (delta_total > 0U)
    {
        float idle_percent_0 = ((float)delta_idle_0 / (float)delta_total) * 100.0f;
        float idle_percent_1 = ((float)delta_idle_1 / (float)delta_total) * 100.0f;
        *core_usage_0 = 100.0f - idle_percent_0;
        *core_usage_1 = 100.0f - idle_percent_1;
        
        // Clamp to valid range
        if (*core_usage_0 < 0.0f) { *core_usage_0 = 0.0f; }
        if (*core_usage_0 > 100.0f) { *core_usage_0 = 100.0f; }
        if (*core_usage_1 < 0.0f) { *core_usage_1 = 0.0f; }
        if (*core_usage_1 > 100.0f) { *core_usage_1 = 100.0f; }
    }
    *overall_usage = (*core_usage_0 + *core_usage_1) * 0.5f;
}

/**
 * @brief Collect DRAM and PSRAM heap statistics.
 * 
 * @param dram_free Output: DRAM free bytes.
 * @param dram_min_free Output: DRAM minimum free bytes.
 * @param dram_largest Output: DRAM largest free block.
 * @param dram_total Output: DRAM total bytes.
 * @param dram_used_percent Output: DRAM used percentage.
 * @param psram_free Output: PSRAM free bytes.
 * @param psram_total Output: PSRAM total bytes.
 * @param psram_used_percent Output: PSRAM used percentage.
 */
static void _collect_memory_stats(uint32_t *dram_free, uint32_t *dram_min_free, uint32_t *dram_largest,
                                   uint32_t *dram_total, float *dram_used_percent,
                                   uint32_t *psram_free, uint32_t *psram_total, float *psram_used_percent)
{
    *dram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    *dram_min_free = esp_get_minimum_free_heap_size();
    *dram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    *dram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    uint32_t dram_used = (*dram_total > *dram_free) ? (*dram_total - *dram_free) : 0;
    *dram_used_percent = (*dram_total > 0) ? ((float)dram_used / (float)*dram_total) * 100.0f : 0.0f;
    
    *psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    *psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (*psram_total > 0U)
    {
        self.psram_seen = true;
    }
    uint32_t psram_used = (*psram_total > *psram_free) ? (*psram_total - *psram_free) : 0;
    *psram_used_percent = (*psram_total > 0) ? ((float)psram_used / (float)*psram_total) * 100.0f : 0.0f;
}

/**
 * @brief Store sampled metrics in cyclic ringbuffer.
 * 
 * @param overall_usage Overall CPU usage.
 * @param core_usage_0 CPU usage for core 0.
 * @param core_usage_1 CPU usage for core 1.
 * @param dram_free DRAM free bytes.
 * @param dram_min_free DRAM minimum free bytes.
 * @param dram_largest DRAM largest free block.
 * @param dram_total DRAM total bytes.
 * @param dram_used_percent DRAM used percentage.
 * @param psram_free PSRAM free bytes.
 * @param psram_total PSRAM total bytes.
 * @param psram_used_percent PSRAM used percentage.
 */
static void _update_series_buffers(float overall_usage, float core_usage_0, float core_usage_1,
                                   uint32_t dram_free, uint32_t dram_min_free, uint32_t dram_largest,
                                   uint32_t dram_total, float dram_used_percent,
                                   uint32_t psram_free, uint32_t psram_total, float psram_used_percent)
{
    int write_index = self.series_write_index;
    self.cpu_overall_percent[write_index] = overall_usage;
    self.cpu_core_percent[0][write_index] = core_usage_0;
    self.cpu_core_percent[1][write_index] = core_usage_1;
    self.dram_free[write_index] = dram_free;
    self.dram_min_free[write_index] = dram_min_free;
    self.dram_largest_block[write_index] = dram_largest;
    self.dram_total[write_index] = dram_total;
    self.dram_used_percent[write_index] = dram_used_percent;
    self.psram_free[write_index] = psram_free;
    self.psram_total[write_index] = psram_total;
    self.psram_used_percent[write_index] = psram_used_percent;
    self.series_write_index = (write_index + 1) % CONFIG_SYSMON_SAMPLE_COUNT;
}

/**
 * @brief FreeRTOS-RTOS task to sample per-task CPU usage and memory stats at fixed intervals.
 *
 * This function is executed as a pinned FreeRTOS task and performs the following loop:
 *   1. Allocates and right-sizes memory to track all active tasks if the count grows.
 *   2. Samples all tasks' runtime counters and global total counters using uxTaskGetSystemState().
 *   3. Updates or creates per-task usage history entries, calculating deltas and utilization percent.
 *   4. Identifies idle tasks per core, computes per-core idle, and derives CPU workload metrics.
 *   5. Collects DRAM and PSRAM heap statistics for memory diagnostics.
 *   6. Records all observations into cyclic ringbuffers for overview and UI reporting.
 *   7. Sleeps for a configured interval before next sample.
 * Loop continues until task is deleted by external shutdown.
 *
 * Thread-unsafe: This runs as a single RTOS sampler and should not be invoked directly.
 * Relies on external lifetime management through sysmon_init()/sysmon_deinit().
 *
 * @param param (unused)
 */

static void sysmon_monitor(void *param)
{
    ESP_LOGI(LOG_TAG, "task monitor started");
    
    static int log_counter = 0;
    
    for (;;)
    {
        // 1. Ensure task storage capacity
        if (!_ensure_task_storage_capacity())
        {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_SYSMON_CPU_SAMPLING_INTERVAL_MS));
            continue;
        }
        
        // 2. Sample task states
        UBaseType_t num_returned = 0;
        uint32_t delta_total = 0;
        if (!_sample_task_states(&num_returned, &delta_total))
        {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_SYSMON_CPU_SAMPLING_INTERVAL_MS));
            continue;
        }
        
        // Debug logging
        if (log_counter++ % 10 == 0)
        {
            ESP_LOGI(LOG_TAG, "Sampling %u tasks", num_returned);
        }
        
        // Track which tasks were seen
        bool *tasks_seen = (bool *)calloc(self.task_capacity, sizeof(bool));
        if (tasks_seen == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_SYSMON_CPU_SAMPLING_INTERVAL_MS));
            continue;
        }
        
        // 3. Update per-task histories
        for (UBaseType_t i = 0; i < num_returned; i++)
        {
            TaskStatus_t *t = &self.task_status[i];
            if (t->pcTaskName == NULL)
            {
                continue;
            }
            
            int idx = _find_or_create_task_index(t->pcTaskName);
            if (idx == -1)
            {
                ESP_LOGW(LOG_TAG, "Task capacity exceeded, cannot track task '%s' (capacity: %d, num_tasks: %d). Will retry next sample.", 
                         t->pcTaskName, self.task_capacity, uxTaskGetNumberOfTasks());
                continue;
            }
            
            _update_task_history(idx, t, delta_total);
            tasks_seen[idx] = true;
        }
        
        // 4. Process deleted tasks
        _process_deleted_tasks(tasks_seen);
        free(tasks_seen);
        
        // 5. Calculate CPU metrics
        float core_usage_0, core_usage_1, overall_usage;
        _calculate_cpu_metrics(num_returned, delta_total, &core_usage_0, &core_usage_1, &overall_usage);
        
        // 6. Collect memory statistics
        uint32_t dram_free, dram_min_free, dram_largest, dram_total;
        float dram_used_percent;
        uint32_t psram_free, psram_total;
        float psram_used_percent;
        _collect_memory_stats(&dram_free, &dram_min_free, &dram_largest, &dram_total, &dram_used_percent,
                              &psram_free, &psram_total, &psram_used_percent);
        
        // 7. Update series buffers
        _update_series_buffers(overall_usage, core_usage_0, core_usage_1,
                               dram_free, dram_min_free, dram_largest, dram_total, dram_used_percent,
                               psram_free, psram_total, psram_used_percent);
        
        // 8. Delay before next sample
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SYSMON_CPU_SAMPLING_INTERVAL_MS));
    }
}

/**
 * @brief Deinitialize all sysmon state and monitoring resources.
 *
 * Shuts down HTTP telemetry, stops the sampler task, and releases all
 * dynamically allocated memory. After calling, all state is reset and
 * sysmon monitoring is fully stopped.
 *
 * Safe to call multiple times (idempotent).
 *
 * @note Should be called from an appropriate system context to avoid
 *       deleting tasks from within their own context.
 */
void sysmon_deinit(void)
{
    sysmon_http_stop();

    // Terminate task monitor, if running
    if (self.monitor_task_handle != NULL)
    {
        vTaskDelete(self.monitor_task_handle);
        self.monitor_task_handle = NULL;
    }
    // Free task metric storage buffers
    free(self.tasks);
    self.tasks = NULL;
    free(self.task_status);
    self.task_status          = NULL;
    self.task_capacity        = 0;
    self.prev_total_run_time  = 0;
    
    // Clean up stack records
    sysmon_stack_cleanup();
}


/**
 * @brief Initialize task and system monitoring, and start HTTP telemetry server.
 *
 * Allocates and starts the main sampler background task (pinned to core 0)
 * if not already active, and initializes HTTP telemetry endpoints.
 *
 * @return ESP_OK on success, or ESP_FAIL/ESP_ERR_xx on failure.
 * @note Call only once at system startup or when first enabling the UI/telemetry feature.
 *
 * Step-by-step operation:
 *  1. Verify WiFi connectivity (required for HTTP server).
 *  2. Start HTTP API handler for telemetry endpoints.
 *  3. If not already running, create task monitor (CPU+memory) pinned to core 0.
 *  4. Report initialization status via log and return result.
 */
esp_err_t sysmon_init(void)
{
    // 1. Verify WiFi connectivity before starting HTTP server
    esp_err_t err = _check_wifi_connectivity();
    if (err != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "sysmon initialization failed: WiFi connectivity check failed.");
        ESP_LOGE(LOG_TAG, "Halted. Please configure and connect WiFi before initializing sysmon.");
        return err;
    }

    // 2. Start HTTP endpoint
    err = sysmon_http_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "sysmon_http_start() failed: %s (0x%x). Cannot start HTTP telemetry server.", 
                 esp_err_to_name(err), err);
        return err;
    }

    // 3. Only start monitor if not running (singleton pattern)
    if (self.monitor_task_handle == NULL)
    {
        BaseType_t result = xTaskCreatePinnedToCore(
            sysmon_monitor,
            "sysmon_monitor",
            SYSMON_MONITOR_STACK_SIZE,
            NULL,
            SYSMON_MONITOR_PRIORITY,
            &self.monitor_task_handle,
            SYSMON_MONITOR_CORE);

        if (result != pdPASS)
        {
            ESP_LOGE(LOG_TAG, "Failed to create sysmon_monitor task: xTaskCreatePinnedToCore returned %d (pdPASS=%d). Insufficient memory or invalid parameters.", 
                     result, pdPASS);
            return ESP_FAIL;
        }

        // Register the sysmon task stack size
        sysmon_stack_register(self.monitor_task_handle, SYSMON_MONITOR_STACK_SIZE);

    }

    // 4. Successful startup log for diagnostics with actual IP and port
    char ip_buffer[16] = { 0 };
    esp_err_t ip_err = _get_wifi_ip_info(ip_buffer, sizeof(ip_buffer));
    if (ip_err == ESP_OK)
    {
        ESP_LOGW(LOG_TAG, "sysmon fully initialized and ready: http://%s:%d/", ip_buffer, CONFIG_SYSMON_HTTPD_SERVER_PORT);
    }
    else
    {
        ESP_LOGW(LOG_TAG, "sysmon fully initialized and ready: http://<device-ip>:%d/", CONFIG_SYSMON_HTTPD_SERVER_PORT);
    }
    return ESP_OK;
}

