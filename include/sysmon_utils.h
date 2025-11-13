/**
 * @file sysmon_utils.h
 * @brief Utility functions for sysmon HTTP module.
 *
 * This header declares utility functions used across the sysmon HTTP
 * subsystem for content type detection, task name formatting, and JSON
 * cleanup operations.
 */

#pragma once

// ESP-IDF includes
#include "cJSON.h"
#include "esp_err.h"

// System includes
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get display name for a task, renaming "main" to "app_main" for clarity.
 *
 * @param task_name Original task name.
 * @return Display name for the task.
 */
const char *_get_task_display_name(const char *task_name);

/**
 * @brief Determine content type from URI path.
 *
 * @param uri URI path string.
 * @return Content type string based on URI extension.
 */
const char *_get_content_type_from_uri(const char *uri);

/**
 * @brief Clean up multiple cJSON objects.
 *
 * @param first First cJSON object to delete (required).
 * @param ... Additional cJSON objects to delete, terminated by NULL.
 *
 * Deletes all provided cJSON objects. Safe to call with NULL pointers.
 */
void _json_cleanup(cJSON *first, ...);

/**
 * @brief Macro to simplify cleanup of multiple cJSON objects.
 *
 * Usage: JSON_CLEANUP(json1, json2, json3);
 */
#define JSON_CLEANUP(...) _json_cleanup(__VA_ARGS__, NULL)

/**
 * @brief Get connected WiFi SSID.
 *
 * @param ssid_buffer Buffer to store SSID string (must be at least 33 bytes).
 * @param buffer_size Size of the buffer.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t _get_wifi_ssid(char *ssid_buffer, size_t buffer_size);

/**
 * @brief Get WiFi signal strength (RSSI) in dBm.
 *
 * @param rssi Pointer to store RSSI value.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t _get_wifi_rssi(int8_t *rssi);

/**
 * @brief Get WiFi IP address as string.
 *
 * @param ip_buffer Buffer to store IP address string (must be at least 16 bytes for "xxx.xxx.xxx.xxx").
 * @param buffer_size Size of the buffer.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t _get_wifi_ip_info(char *ip_buffer, size_t buffer_size);

/**
 * @brief Check if WiFi is connected and has an IP address.
 *
 * Verifies that the default WiFi STA netif exists and has a valid IPv4 address.
 * This is required for the HTTP server to function properly.
 *
 * @return ESP_OK if WiFi is connected with valid IP, ESP_ERR_INVALID_STATE otherwise.
 */
esp_err_t _check_wifi_connectivity(void);

#ifdef __cplusplus
}
#endif

