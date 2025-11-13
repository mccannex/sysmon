/**
 * @file sysmon_config.h
 * @brief Configuration structures and macros for HTTP server route handlers.
 *
 * This header defines the configuration structures and helper macros used to
 * configure static file handlers and JSON endpoint handlers in the sysmon
 * HTTP server.
 */

#pragma once

// ESP-IDF includes
#include "cJSON.h"

// System includes
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration structure for static file handlers.
 */
typedef struct
{
    const char *uri;
    const uint8_t *start;
    const uint8_t *end;
} static_file_config_t;

/**
 * @brief Configuration structure for JSON endpoint handlers.
 */
typedef struct
{
    const char *uri;
    cJSON *(*create_json)(void);
} json_handler_config_t;

/**
 * @brief Macro to simplify binary file entry configuration.
 *
 * @param uri_path URI path for the static file
 * @param name Base name of the binary symbol (e.g., "index_html" for _binary_index_html_start)
 */
#define STATIC_FILE_ENTRY(uri_path, name) \
    { \
        .uri   = uri_path, \
        .start = _binary_##name##_start, \
        .end   = _binary_##name##_end \
    }

/**
 * @brief Macro to simplify JSON endpoint entry configuration.
 *
 * @param uri_path URI path for the JSON endpoint
 * @param create_json_func Function pointer to JSON creation function
 */
#define JSON_ENDPOINT_ENTRY(uri_path, create_json_func) \
    { \
        .uri         = uri_path, \
        .create_json = create_json_func \
    }

#ifdef __cplusplus
}
#endif

