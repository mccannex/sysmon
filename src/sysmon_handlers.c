/**
 * @file sysmon_handlers.c
 * @brief HTTP request handlers for sysmon HTTP server.
 *
 * This file implements HTTP request handlers for serving static files and
 * JSON endpoints in the sysmon HTTP server.
 */

// Project-specific includes
#include "sysmon_config.h"
#include "sysmon_json.h"
#include "sysmon_utils.h"
#include "sysmon.h"

// ESP-IDF includes
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

// System includes
#include <stdlib.h>
#include <string.h>

// Logger tag for this module
static const char *LOG_TAG = "sysmon_handlers";

/**
 * @brief Handler function for static files (internal use only).
 *
 * @param request HTTP request object.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t http_handle_static_file(httpd_req_t *request)
{
    // Get config from user_ctx
    const static_file_config_t *config = (const static_file_config_t *)request->user_ctx;
    if (config == NULL)
    {
        ESP_LOGE(LOG_TAG, "Static file config is NULL");
        return httpd_resp_send_500(request);
    }

    const uint8_t *start = config->start;
    const uint8_t *end = config->end;
    size_t len = (size_t)(end - start);

    // Always exclude null terminator from TEXT mode embeddings
    if (len > 0)
    {
        len--;
    }

    // Symbol and length checks to prevent runtime failure.
    if (start == NULL || end == NULL || len == 0)
    {
        ESP_LOGE(LOG_TAG, "embedded symbols not found for %s", config->uri);
        return httpd_resp_send_500(request);
    }

    const char *content_type = _get_content_type_from_uri(config->uri);
    httpd_resp_set_type(request, content_type);
    
    // Add CORS headers to allow cross-origin requests from other machines
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Content-Type");
    
    return httpd_resp_send(request, (const char *)start, (ssize_t)len);
}

/**
 * @brief Handler function for JSON endpoints (internal use only).
 *
 * @param request HTTP request object.
 * @return ESP_OK on success, HTTP 500 on JSON build failure.
 */
esp_err_t http_handle_json_endpoint(httpd_req_t *request)
{
    // Get config from user_ctx
    const json_handler_config_t *config = (const json_handler_config_t *)request->user_ctx;
    if (config == NULL || config->create_json == NULL)
    {
        ESP_LOGE(LOG_TAG, "JSON handler config is NULL");
        return httpd_resp_send_500(request);
    }

    cJSON *json_root = config->create_json();
    if (json_root == NULL)
    {
        ESP_LOGE(LOG_TAG, "Failed to create JSON for %s", config->uri);
        return httpd_resp_send_500(request);
    }

    // Serialize JSON to string
    char *json_string = cJSON_Print(json_root);
    if (json_string == NULL)
    {
        ESP_LOGE(LOG_TAG, "Failed to serialize JSON for %s", config->uri);
        JSON_CLEANUP(json_root);
        return httpd_resp_send_500(request);
    }

    // Send JSON response
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    
    // Add CORS headers to allow cross-origin requests from other machines
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Content-Type");
    
    esp_err_t result = httpd_resp_send(request, json_string, HTTPD_RESP_USE_STRLEN);
    if (result != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "httpd_resp_send() failed for %s: %s (0x%x)", 
                 config->uri, esp_err_to_name(result), result);
    }

    // Cleanup
    free(json_string);
    cJSON_Delete(json_root);
    return result;
}

