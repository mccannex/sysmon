#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the HTTP server with all route handlers.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t sysmon_http_start(void);

/**
 * @brief Stop the HTTP server.
 */
void sysmon_http_stop(void);

#ifdef __cplusplus
}
#endif

