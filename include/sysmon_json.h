/**
 * @file sysmon_json.h
 * @brief JSON creation functions for sysmon HTTP endpoints.
 *
 * This header declares all JSON builder functions used to generate responses
 * for the sysmon HTTP API endpoints.
 */

#pragma once

// ESP-IDF includes
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build task metadata JSON object for all monitored tasks.
 *
 * @return Pointer to root cJSON object (must be freed by caller), or NULL on oom.
 */
cJSON *_create_tasks_json(void);

/**
 * @brief Build JSON object tracing task usage history for all monitored tasks.
 *
 * @return Root cJSON object, or NULL on allocation failure.
 */
cJSON *_create_history_json(void);

/**
 * @brief Create hardware information JSON object with static chip and system info.
 *
 * @return Hardware info JSON object, or NULL on allocation failure.
 */
cJSON *_create_hardware_json(void);

/**
 * @brief Build a complete telemetry JSON object summarizing CPU/memory and current registered task usage.
 *
 * @return Root cJSON object, or NULL on allocation failure.
 */
cJSON *_create_telemetry_json(void);

#ifdef __cplusplus
}
#endif

