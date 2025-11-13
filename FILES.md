# SysMon Component Files

This document describes the internal structure and implementation details of the SysMon component. This information is useful for developers who want to understand how the component works internally, modify it, or build similar functionality.

## External Libraries (CDN)

The embedded web UI relies on several third-party libraries, loaded via CDN for convenience and minimal flash usage. This means the ESP32 doesn't store these libraries - your browser downloads them from the internet. This is intentional, because storing Chart.js and Tailwind in flash would be wasteful:

- **[Tailwind CSS (browser version)](https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4)** - Utility-first CSS framework. Allows composition of all UI via classes and exhaustive dark mode/theming, without requiring an exorbitant amount of custom CSS. The browser version processes CSS on-the-fly, which is perfect for embedded use cases.

- **[Chart.js](https://cdn.jsdelivr.net/npm/chart.js)** - Used for rendering live updating charts for CPU and memory usage over time. Handles chart rendering complexity and provides a robust API for real-time data visualization.

- **[Tablesort](https://cdn.jsdelivr.net/npm/tablesort@5.1.0/dist/tablesort.min.js)** - Enables sorting columns in task and hardware information tables. Includes [number column sort extension](https://cdn.jsdelivr.net/npm/tablesort@5.1.0/src/sorts/tablesort.number.min.js) for proper numeric sorting (alphabetical sorting would place "10" before "2").

- **[Google Material Symbols Outlined](https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@24,400,0,0)** - Used for consistent, accessible iconography throughout the UI. Unicode symbols can be inconsistent across platforms, so icon fonts provide more reliable cross-platform consistency.

- **[Microtip](https://cdn.jsdelivr.net/npm/microtip/microtip.css)** - Lightweight CSS-only ARIA-compliant tooltip library. No JavaScript required, reducing complexity and improving performance.

All library dependencies are loaded at runtime via CDN, so the only files stored on the device are small web resources. No heavyweight frameworks (React, Vue, etc.) are used — the UI logic is plain, modern JavaScript using modules in `/www/js/`. This keeps the flash footprint small and the code maintainable.

## Key Files

- **`example/main/main.c`** - Application entry point and demo code showcasing SysMon integration. Initializes Wi-Fi networking (via `wifi_credentials.h`, not included), starts the SysMon background monitor, and creates example FreeRTOS tasks (CPU load generator, dynamic task lifecycle manager, RGB LED demo). Tasks are registered with SysMon for stack monitoring to demonstrate real-time CPU, stack, and memory tracking in action. Intended as a reference for integrating SysMon into your own ESP-IDF projects.

### Core Source Files

- **`src/sysmon.c`** - Main monitoring engine that samples FreeRTOS task statistics and system memory at configurable intervals. Manages the background monitor task, maintains cyclic history buffers for CPU/memory metrics, calculates per-task and per-core CPU utilization, tracks DRAM/PSRAM statistics, and coordinates with the HTTP server for telemetry export.

- **`src/sysmon_http.c`** - HTTP server lifecycle management. Initializes and configures the ESP-IDF HTTP server, registers static file handlers for web UI assets, registers JSON API endpoint handlers, and manages server start/stop operations.

- **`src/sysmon_handlers.c`** - HTTP request handlers for serving embedded static files (HTML, CSS, JS) and JSON API endpoints. Implements generic handler factories that work with configuration structures to serve binary-embedded web resources and generate JSON responses. The generic approach reduces code duplication.

- **`src/sysmon_json.c`** - JSON response generation for all API endpoints. Builds JSON objects for `/tasks` (task metadata), `/history` (time-series data), `/telemetry` (current CPU/memory snapshots), and `/hardware` (chip info, partitions, WiFi status). Handles chip variant detection, partition usage statistics, and hardware feature enumeration.

- **`src/sysmon_stack.c`** - Stack size registration and lookup system. Maintains a thread-safe registry of task stack sizes (since ESP-IDF doesn't expose this via FreeRTOS APIs), enabling accurate stack usage percentage calculations for registered tasks.

- **`src/sysmon_utils.c`** - Utility functions for content type detection, task name formatting (renames "main" to "app_main" for clarity), JSON cleanup macros, and WiFi connectivity checks (SSID, RSSI, IP address retrieval).

### Header Files

- **`include/sysmon.h`** - Main public API header. Defines `SysMonState` structure, `TaskUsageSample` structure, initialization/deinitialization functions, and configuration constants. Includes validation checks for required FreeRTOS configuration options.

- **`include/sysmon_http.h`** - HTTP server API declarations (`sysmon_http_start()`, `sysmon_http_stop()`). Internal API, but exposed in case you need it.

- **`include/sysmon_json.h`** - JSON creation function declarations for all API endpoints (`_create_tasks_json()`, `_create_history_json()`, `_create_telemetry_json()`, `_create_hardware_json()`). Internal API.

- **`include/sysmon_stack.h`** - Stack registration API (`sysmon_stack_register()`, `sysmon_stack_get_size()`, `sysmon_stack_cleanup()`). This is the public API for stack monitoring.

- **`include/sysmon_config.h`** - Configuration structures and macros for HTTP route handlers. Defines `static_file_config_t` and `json_handler_config_t` structures, plus helper macros `STATIC_FILE_ENTRY()` and `JSON_ENDPOINT_ENTRY()` for route registration. Internal implementation detail.

- **`include/sysmon_utils.h`** - Utility function declarations for content type detection, task name formatting, JSON cleanup, and WiFi information retrieval. Internal implementation detail.

### Web UI Files

- **`www/index.html`** - Main HTML entry point for the web dashboard. Loads Tailwind CSS via CDN, embeds Material Symbols icons, loads Chart.js and Tablesort libraries, and orchestrates CSS/JS module loading. Contains the complete dashboard structure with charts, tables, and controls.

- **`www/css/sysmon-theme-color-vars.css`** - CSS custom properties (variables) for theme colors in light and dark modes. Defines all color variables used throughout the theme system.

- **`www/css/sysmon-theme-utility-classes.css`** - Theme utility classes that can be used with Tailwind's `@apply` directive. Defines reusable utility classes that reference the color variables from `sysmon-theme-color-vars.css`.

- **`www/css/sysmon-theme.css`** - Theme-specific styling using Tailwind's `@apply` directive. Composes UI components from utility classes defined in `sysmon-theme-utility-classes.css`, providing consistent theming across the dashboard.

- **`www/js/app.js`** - Main application controller. Manages application state, coordinates data fetching from API endpoints, handles UI updates, manages pause/resume functionality, and orchestrates communication between chart, table, and theme modules.

- **`www/js/charts.js`** - Chart.js integration for CPU and memory visualization. Creates and updates Chart.js instances for CPU usage (per-task and per-core) and memory usage (DRAM/PSRAM) over time. Handles color assignment, data series management, and real-time chart updates.

- **`www/js/table.js`** - Task table management with sorting capabilities. Renders sortable task information tables, integrates Tablesort library for column sorting, and updates table data from API responses.

- **`www/js/config.js`** - Configuration management and API routing. Defines API endpoint constants, chart configuration (colors, intervals, sample counts), and provides theme-aware configuration getters.

- **`www/js/theme.js`** - Dark/light theme switching. Manages theme state persistence, applies theme classes to the document, and coordinates theme changes across UI components.

- **`www/js/utils.js`** - General utility functions for formatting, data manipulation, and helper operations used across the frontend modules.

### Configuration Files

- **`CMakeLists.txt`** - ESP-IDF component build configuration. Declares source files, include directories, required ESP-IDF components, and embeds web assets (HTML, CSS, JS) as binary data using `target_add_binary_data()`.

- **`Kconfig`** - ESP-IDF Kconfig menu definitions for sysmon configuration options. Defines configurable parameters: HTTP server port, CPU sampling interval, history buffer size, and HTTP control port.

## Web Server and Binary Data Embedding

This project was partially an excuse to experiment with building a full-featured web server on ESP32 and explore modern web development techniques, particularly Tailwind CSS. The result is a drop-in component that demonstrates how to serve a complete, modern web application directly from ESP32 flash memory without requiring external storage or SD cards.

### Binary Data Embedding on ESP32

All web assets (HTML, CSS, and JavaScript files) are embedded directly into flash memory during the build process using ESP-IDF's `target_add_binary_data()` CMake function. This converts source files into linker symbols that can be accessed from C code.

During the build, `target_add_binary_data(${COMPONENT_LIB} "www/index.html" TEXT)` embeds each file and ESP-IDF automatically generates two linker symbols: `_binary_<name>_start` and `_binary_<name>_end` (where `<name>` is derived from the file path, e.g., `www/index.html` becomes `index_html`). The HTTP handlers use the `STATIC_FILE_ENTRY()` macro to access these symbols, which expands to a structure containing the URI path and pointers to the start/end symbols. When serving a file, `http_handle_static_file()` calculates the length as `end - start`, excludes the null terminator added by `TEXT` mode, sets the content type, and sends the data directly from flash memory.

### Tailwind CSS Experimentation

The web dashboard uses Tailwind CSS's browser version, which processes utility classes on-the-fly in the browser rather than requiring a build step. This was chosen as an experiment to see if modern CSS frameworks could work well in embedded contexts where traditional build pipelines aren't practical. The browser version eliminates the need for Node.js build processes, processes only the classes actually used in the HTML (keeping runtime overhead minimal), and provides the full Tailwind utility class system. The experiment proved successful - you can build modern, responsive UIs with Tailwind's utility classes directly in embedded web applications, with all CSS processing happening client-side.

### Future Features

- Warning/Error logging via toast popup
- Variable tracking

