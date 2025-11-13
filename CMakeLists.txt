idf_component_register(
    SRCS
        "src/sysmon.c"
        "src/sysmon_http.c"
        "src/sysmon_handlers.c"
        "src/sysmon_json.c"
        "src/sysmon_utils.c"
        "src/sysmon_stack.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        "esp_http_server"      # HTTP server for web UI and JSON API endpoints
        "esp_netif"            # Network interface statistics and network info
        "esp_wifi"             # WiFi statistics and connection information
        "esp_partition"        # Partition table enumeration and partition info display
        "bootloader_support"   # Required for esp_image_format.h to parse app partition image headers
        "nvs_flash"            # NVS (non-volatile storage) usage statistics
        "spi_flash"            # SPI flash size and flash information
        "freertos"             # FreeRTOS task statistics, system state, and CPU usage monitoring
        "json"                 # JSON parsing and generation for API responses
)

# Explicitly embed HTML, CSS, and JS files as TEXT
# ensures a trailing \0 is present even though we remove it in the HTTP response
target_add_binary_data(${COMPONENT_LIB} "www/index.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "www/css/sysmon-theme-color-vars.css" TEXT)
target_add_binary_data(${COMPONENT_LIB} "www/css/sysmon-theme-utility-classes.css" TEXT)
target_add_binary_data(${COMPONENT_LIB} "www/css/sysmon-theme.css" TEXT)
target_add_binary_data(${COMPONENT_LIB} "www/js/theme.js" TEXT)
target_add_binary_data(${COMPONENT_LIB} "www/js/config.js" TEXT)
target_add_binary_data(${COMPONENT_LIB} "www/js/utils.js" TEXT)
target_add_binary_data(${COMPONENT_LIB} "www/js/charts.js" TEXT)
target_add_binary_data(${COMPONENT_LIB} "www/js/table.js" TEXT)
target_add_binary_data(${COMPONENT_LIB} "www/js/app.js" TEXT)