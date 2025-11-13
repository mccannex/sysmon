/**
 * @file sysmon_stack.c
 * @brief Stack registration and lookup for sysmon task monitoring.
 *
 * This module manages stack size registration for tasks to enable accurate
 * stack usage percentage calculations in the sysmon monitoring system.
 */

// Project-specific includes
#include "sysmon_stack.h"
#include "sysmon.h"

// ESP-IDF includes
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// System includes
#include <string.h>
#include <stdlib.h>

// Logger tag for this module
static const char *LOG_TAG = "sysmon_stack";

typedef struct
{
    TaskHandle_t handle;
    uint32_t     depth_bytes;
    bool         is_valid;
} TaskStackRecord;

static TaskStackRecord *s_stack_records = NULL;
static int s_stack_records_capacity = 0;
static portMUX_TYPE s_stack_records_lock = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Register a task's stack size for accurate monitoring.
 *
 * Call this immediately after creating a task to record its configured stack size.
 * This function is a no-op if sysmon is not initialized, so it's safe to call
 * unconditionally in production code.
 *
 * @param task_handle Handle of the task (returned from xTaskCreate)
 * @param stack_size_bytes Stack size in bytes (same value passed to xTaskCreate)
 */
void sysmon_stack_register(TaskHandle_t task_handle, uint32_t stack_size_bytes)
{
    // No-op if sysmon not initialized
    if (self.monitor_task_handle == NULL)
    {
        ESP_LOGW(LOG_TAG, "sysmon not initialized, cannot register stack");
        return;
    }

    if (task_handle == NULL || stack_size_bytes == 0U)
    {
        ESP_LOGW(LOG_TAG, "Invalid parameters for task stack registration: handle=%p, size=%lu", 
                 task_handle, (unsigned long)stack_size_bytes);
        return;
    }

    // Get task name for logging purposes
    const char *task_name = pcTaskGetName(task_handle);
    if (task_name == NULL)
    {
        task_name = "unknown";
    }

    // Store or update the stack record
    portENTER_CRITICAL(&s_stack_records_lock);
    
    // Determine required capacity (use task_capacity if set, otherwise use a reasonable initial size)
    int required_capacity = (self.task_capacity > 0) ? self.task_capacity : 32;
    
    // Ensure capacity matches task capacity (grow if needed)
    if (s_stack_records_capacity < required_capacity)
    {
        int new_capacity = required_capacity;
        TaskStackRecord *new_records = (TaskStackRecord *)calloc(new_capacity, sizeof(TaskStackRecord));
        if (new_records == NULL)
        {
            portEXIT_CRITICAL(&s_stack_records_lock);
            ESP_LOGE(LOG_TAG, "Failed to allocate stack records (capacity: %d)", new_capacity);
            return;
        }
        
        // Copy existing records
        if (s_stack_records != NULL)
        {
            memcpy(new_records, s_stack_records, s_stack_records_capacity * sizeof(TaskStackRecord));
            free(s_stack_records);
        }
        
        s_stack_records = new_records;
        s_stack_records_capacity = new_capacity;
    }
    
    // Update existing record
    for (int i = 0; i < s_stack_records_capacity; i++)
    {
        if (s_stack_records[i].is_valid && s_stack_records[i].handle == task_handle)
        {
            s_stack_records[i].depth_bytes = stack_size_bytes;
            portEXIT_CRITICAL(&s_stack_records_lock);
            ESP_LOGI(LOG_TAG, "Updated stack size for task '%s': %lu bytes", 
                     task_name, (unsigned long)stack_size_bytes);
            return;
        }
    }

    // Create new record
    for (int i = 0; i < s_stack_records_capacity; i++)
    {
        if (!s_stack_records[i].is_valid)
        {
            s_stack_records[i].handle      = task_handle;
            s_stack_records[i].depth_bytes = stack_size_bytes;
            s_stack_records[i].is_valid    = true;
            break;
        }
    }
    
    portEXIT_CRITICAL(&s_stack_records_lock);
    ESP_LOGI(LOG_TAG, "Registered stack size for task '%s': %lu bytes", 
             task_name, (unsigned long)stack_size_bytes);
}

/**
 * @brief Get registered stack size for a task.
 *
 * @param task_handle Handle of the task.
 * @param stack_size_bytes Output: stack size in bytes (0 if not registered).
 * @return true if task is registered, false otherwise.
 */
bool sysmon_stack_get_size(TaskHandle_t task_handle, uint32_t *stack_size_bytes)
{
    if (task_handle == NULL || stack_size_bytes == NULL)
    {
        return false;
    }

    portENTER_CRITICAL(&s_stack_records_lock);
    if (s_stack_records != NULL)
    {
        for (int i = 0; i < s_stack_records_capacity; i++)
        {
            if (s_stack_records[i].is_valid && s_stack_records[i].handle == task_handle)
            {
                *stack_size_bytes = s_stack_records[i].depth_bytes;
                portEXIT_CRITICAL(&s_stack_records_lock);
                return true;
            }
        }
    }
    portEXIT_CRITICAL(&s_stack_records_lock);
    
    *stack_size_bytes = 0;
    return false;
}

/**
 * @brief Clean up stack records (called during sysmon_deinit).
 */
void sysmon_stack_cleanup(void)
{
    portENTER_CRITICAL(&s_stack_records_lock);
    if (s_stack_records != NULL)
    {
        free(s_stack_records);
        s_stack_records = NULL;
        s_stack_records_capacity = 0;
    }
    portEXIT_CRITICAL(&s_stack_records_lock);
}

