# 📊 SysMon - ESP IDF System Monitor Component

https://github.com/user-attachments/assets/f62e3be2-d6b4-4ffc-848d-95a285982dd1

## Table of Contents

- [📋 Overview](#overview)
- [📦 Requirements](#requirements)
- [🚀 Quick Start](#quick-start)
- [⚙️ Configuration](#configuration)
- [🔌 Disabling the Component](#disabling-the-component)
- [📈 Stack Monitoring](#stack-monitoring)
- [📡 API Endpoints](#api-endpoints)
- [🔗 See Also](#see-also)

## 📋Overview

The **SysMon** component provides a real-time system monitor for ESP-IDF projects. While it is tested primarily on ESP32, it is designed to work on most ESP-IDF targets. SysMon runs a lightweight background task that samples your FreeRTOS tasks and memory usage, then serves this data through a web-based dashboard accessible from any browser.

**What it monitors:**

- **CPU usage** - See how much CPU time each task is using, both individually and per CPU core. Historical charts show trends over time, helping you identify CPU-hungry tasks or uneven workload distribution.
- **Stack usage** - Track stack usage for all your FreeRTOS tasks. For registered tasks, you get percentage-based monitoring that makes it easy to spot tasks running low on stack space before they overflow.
- **Memory tracking** - Monitor DRAM and PSRAM usage with metrics like free memory, minimum free (lowest point reached), largest free block (useful for detecting fragmentation), and usage percentages over time.
- **Hardware information** - Displays chip model, partitions, WiFi status, and other system details.

**The web dashboard:**

- Access from any modern web browser - no special software needed
- Live-updating charts showing CPU and memory trends
- Sortable task tables with filtering options
- Mobile-responsive design (works on phones and tablets)
- Dark/light theme support, defaults to current system setting
- Pause/resume functionality to freeze updates while inspecting data

**The featherweight implementation:**

- Uses only ~1KB of stack and ~0.1% CPU overhead - designed to run alongside your application without impacting performance
- All visualization happens in your browser - the ESP32 just serves JSON data
- Web UI files are embedded in flash memory (no SD card or external storage needed)
- Modern web technologies (Tailwind CSS, Chart.js) loaded via CDN to minimize device component filesize

## 📦Requirements

- ESP-IDF v4.4 or later (FreeRTOS must be enabled, which is the default)
- Your project must have Wi-Fi (or Ethernet) configured and connected before initializing SysMon
- Standard ESP-IDF build system (CMake-based)

> **Important:** SysMon does not handle Wi-Fi setup or authentication - you need to configure and connect Wi-Fi in your own application code before calling `sysmon_init()`. The component will verify that Wi-Fi is connected and has an IP address before starting the HTTP server.

**Required ESP-IDF components:** These are automatically pulled in when you add `sysmon` to your `REQUIRES` list. The component uses `esp_http_server`, `esp_netif`, `esp_wifi`, `esp_partition`, `freertos`, and several other standard ESP-IDF components.

**Required FreeRTOS Configuration:**

SysMon requires two FreeRTOS features to be enabled. These are usually enabled by default, but if you get compilation errors complaining about missing functions, check your `sdkconfig`:

- `CONFIG_FREERTOS_USE_TRACE_FACILITY=y` - Required for `uxTaskGetSystemState()` to enumerate all tasks
- `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y` - Required for CPU usage calculations based on task runtime counters

## 🚀Quick Start

1. **Add to your project:** Copy the `sysmon` directory into your project's `components/` folder.

2. **Update CMakeLists.txt:** Add `sysmon` to the `REQUIRES` list in your main `CMakeLists.txt`:
   
   ```cmake
   idf_component_register(
       ...
       REQUIRES sysmon
   )
   ```

3. **Include headers:** Add these includes where you'll use sysmon:
   
   ```c
   #include "sysmon.h"
   #include "sysmon_stack.h"
   ```

4. **Initialize:** Call `sysmon_init()` after your WiFi is connected and has obtained an IP address. This function will fail if WiFi isn't ready, so make sure you've completed WiFi setup first. Generally you'd call this in your main app initialization function, after `esp_wifi_start()` and waiting for a connection.

5. **Register task stacks (optional but recommended):** For each task you create, immediately after calling `xTaskCreate()` or `xTaskCreatePinnedToCore()`, register its stack size:
   
   ```c
   #define MY_TASK_STACK_SIZE 4096
   TaskHandle_t my_task_handle;
   xTaskCreate(my_task_function, "my_task", MY_TASK_STACK_SIZE, NULL, 5, &my_task_handle);
   sysmon_stack_register(my_task_handle, MY_TASK_STACK_SIZE);  // Same size as passed to xTaskCreate
   ```
   
   This enables percentage-based stack monitoring. See the Stack Monitoring section below for why this matters.

6. **Access the dashboard:** Once initialized, open `http://<your-esp32-ip>:<configured-port>/` in a web browser. The default port is 8080, but can be changed via Kconfig if needed.

If initialization fails, verify that WiFi is connected and has obtained an IP address.

**Example implementation:** See [`example/main/main.c`](example/main/main.c) for a complete working example that demonstrates WiFi setup, SysMon initialization, task creation, and stack registration. This demo app creates several example tasks (CPU load generator, task lifecycle manager, RGB LED controller) and registers them with SysMon to showcase the monitoring capabilities.

## ⚙️Configuration

SysMon uses ESP-IDF's Kconfig system for configuration. Run `idf.py menuconfig` and navigate to **Component config → SysMon Configuration**:

- **HTTP server port** (default: `8080`) - The port number where the web dashboard will be accessible. Make sure this doesn't conflict with other services.
- **CPU sampling interval (ms)** (default: `1000`) - How often the monitor task samples system statistics. Lower values give more frequent updates but use slightly more CPU. 1000ms is usually a good balance.
- **Number of samples in history** (default: `60`) - How many historical data points to keep. With the default 1000ms interval, this gives you the previous full minute of history. More samples = more RAM usage.
- **HTTP control port** (default: `32768`) - Only needed if you're running multiple HTTP servers. Most people can ignore this.

**LWIP Socket Configuration:**

The web dashboard makes multiple concurrent connections when loading (HTML, CSS, JavaScript files, plus API endpoints). The default `CONFIG_LWIP_MAX_SOCKETS=10` may work, but increasing it to at least 16 improves reliability and performance:

1. Run `idf.py menuconfig`
2. Navigate to **Component config → LWIP → Max number of open sockets**
3. Set it to 16 or higher

Alternatively, edit `sdkconfig` directly: `CONFIG_LWIP_MAX_SOCKETS=16`

The build system will warn if `CONFIG_LWIP_MAX_SOCKETS` is too low.

## 🔌Disabling the Component

If you are working on a project where you often need to turn SysMon on or off, for example during development when you want to test your code with and without certain features, you need to make sure your build does not fail when SysMon is removed. This is important because your code might call `sysmon_init()` or `sysmon_stack_register()` even when the component is not present, leading to linker errors.

Here are two common ways to handle this:

1. Add no-op stub functions: Create empty function definitions for the SysMon functions you use. This makes it easy to exclude SysMon from your build without having to search for and remove every call. This approach is especially useful if you frequently toggle SysMon on and off during development.

2. Manually remove or comment out all `sysmon_*` function calls: Go through your code and remove or comment out each call to SysMon functions whenever you want to disable it. This approach gives you a clean build, but it can be tedious if you need to switch SysMon on and off regularly.

Most developers find that option 1 is faster and more convenient when toggling SysMon during development.

**Steps to disable:**

1. Remove `sysmon` from your `CMakeLists.txt` `REQUIRES` list
2. Remove the `sysmon` directory from `components/` (or just don't include it in your build)
3. Remove `#include "sysmon.h"` and `#include "sysmon_stack.h"` from your code, OR
4. Add these no-op stub functions to your code (after removing the includes):

```c
void sysmon_stack_register(TaskHandle_t task_handle, uint32_t stack_size_bytes)
{
    // No-op: sysmon component disabled
    (void)task_handle;
    (void)stack_size_bytes;
}

esp_err_t sysmon_init(void)
{
    // No-op: sysmon component disabled
    return ESP_OK;
}
```

## 📈Stack Monitoring

FreeRTOS tracks stack usage using a metric called the **High Water Mark** (HWM), which represents the smallest amount of free stack space that has been available since the task started running. Think of it like tracking the deepest your stack has ever grown - the "high water mark" shows how close you've come to overflow.

**Important:** FreeRTOS reports stack sizes in **words**, not bytes. On ESP32 (a 32-bit system), one word equals 4 bytes. SysMon converts these values to KB for display.

### Why Register Tasks?

ESP-IDF doesn't provide a way to retrieve the original stack size that was allocated when you created a task. You can get the task name, priority, core assignment, and the high water mark - but not the total stack size you passed to `xTaskCreate()`. This is an ESP-IDF/FreeRTOS limitation, not a bug in SysMon.

This is why `sysmon_stack_register()` exists. By telling SysMon the stack size you used when creating the task, it can calculate percentage-based usage, which is much more useful than raw stack values alone.

**Registered tasks** (where you called `sysmon_stack_register()`):

- Shows remaining stack in bytes
- Shows used stack in bytes  
- Shows used percentage (e.g., "45% used")
- Makes it easy to spot tasks running low on stack

**Unregistered tasks:**

- Only CPU usage is shown; stack usage details are not available
- No stack usage percentage is reported because the original stack size is unknown
- Diagnosing stack issues is much harder - knowing "2KB remaining" alone isn't useful without knowing the total stack allocated (2KB remaining could be almost out of stack, or still plenty left, depending on allocation size)

### Interpreting Stack Usage

To interpret stack usage meaningfully, you need both the total stack size and the HWM. Consider this: a task with a 4KB stack and 2KB remaining (50% used) is in a healthy range, but a task with a 16KB stack and the same 2KB remaining (87.5% used) is at high risk. The absolute remaining stack is the same in both cases, but the risk profile is very different.

**Operational guidelines for a typical 4KB stack allocation:**

- **85–100% used** (remaining stack: less than 600 B) – very high risk; consider higher allocation
- **70–85% used** (600 B – 1.2 KB remaining) – moderate risk; monitor closely
- **50–70% used** (1.2 KB – 2 KB remaining) – healthy range; typically safe
- **Below 50% used** (2 KB+ remaining) – over-provisioned; consider reducing stack allocation

If the HWM approaches zero, stack reallocation is required. Stack overflows are hard to predict and debug, potentially resulting in undefined system behavior. Accurate risk assessment requires all tasks to be registered.

## 📡API Endpoints

The web dashboard uses four JSON API endpoints:

- **`/tasks`** - Returns metadata about all monitored tasks: core assignment, priority levels, stack sizes (for registered tasks), and current stack usage. Relatively static data.

- **`/history`** - Returns time-series data showing how CPU and stack usage has changed over time. Used by the frontend to draw trend charts.

- **`/telemetry`** - Returns current system state: overall CPU usage, per-core CPU usage, current memory statistics (DRAM/PSRAM), and current task usage percentages. Polled frequently for real-time updates.

- **`/hardware`** - Returns static hardware information: chip model and revision, CPU frequency, flash partition table, NVS usage statistics, WiFi connection info, and ESP-IDF version. Typically fetched once when the page loads.

All endpoints return JSON data. The web UI polls `/telemetry` and `/history` at regular intervals. If you're building your own client, you probably want to do the same.

For implementation details, file descriptions, and information about the web server architecture, see [FILES.md](FILES.md).

## 🔗See Also

- Original Arduino implementation: [jameszah/ESP32-Task-Manager](https://github.com/jameszah/ESP32-Task-Manager)
