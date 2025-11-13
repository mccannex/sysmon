/**
 * @file sysmon_http.c
 * @brief HTTP server lifecycle management for ESP32 Task/resource monitoring telemetry.
 *
 * This module manages the HTTP server lifecycle and route registration for the sysmon
 * system monitor. It coordinates with sysmon_handlers.c for request handling and
 * sysmon_json.c for JSON response generation.
 *
 * Responsibilities:
 *   - Initializes and runs the HTTP server for telemetry endpoints.
 *   - Registers static file and JSON endpoint handlers.
 *   - Manages server lifecycle (start/stop).
 *
 * Dependencies:
 *   - ESP-IDF HTTP server (esp_http_server)
 *   - sysmon core API (sysmon.h)
 *   - sysmon_config.h for configuration structures
 *   - sysmon_json.h for JSON function declarations
 *   - sysmon_handlers.c for HTTP request handlers
 *
 * Usage:
 *   - Call sysmon_http_start() to activate endpoints; sysmon_http_stop() to disable.
 *   - Endpoints: '/', '/tasks', '/history', '/telemetry', '/hardware'
 *  */

// Project-specific includes
#include "sysmon_http.h"
#include "sysmon.h"
#include "sysmon_config.h"
#include "sysmon_json.h"

// ESP-IDF includes
#include "esp_log.h"
#include "esp_http_server.h"

// System includes
#include <stddef.h>

// Logger tag for this module
static const char *LOG_TAG = "sysmon_http";

// Forward declarations for handler functions (defined in sysmon_handlers.c)
extern esp_err_t http_handle_static_file(httpd_req_t *request);
extern esp_err_t http_handle_json_endpoint(httpd_req_t *request);

// Static file handler configurations
static const static_file_config_t static_file_configs[] =
{
    STATIC_FILE_ENTRY("/", index_html),
    STATIC_FILE_ENTRY("/css/sysmon-theme-color-vars.css", sysmon_theme_color_vars_css),
    STATIC_FILE_ENTRY("/css/sysmon-theme-utility-classes.css", sysmon_theme_utility_classes_css),
    STATIC_FILE_ENTRY("/css/sysmon-theme.css", sysmon_theme_css),
    STATIC_FILE_ENTRY("/js/theme.js", theme_js),
    STATIC_FILE_ENTRY("/js/config.js", config_js),
    STATIC_FILE_ENTRY("/js/utils.js", utils_js),
    STATIC_FILE_ENTRY("/js/charts.js", charts_js),
    STATIC_FILE_ENTRY("/js/table.js", table_js),
    STATIC_FILE_ENTRY("/js/app.js", app_js)
};

// JSON endpoint handler configurations
static const json_handler_config_t json_handler_configs[] =
{
    JSON_ENDPOINT_ENTRY("/tasks", _create_tasks_json),
    JSON_ENDPOINT_ENTRY("/history", _create_history_json),
    JSON_ENDPOINT_ENTRY("/telemetry", _create_telemetry_json),
    JSON_ENDPOINT_ENTRY("/hardware", _create_hardware_json)
};

/**
 * @brief Helper function to register a URI handler with error handling.
 *
 * @param server HTTP server handle.
 * @param uri URI path string.
 * @param method HTTP method.
 * @param handler Handler function.
 * @param user_ctx User context pointer (config structure).
 * @param handler_name Name for error logging.
 * @return ESP_OK on success, error code otherwise.
 */
static esp_err_t _register_handler(httpd_handle_t server, const char *uri, httpd_method_t method,
                                   esp_err_t (*handler)(httpd_req_t *), void *user_ctx,
                                   const char *handler_name)
{
    httpd_uri_t uri_config =
    {
        .uri      = uri,
        .method   = method,
        .handler  = handler,
        .user_ctx = user_ctx
    };

    esp_err_t err = httpd_register_uri_handler(server, &uri_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "Failed to register %s handler: %s", handler_name, esp_err_to_name(err));
        httpd_stop(server);
        self.httpd = NULL;
        return err;
    }
    return ESP_OK;
}

/**
 * @brief Start and initialize the HTTP telemetry service.
 *
 * @return ESP_OK if service started (or was already running),
 *         or ESP_FAIL/ESP_ERR_xxx from ESP-IDF HTTPD on error.
 *
 * Operation (step-by-step):
 *   1. Idempotently checks if HTTPD already started—exits OK if so.
 *   2. Creates and configures ESP-IDF HTTP server instance.
 *   3. Registers all REST URI handlers.
 *   4. On error, leaves self.httpd = NULL and propagates error upwards.
 *
 * @note The HTTP API uses port/task settings defined by CONFIG_SYSMON_HTTPD_SERVER_PORT/etc.
 * @note All handlers are GET (read-only, telemetry export).
 * @note The sysmon_http module must be initialized before use.
 */
esp_err_t sysmon_http_start(void)
{
    if (self.httpd != NULL)
    {
        // Already running; no-op for idempotence.
        return ESP_OK;
    }
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = CONFIG_SYSMON_HTTPD_SERVER_PORT;
    config.ctrl_port        = CONFIG_SYSMON_HTTPD_CTRL_PORT; // necessary if you want to create multiple HTTPD servers

    // Allow more simultaneous connections for multiple browser asset/API requests
    // Served files: 1 HTML + 3 CSS + 6 JS = 10 static files, plus 4 JSON API endpoints
    // Browsers load these concurrently, so default max_open_sockets=7 is insufficient
    config.max_open_sockets = 12;

    // Set max URI handlers based on how many static files & APIs we'll serve
    size_t static_file_count  = sizeof(static_file_configs) / sizeof(static_file_configs[0]);
    size_t json_handler_count = sizeof(json_handler_configs) / sizeof(json_handler_configs[0]);
    config.max_uri_handlers   = static_file_count + json_handler_count;

    // Warn if LWIP socket pool is too small for this server config
#if CONFIG_LWIP_MAX_SOCKETS < 15
    #warning "CONFIG_LWIP_MAX_SOCKETS may be too low (need at least 15 for max_open_sockets=12)."
#endif


    esp_err_t err = httpd_start(&self.httpd, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "httpd_start() failed: %s (0x%x). Cannot start HTTP server on port %d.", 
                 esp_err_to_name(err), err, CONFIG_SYSMON_HTTPD_SERVER_PORT);
        return err;
    }

    // Register all static file handlers
    for (size_t i = 0; i < sizeof(static_file_configs) / sizeof(static_file_configs[0]); i++)
    {
        err = _register_handler(self.httpd, static_file_configs[i].uri, HTTP_GET,
                                 http_handle_static_file, (void *)&static_file_configs[i],
                                 static_file_configs[i].uri);
        if (err != ESP_OK)
        {
            return err;
        }
    }

    // Register all JSON endpoint handlers
    for (size_t i = 0; i < sizeof(json_handler_configs) / sizeof(json_handler_configs[0]); i++)
    {
        err = _register_handler(self.httpd, json_handler_configs[i].uri, HTTP_GET,
                                 http_handle_json_endpoint, (void *)&json_handler_configs[i],
                                 json_handler_configs[i].uri);
        if (err != ESP_OK)
        {
            return err;
        }
    }

    return ESP_OK;
}

/**
 * @brief Stop the HTTP telemetry service and clean up all resources.
 *
 * @note Safe to call multiple times; does nothing if already stopped.
 * @post self.httpd is set to NULL.
 * @warning Should not be called from within any handler/task running in the HTTPD context.
 */
void sysmon_http_stop(void)
{
    if (self.httpd != NULL)
    {
        httpd_stop(self.httpd);
        self.httpd = NULL;
    }
}
