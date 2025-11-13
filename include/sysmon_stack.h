#pragma once

// ESP-IDF includes
#include "freertos/FreeRTOS.h"

// System includes
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
void sysmon_stack_register(TaskHandle_t task_handle, uint32_t stack_size_bytes);

/**
 * @brief Get registered stack size for a task.
 *
 * @param task_handle Handle of the task.
 * @param stack_size_bytes Output: stack size in bytes (0 if not registered).
 * @return true if task is registered, false otherwise.
 */
bool sysmon_stack_get_size(TaskHandle_t task_handle, uint32_t *stack_size_bytes);

/**
 * @brief Clean up stack records (called during sysmon_deinit).
 */
void sysmon_stack_cleanup(void);

#ifdef __cplusplus
}
#endif

