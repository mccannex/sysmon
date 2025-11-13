/**
 * @file sysmon_utils.c
 * @brief Utility functions for sysmon HTTP module.
 *
 * This file implements utility functions used across the sysmon HTTP
 * subsystem for content type detection, task name formatting, and JSON
 * cleanup operations.
 */

// Project-specific includes
#include "sysmon_utils.h"

// ESP-IDF includes
#include "cJSON.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

// System includes
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

// Logger tag for this module
static const char *LOG_TAG = "sysmon_utils";

/**
 * @brief Get display name for a task, renaming "main" to "app_main" for clarity.
 *
 * @param task_name Original task name.
 * @return Display name for the task.
 */
const char *_get_task_display_name(const char *task_name)
{
    // Rename "main" task to "app_main" to make it clear this is the initialization task
    if (task_name != NULL && strcmp(task_name, "main") == 0)
    {
        return "app_main";
    }
    return task_name;
}

/**
 * @brief Determine content type from URI path.
 *
 * @param uri URI path string.
 * @return Content type string based on URI extension.
 */
const char *_get_content_type_from_uri(const char *uri)
{
    if (uri == NULL)
    {
        return "application/octet-stream";
    }

    // Root URI is HTML
    if (strcmp(uri, "/") == 0)
    {
        return "text/html; charset=utf-8";
    }

    // Check file extension
    size_t uri_len = strlen(uri);
    if (uri_len >= 4 && strcmp(uri + uri_len - 4, ".css") == 0)
    {
        return "text/css; charset=utf-8";
    }
    if (uri_len >= 3 && strcmp(uri + uri_len - 3, ".js") == 0)
    {
        return "application/javascript; charset=utf-8";
    }

    // Default fallback
    return "application/octet-stream";
}

/**
 * @brief Clean up multiple cJSON objects.
 *
 * @param first First cJSON object to delete (required).
 * @param ... Additional cJSON objects to delete, terminated by NULL.
 *
 * Deletes all provided cJSON objects. Safe to call with NULL pointers.
 */
void _json_cleanup(cJSON *first, ...)
{
    if (first != NULL)
    {
        cJSON_Delete(first);
    }

    va_list args;
    va_start(args, first);
    cJSON *item = va_arg(args, cJSON *);
    while (item != NULL)
    {
        cJSON_Delete(item);
        item = va_arg(args, cJSON *);
    }
    va_end(args);
}

/**
 * @brief Get connected WiFi SSID.
 *
 * @param ssid_buffer Buffer to store SSID string (must be at least 33 bytes).
 * @param buffer_size Size of the buffer.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t _get_wifi_ssid(char *ssid_buffer, size_t buffer_size)
{
    if (ssid_buffer == NULL || buffer_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK)
    {
        // WiFi not connected or not initialized
        snprintf(ssid_buffer, buffer_size, "Not Connected");
        return err;
    }

    // Copy SSID (max 32 bytes + null terminator)
    size_t copy_size = (buffer_size - 1 < sizeof(ap_info.ssid)) ? (buffer_size - 1) : sizeof(ap_info.ssid);
    strncpy(ssid_buffer, (const char *)ap_info.ssid, copy_size);
    ssid_buffer[copy_size] = '\0';

    return ESP_OK;
}

/**
 * @brief Get WiFi signal strength (RSSI) in dBm.
 *
 * @param rssi Pointer to store RSSI value.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t _get_wifi_rssi(int8_t *rssi)
{
    if (rssi == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK)
    {
        // WiFi not connected or not initialized
        *rssi = 0;
        return err;
    }

    *rssi = ap_info.rssi;
    return ESP_OK;
}

/**
 * @brief Get WiFi IP address as string.
 *
 * @param ip_buffer Buffer to store IP address string (must be at least 16 bytes for "xxx.xxx.xxx.xxx").
 * @param buffer_size Size of the buffer.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t _get_wifi_ip_info(char *ip_buffer, size_t buffer_size)
{
    if (ip_buffer == NULL || buffer_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL)
    {
        snprintf(ip_buffer, buffer_size, "N/A");
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info = { 0 };
    esp_err_t err = esp_netif_get_ip_info(sta_netif, &ip_info);
    if (err != ESP_OK || ip_info.ip.addr == 0)
    {
        snprintf(ip_buffer, buffer_size, "N/A");
        return (err != ESP_OK) ? err : ESP_ERR_INVALID_STATE;
    }

    // Format IP address as string
    snprintf(ip_buffer, buffer_size, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

/**
 * @brief Check if WiFi is connected and has an IP address.
 *
 * Verifies that the default WiFi STA netif exists and has a valid IPv4 address.
 * This is required for the HTTP server to function properly.
 *
 * @return ESP_OK if WiFi is connected with valid IP, ESP_ERR_INVALID_STATE otherwise.
 */
esp_err_t _check_wifi_connectivity(void)
{
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL)
    {
        ESP_LOGE(LOG_TAG, "WiFi STA netif not found. WiFi must be initialized before sysmon.");
        ESP_LOGE(LOG_TAG, "Please ensure esp_netif_create_default_wifi_sta() has been called.");
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info = { 0 };
    esp_err_t err = esp_netif_get_ip_info(sta_netif, &ip_info);
    if (err != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "Failed to get IP info from WiFi STA netif: %s", esp_err_to_name(err));
        ESP_LOGE(LOG_TAG, "WiFi may not be connected. Please ensure WiFi is connected before initializing sysmon.");
        return ESP_ERR_INVALID_STATE;
    }

    if (ip_info.ip.addr == 0)
    {
        ESP_LOGE(LOG_TAG, "WiFi is not connected (no IP address assigned).");
        ESP_LOGE(LOG_TAG, "Please ensure WiFi is connected and has obtained an IP address before initializing sysmon.");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(LOG_TAG, "WiFi connectivity verified, IP: %d.%d.%d.%d", IP2STR(&ip_info.ip));
    return ESP_OK;
}

