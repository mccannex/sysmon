/**
 * @file sysmon_json.c
 * @brief JSON creation functions for sysmon HTTP endpoints.
 *
 * This file implements all JSON builder functions used to generate responses
 * for the sysmon HTTP API endpoints.
 */

// Project-specific includes
#include "sysmon_json.h"
#include "sysmon.h"
#include "sysmon_utils.h"

// ESP-IDF includes
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_clk_tree.h"
#include "soc/clk_tree_defs.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "nvs_flash.h"
#include "esp_image_format.h"
#include "cJSON.h"
#include "freertos/task.h"

// System includes
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <time.h>

// Logger tag for this module
static const char *LOG_TAG = "sysmon_json";

// ============================================================================
// Internal Helper Functions (Build Sub-components)
// ============================================================================

/**
 * @brief Determine chip variant string based on model, features, and memory configuration.
 *
 * @param chip_info Chip information structure.
 * @param psram_total Total PSRAM size in bytes (0 if not present).
 * @return Variant string (e.g., "ESP32-S3R8", "ESP32-S3FN8"), or NULL for base model.
 *
 * Note: Embedded flash size cannot be determined programmatically, so variants
 * with embedded flash will show "F" prefix but without size specification.
 */
static const char *_determine_chip_variant(const esp_chip_info_t *chip_info, uint32_t psram_total)
{
    if (chip_info == NULL)
    {
        return NULL;
    }

    // Only ESP32-S3 has multiple variants
    if (chip_info->model != CHIP_ESP32S3)
    {
        return NULL;
    }

    bool has_emb_flash = (chip_info->features & CHIP_FEATURE_EMB_FLASH) != 0;
    bool has_emb_psram = (chip_info->features & CHIP_FEATURE_EMB_PSRAM) != 0;

    // If no embedded flash or PSRAM, it's the base ESP32-S3
    if (!has_emb_flash && !has_emb_psram)
    {
        return NULL;
    }

    // Build variant string
    // Note: We use a static buffer since this is called during JSON creation
    // and the result is immediately copied to JSON
    static char variant_str[32];
    size_t pos = 0;

    // Start with base model
    pos = snprintf(variant_str, sizeof(variant_str), "ESP32-S3");

    // Add embedded flash indicator (F)
    if (has_emb_flash)
    {
        // Note: Embedded flash size cannot be determined programmatically
        // Common sizes are 4MB (H4) and 8MB (N8), but we can't detect which
        variant_str[pos++] = 'F';
    }

    // Add PSRAM size indicator (R2, R8, R16)
    if (has_emb_psram && psram_total > 0)
    {
        // Convert bytes to MB and determine variant suffix
        uint32_t psram_mb = psram_total / (1024 * 1024);
        if (psram_mb == 2)
        {
            pos += snprintf(variant_str + pos, sizeof(variant_str) - pos, "R2");
        }
        else if (psram_mb == 8)
        {
            pos += snprintf(variant_str + pos, sizeof(variant_str) - pos, "R8");
        }
        else if (psram_mb == 16)
        {
            pos += snprintf(variant_str + pos, sizeof(variant_str) - pos, "R16");
        }
        else
        {
            // Unknown PSRAM size, just add the size
            pos += snprintf(variant_str + pos, sizeof(variant_str) - pos, "R%" PRIu32, psram_mb);
        }
    }

    variant_str[pos] = '\0';
    return variant_str;
}

/**
 * @brief Build CPU summary JSON object.
 *
 * @param read_index Index into series arrays for latest sample.
 * @return CPU summary JSON object, or NULL on allocation failure.
 */
static cJSON *_build_cpu_summary(int read_index)
{
    cJSON *cpu = cJSON_CreateObject();
    if (cpu == NULL)
    {
        return NULL;
    }

    // Round CPU overall to 2 decimal places (XX.XX%)
    float overall_raw = self.cpu_overall_percent[read_index];
    double overall_rounded = round(overall_raw * 100.0) / 100.0;
    cJSON_AddNumberToObject(cpu, "overall", overall_rounded);

    cJSON *cores_array = cJSON_CreateArray();
    if (cores_array == NULL)
    {
        JSON_CLEANUP(cpu);
        return NULL;
    }
    // Round CPU core percentages to 2 decimal places (XX.XX%)
    float core0_raw = self.cpu_core_percent[0][read_index];
    float core1_raw = self.cpu_core_percent[1][read_index];
    double core0_rounded = round(core0_raw * 100.0) / 100.0;
    double core1_rounded = round(core1_raw * 100.0) / 100.0;
    cJSON_AddItemToArray(cores_array, cJSON_CreateNumber(core0_rounded));
    cJSON_AddItemToArray(cores_array, cJSON_CreateNumber(core1_rounded));
    cJSON_AddItemToObject(cpu, "cores", cores_array);

    return cpu;
}

/**
 * @brief Build memory summary JSON object.
 *
 * @param read_index Index into series arrays for latest sample.
 * @return Memory summary JSON object, or NULL on allocation failure.
 */
static cJSON *_build_memory_summary(int read_index)
{
    cJSON *mem = cJSON_CreateObject();
    if (mem == NULL)
    {
        return NULL;
    }

    // DRAM stats
    cJSON *dram = cJSON_CreateObject();
    if (dram == NULL)
    {
        JSON_CLEANUP(mem);
        return NULL;
    }
    cJSON_AddNumberToObject(dram, "free", (double)self.dram_free[read_index]);
    cJSON_AddNumberToObject(dram, "largest", (double)self.dram_largest_block[read_index]);
    cJSON_AddNumberToObject(dram, "total", (double)self.dram_total[read_index]);
    cJSON_AddNumberToObject(dram, "usedPct", (double)self.dram_used_percent[read_index]);
    cJSON_AddItemToObject(mem, "dram", dram);

    // PSRAM stats
    cJSON *psram = cJSON_CreateObject();
    if (psram == NULL)
    {
        JSON_CLEANUP(mem);
        return NULL;
    }
    cJSON_AddNumberToObject(psram, "free", (double)self.psram_free[read_index]);
    cJSON_AddNumberToObject(psram, "total", (double)self.psram_total[read_index]);
    cJSON_AddNumberToObject(psram, "usedPct", (double)self.psram_used_percent[read_index]);
    cJSON_AddBoolToObject(psram, "present", self.psram_seen);
    cJSON_AddItemToObject(mem, "psram", psram);

    return mem;
}

/**
 * @brief Get usage statistics for a partition based on its type.
 *
 * @param part Partition to get stats for.
 * @param used_bytes Output parameter for used bytes (0 if unavailable).
 * @param free_bytes Output parameter for free bytes (0 if unavailable).
 * @return true if usage stats are available, false otherwise.
 *
 * Details:
 *   - For NVS partitions: Uses nvs_get_stats() to get actual usage.
 *   - For App partitions: Assumes fully used (contains firmware).
 *   - For other partition types: Returns false (stats not available).
 */
static bool _get_partition_usage(const esp_partition_t *part, 
                                 uint32_t *used_bytes, 
                                 uint32_t *free_bytes)
{
    if (part == NULL || used_bytes == NULL || free_bytes == NULL)
    {
        return false;
    }

    *used_bytes = 0;
    *free_bytes = 0;

    // NVS partitions - can get actual usage stats
    if (part->type == ESP_PARTITION_TYPE_DATA && 
        part->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS)
    {
        nvs_stats_t nvs_stats;
        esp_err_t err = nvs_get_stats(part->label, &nvs_stats);
        if (err == ESP_OK)
        {
            // NVS doesn't directly give bytes, but we can estimate
            // Each entry has overhead, so we calculate based on entries
            // This is approximate - NVS has variable entry sizes
            uint32_t total_entries = nvs_stats.used_entries + nvs_stats.free_entries;
            if (total_entries > 0)
            {
                // Estimate: used entries / total entries * partition size
                *used_bytes = (uint32_t)((double)nvs_stats.used_entries / 
                                        (double)total_entries * part->size);
                *free_bytes = part->size - *used_bytes;
            }
            else
            {
                *free_bytes = part->size;
            }
            return true;
        }
        else
        {
            ESP_LOGW(LOG_TAG, "nvs_get_stats() failed for partition '%s': %s (0x%x). Usage stats unavailable.", 
                     part->label, esp_err_to_name(err), err);
        }
    }

    // App partitions - read actual image size from image header
    if (part->type == ESP_PARTITION_TYPE_APP)
    {
        // Read image header to verify it's a valid app image
        esp_image_header_t image_header;
        esp_err_t err = esp_flash_read(NULL, &image_header, part->address, sizeof(esp_image_header_t));
        if (err == ESP_OK && image_header.magic == ESP_IMAGE_HEADER_MAGIC)
        {
            // Calculate image size by reading all segment headers sequentially
            uint32_t image_size = sizeof(esp_image_header_t);
            uint8_t segment_count = image_header.segment_count;
            uint32_t current_offset = sizeof(esp_image_header_t);
            
            // Read each segment header and sum data lengths
            for (uint8_t i = 0; i < segment_count; i++)
            {
                esp_image_segment_header_t seg_header;
                err = esp_flash_read(NULL, &seg_header, part->address + current_offset, 
                                    sizeof(esp_image_segment_header_t));
                if (err != ESP_OK)
                
                // Use PRIx32 for current_offset because it is a uint32_t and must be printed with the correct format
                // specifier for portability across different platforms/architectures (see inttypes.h).
                {
                    ESP_LOGW(LOG_TAG, "esp_flash_read() failed for partition '%s' at offset 0x%" PRIx32 ": %s (0x%x). Using fallback size calculation.", 
                             part->label, current_offset, esp_err_to_name(err), err);
                    break;
                }
                
                // Add segment header size
                image_size += sizeof(esp_image_segment_header_t);
                
                // Add segment data length (aligned to 4 bytes)
                uint32_t data_len = seg_header.data_len;
                if (data_len % 4 != 0)
                {
                    data_len = (data_len + 3) & ~3; // Align to 4 bytes
                }
                image_size += data_len;
                
                // Move to next segment header
                current_offset += sizeof(esp_image_segment_header_t) + data_len;
            }
            
            // Add app description size (typically 32 bytes at end of image)
            // Using constant size since esp_app_desc_t may not be available in all ESP-IDF versions
            image_size += 32; // sizeof(esp_app_desc_t) is typically 32 bytes
            
            // Ensure we don't exceed partition size
            if (image_size > part->size)
            {
                image_size = part->size;
            }
            
            *used_bytes = image_size;
            *free_bytes = part->size - image_size;
            return true;
        }
        
        // Fallback: if we can't read the header, assume fully used
        // This is safer than showing incorrect free space
        *used_bytes = part->size;
        *free_bytes = 0;
        return true;
    }

    // Other partition types - usage stats not available
    return false;
}

/**
 * @brief Build partitions JSON array.
 *
 * @return Partitions JSON array, or NULL on allocation failure.
 *
 * Details:
 *   - Enumerates all partitions in the partition table.
 *   - Includes label, type, subtype, address, and size for each partition.
 *   - Includes usage statistics (used, free, usedPct) when available.
 *   - All allocations checked for robustness.
 */
static cJSON *_build_partitions_json(void)
{
    cJSON *partitions = cJSON_CreateArray();
    if (partitions == NULL)
    {
        return NULL;
    }

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, 
                                                     ESP_PARTITION_SUBTYPE_ANY, 
                                                     NULL);
    if (it == NULL)
    {
        // No partitions found, return empty array
        return partitions;
    }

    while (it != NULL)
    {
        const esp_partition_t *part = esp_partition_get(it);
        if (part == NULL)
        {
            it = esp_partition_next(it);
            continue;
        }

        // Skip system partitions that don't need to be displayed
        // phy_init is a system partition for PHY initialization data
        // Note: part->label is a char array, not a pointer, so we can compare directly
        if (strcmp(part->label, "phy_init") == 0)
        {
            it = esp_partition_next(it);
            continue;
        }

        cJSON *part_obj = cJSON_CreateObject();
        if (part_obj == NULL)
        {
            esp_partition_iterator_release(it);
            JSON_CLEANUP(partitions);
            return NULL;
        }

        cJSON_AddStringToObject(part_obj, "label", part->label);
        cJSON_AddNumberToObject(part_obj, "type", (double)part->type);
        cJSON_AddNumberToObject(part_obj, "address", (double)part->address);
        cJSON_AddNumberToObject(part_obj, "size", (double)part->size);

        // Get usage statistics if available
        uint32_t used_bytes = 0;
        uint32_t free_bytes = 0;
        bool usage_available = _get_partition_usage(part, &used_bytes, &free_bytes);
        
        cJSON_AddBoolToObject(part_obj, "usageAvailable", usage_available);
        if (usage_available)
        {
            cJSON_AddNumberToObject(part_obj, "used", (double)used_bytes);
            cJSON_AddNumberToObject(part_obj, "free", (double)free_bytes);
            if (part->size > 0)
            {
                double used_pct = ((double)used_bytes / (double)part->size) * 100.0;
                cJSON_AddNumberToObject(part_obj, "usedPct", used_pct);
            }
            else
            {
                cJSON_AddNumberToObject(part_obj, "usedPct", 0.0);
            }
        }

        cJSON_AddItemToArray(partitions, part_obj);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    return partitions;
}

/**
 * @brief Build current task usage JSON object.
 *
 * @return Current task usage JSON object, or NULL on allocation failure.
 */
static cJSON *_build_current_task_usage(void)
{
    cJSON *current = cJSON_CreateObject();
    if (current == NULL)
    {
        return NULL;
    }

    for (int i = 0; i < self.task_capacity; i++)
    {
        if (!self.tasks || !self.tasks[i].is_active)
        {
            continue;
        }

        int read_index = (self.tasks[i].write_index - 1 + CONFIG_SYSMON_SAMPLE_COUNT) % CONFIG_SYSMON_SAMPLE_COUNT;
        cJSON *task_obj = cJSON_CreateObject();
        if (task_obj == NULL)
        {
            JSON_CLEANUP(current);
            return NULL;
        }
        // Round CPU usage to 2 decimal places (XX.XX%)
        float cpu_raw = self.tasks[i].usage_percent_history[read_index];
        double cpu_rounded = round(cpu_raw * 100.0) / 100.0;
        cJSON_AddNumberToObject(task_obj, "cpu", cpu_rounded);

        double stack_bytes = (double)self.tasks[i].stack_usage_bytes_history[read_index];
        double stack_pct   = (double)self.tasks[i].stack_usage_percent_history[read_index];
        cJSON_AddNumberToObject(task_obj, "stack", stack_bytes);
        cJSON_AddNumberToObject(task_obj, "stackPct", stack_pct);

        // Only include stackRemaining if stack & stackPct are nonzero
        if (stack_bytes > 0.0 && stack_pct > 0.0)
        {
            uint32_t stack_remaining_bytes = self.tasks[i].stack_high_water_mark * sizeof(StackType_t);
            cJSON_AddNumberToObject(task_obj, "stackRemaining", (double)stack_remaining_bytes);
        }

        // Use display name for JSON key (renames "main" to "app_main")
        const char *display_name = _get_task_display_name(self.tasks[i].task_name);
        cJSON_AddItemToObject(current, display_name, task_obj);
    }

    return current;
}

// ============================================================================
// Public API Functions (Endpoint Handlers)
// ============================================================================

/**
 * @brief Build task metadata JSON object for all monitored tasks.
 *
 * @return Pointer to root cJSON object (must be freed by caller), or NULL on oom.
 *
 * Details:
 *   - Iterates over all known tasks, skipping inactive or missing entries.
 *   - For each active task, emits static task metadata: core, priority, stack sizes.
 *   - Top-level dictionary keys are task names, values are per-task metadata objects.
 *   - Fails safely on allocation errors—frees all partial data.
 */
cJSON *_create_tasks_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return NULL;
    }

    for (int i = 0; i < self.task_capacity; i++)
    {
        // Defensive skip for inactive or missing tasks.
        if (!self.tasks || !self.tasks[i].is_active)
        {
            continue;
        }

        cJSON *task_obj = cJSON_CreateObject();
        if (task_obj == NULL)
        {
            JSON_CLEANUP(root);
            return NULL;
        }

        int read_index = (self.tasks[i].write_index - 1 + CONFIG_SYSMON_SAMPLE_COUNT) % CONFIG_SYSMON_SAMPLE_COUNT;

        cJSON_AddNumberToObject(task_obj, "core", self.tasks[i].core_id);
        cJSON_AddNumberToObject(task_obj, "prio", (double)self.tasks[i].current_priority);
        cJSON_AddNumberToObject(task_obj, "stackSize", (double)self.tasks[i].stack_size_bytes);

        double stack_bytes = (double)self.tasks[i].stack_usage_bytes_history[read_index];
        double stack_pct   = (double)self.tasks[i].stack_usage_percent_history[read_index];

        cJSON_AddNumberToObject(task_obj, "stackUsed", stack_bytes);
        cJSON_AddNumberToObject(task_obj, "stackUsedPct", stack_pct);

        // Only include stackRemaining if stack & stackPct are nonzero
        if (stack_bytes > 0.0 && stack_pct > 0.0)
        {
            uint32_t stack_remaining_bytes = self.tasks[i].stack_high_water_mark * sizeof(StackType_t);
            cJSON_AddNumberToObject(task_obj, "stackRemaining", (double)stack_remaining_bytes);
        }

        // Use display name for JSON key (renames "main" to "app_main")
        const char *display_name = _get_task_display_name(self.tasks[i].task_name);
        cJSON_AddItemToObject(root, display_name, task_obj);
    }

    return root;
}

/**
 * @brief Build JSON object tracing task usage history for all monitored tasks.
 *
 * @return Root cJSON object, or NULL on allocation failure.
 *
 * Details:
 *   - Each key (task name) maps to an object with "cpu" and "stack" arrays.
 *   - "cpu" array contains CPU usage percent samples over time (rounded to 1 decimal place).
 *   - "stack" array contains stack usage in bytes samples over time (only for registered tasks).
 *   - Only active, known tasks included.
 *   - Array order is oldest-to-newest based on cyclic buffer logic.
 *   - All allocations checked for robustness/low-memory resilience.
 */
cJSON *_create_history_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return NULL;
    }

    for (int i = 0; i < self.task_capacity; i++)
    {
        if (!self.tasks || !self.tasks[i].is_active)
        {
            continue;
        }

        cJSON *task_obj = cJSON_CreateObject();
        if (task_obj == NULL)
        {
            JSON_CLEANUP(root);
            return NULL;
        }

        // CPU history array
        cJSON *cpu_array = cJSON_CreateArray();
        if (cpu_array == NULL)
        {
            JSON_CLEANUP(task_obj, root);
            return NULL;
        }

        // Stack history array (only for registered tasks)
        cJSON *stack_array = NULL;
        bool is_registered = (self.tasks[i].stack_size_bytes > 0U);
        if (is_registered)
        {
            stack_array = cJSON_CreateArray();
            if (stack_array == NULL)
            {
                JSON_CLEANUP(cpu_array, task_obj, root);
                return NULL;
            }
        }

        // Start from current write index (oldest sample).
        int read_index = self.tasks[i].write_index;
        for (int j = 0; j < CONFIG_SYSMON_SAMPLE_COUNT; j++)
        {
            // Round CPU usage to 1 decimal place to reduce JSON size
            float cpu_raw = self.tasks[i].usage_percent_history[read_index];
            double cpu_rounded = round(cpu_raw * 10.0) / 10.0;
            cJSON *cpu_value = cJSON_CreateNumber(cpu_rounded);
            if (cpu_value == NULL)
            {
                JSON_CLEANUP(cpu_array, stack_array, task_obj, root);
                return NULL;
            }
            cJSON_AddItemToArray(cpu_array, cpu_value);

            // Only generate stack history for registered tasks
            if (is_registered)
            {
                uint32_t stack_value_bytes = self.tasks[i].stack_usage_bytes_history[read_index];
                cJSON *stack_value = cJSON_CreateNumber((double)stack_value_bytes);
                if (stack_value == NULL)
                {
                    JSON_CLEANUP(cpu_array, stack_array, task_obj, root);
                    return NULL;
                }
                cJSON_AddItemToArray(stack_array, stack_value);
            }

            read_index = (read_index + 1) % CONFIG_SYSMON_SAMPLE_COUNT;
        }

        cJSON_AddItemToObject(task_obj, "cpu", cpu_array);
        if (is_registered && stack_array != NULL)
        {
            cJSON_AddItemToObject(task_obj, "stack", stack_array);
        }
        // Use display name for JSON key (renames "main" to "app_main")
        const char *display_name = _get_task_display_name(self.tasks[i].task_name);
        cJSON_AddItemToObject(root, display_name, task_obj);
    }

    return root;
}

/**
 * @brief Build a complete telemetry JSON object summarizing CPU/memory and current registered task usage.
 *
 * @return Root cJSON object, or NULL on allocation failure.
 *
 * Details:
 *   - Produces a two-level structure:
 *       root->summary: {cpu, mem}, root->current: {task current usages}
 *   - 'cpu' includes overall percent + per-core array.
 *   - 'mem' summary embeds DRAM and (if present) PSRAM details.
 *   - Defensive allocation checks propagate errors cleanly upward.
 */
cJSON *_create_telemetry_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return NULL;
    }

    int read_index = (self.series_write_index - 1 + CONFIG_SYSMON_SAMPLE_COUNT) % CONFIG_SYSMON_SAMPLE_COUNT;

    // Summary object
    cJSON *summary = cJSON_CreateObject();
    if (summary == NULL)
    {
        JSON_CLEANUP(root);
        return NULL;
    }

    cJSON *cpu = _build_cpu_summary(read_index);
    if (cpu == NULL)
    {
        JSON_CLEANUP(summary, root);
        return NULL;
    }
    cJSON_AddItemToObject(summary, "cpu", cpu);

    cJSON *mem = _build_memory_summary(read_index);
    if (mem == NULL)
    {
        JSON_CLEANUP(summary, root);
        return NULL;
    }
    cJSON_AddItemToObject(summary, "mem", mem);

    // WiFi RSSI (signal strength)
    int8_t rssi = 0;
    esp_err_t rssi_err = _get_wifi_rssi(&rssi);
    if (rssi_err == ESP_OK)
    {
        cJSON_AddNumberToObject(summary, "wifiRssi", (double)rssi);
    }
    else
    {
        cJSON_AddNullToObject(summary, "wifiRssi");
    }

    cJSON_AddItemToObject(root, "summary", summary);

    // Current task usage
    cJSON *current = _build_current_task_usage();
    if (current == NULL)
    {
        JSON_CLEANUP(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "current", current);

    return root;
}

/**
 * @brief Create hardware information JSON object with static chip and system info.
 *
 * @return Hardware info JSON object, or NULL on allocation failure.
 *
 * Details:
 *   - Retrieves static hardware information that doesn't change during execution.
 *   - Includes chip model, revision, cores, features.
 *   - Includes ESP-IDF version, build info, memory totals.
 *   - All allocations checked for robustness.
 */
cJSON *_create_hardware_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return NULL;
    }

    // Chip information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    cJSON *chip = cJSON_CreateObject();
    if (chip == NULL)
    {
        JSON_CLEANUP(root);
        return NULL;
    }

    // Chip model string
    const char *model_str = "Unknown";
    switch (chip_info.model)
    {
        case CHIP_ESP32:
            model_str = "ESP32";
            break;
        case CHIP_ESP32S2:
            model_str = "ESP32-S2";
            break;
        case CHIP_ESP32S3:
            model_str = "ESP32-S3";
            break;
        case CHIP_ESP32C3:
            model_str = "ESP32-C3";
            break;
        case CHIP_ESP32C2:
            model_str = "ESP32-C2";
            break;
        case CHIP_ESP32C6:
            model_str = "ESP32-C6";
            break;
        case CHIP_ESP32H2:
            model_str = "ESP32-H2";
            break;
        case CHIP_ESP32P4:
            model_str = "ESP32-P4";
            break;
        case CHIP_ESP32C61:
            model_str = "ESP32-C61";
            break;
        case CHIP_ESP32C5:
            model_str = "ESP32-C5";
            break;
        case CHIP_POSIX_LINUX:
            model_str = "POSIX-Linux";
            break;
        default:
            model_str = "Unknown";
            break;
    }

    cJSON_AddStringToObject(chip, "model", model_str);
    cJSON_AddNumberToObject(chip, "revision", (double)chip_info.revision);
    cJSON_AddNumberToObject(chip, "cores", chip_info.cores);
    
    // Get PSRAM size for variant determination (needed before variant check)
    uint32_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    // Determine chip variant (e.g., ESP32-S3R8, ESP32-S3FN8)
    const char *variant_str = _determine_chip_variant(&chip_info, psram_total);
    if (variant_str != NULL)
    {
        cJSON_AddStringToObject(chip, "variant", variant_str);
    }
    
    // Get current CPU frequency in MHz using ESP-IDF 5+ clock tree API
    // According to ESP-IDF 5+ docs: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/clk_tree.html
    uint32_t cpu_freq_hz = 0;
    esp_err_t freq_err = esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &cpu_freq_hz);
    uint32_t cpu_freq_mhz = 0;
    if (freq_err == ESP_OK && cpu_freq_hz > 0)
    {
        cpu_freq_mhz = cpu_freq_hz / 1000000;
    }
    cJSON_AddNumberToObject(chip, "cpuFreqMHz", (double)cpu_freq_mhz);

    // Chip features
    cJSON *features = cJSON_CreateArray();
    if (features == NULL)
    {
        JSON_CLEANUP(chip, root);
        return NULL;
    }

    if (chip_info.features & CHIP_FEATURE_EMB_FLASH)
    {
        cJSON_AddItemToArray(features, cJSON_CreateString("Embedded Flash"));
    }
    if (chip_info.features & CHIP_FEATURE_WIFI_BGN)
    {
        cJSON_AddItemToArray(features, cJSON_CreateString("WiFi 2.4GHz"));
    }
    if (chip_info.features & CHIP_FEATURE_BLE)
    {
        cJSON_AddItemToArray(features, cJSON_CreateString("Bluetooth LE"));
    }
    if (chip_info.features & CHIP_FEATURE_BT)
    {
        cJSON_AddItemToArray(features, cJSON_CreateString("Bluetooth Classic"));
    }
    if (chip_info.features & CHIP_FEATURE_IEEE802154)
    {
        cJSON_AddItemToArray(features, cJSON_CreateString("IEEE 802.15.4"));
    }
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM)
    {
        cJSON_AddItemToArray(features, cJSON_CreateString("Embedded PSRAM"));
    }

    cJSON_AddItemToObject(chip, "features", features);
    cJSON_AddItemToObject(root, "chip", chip);

    // Memory information (totals are static)
    cJSON *memory = cJSON_CreateObject();
    if (memory == NULL)
    {
        JSON_CLEANUP(root);
        return NULL;
    }

    uint32_t dram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    cJSON_AddNumberToObject(memory, "dramTotal", (double)dram_total);

    // PSRAM size already retrieved earlier for variant determination, reuse it
    if (psram_total > 0U)
    {
        cJSON_AddNumberToObject(memory, "psramTotal", (double)psram_total);

        // PSRAM speed from config (static value)
        // CONFIG_SPIRAM_SPEED is only available when PSRAM is enabled in sdkconfig
        // The #ifdef guard is intentional - this config option may not exist if PSRAM is disabled
#ifdef CONFIG_SPIRAM_SPEED
        cJSON_AddNumberToObject(memory, "psramSpeed", (double)CONFIG_SPIRAM_SPEED);
#endif
    }
    else
    {
        cJSON_AddNumberToObject(memory, "psramTotal", 0);
    }

    cJSON_AddItemToObject(root, "memory", memory);

    // System information
    cJSON *system = cJSON_CreateObject();
    if (system == NULL)
    {
        JSON_CLEANUP(root);
        return NULL;
    }

    const char *idf_version = esp_get_idf_version();
    cJSON_AddStringToObject(system, "idfVersion", idf_version);

    // Compile time
    char compile_time[64];
    snprintf(compile_time, sizeof(compile_time), "%s %s", __DATE__, __TIME__);
    cJSON_AddStringToObject(system, "compileTime", compile_time);

    // Boot time - show current date/time as ESP32 sees it
    // Format matches compile time: "MMM DD YYYY HH:MM:SS" (e.g., "Nov 11 2025 02:17:56")
    time_t now = time(NULL);
    char boot_time_str[64];
    if (now > 0)
    {
        struct tm timeinfo;
        if (localtime_r(&now, &timeinfo) != NULL)
        {
            strftime(boot_time_str, sizeof(boot_time_str), "%b %d %Y %H:%M:%S", &timeinfo);
        }
        else
        {
            snprintf(boot_time_str, sizeof(boot_time_str), "Time not available");
        }
    }
    else
    {
        snprintf(boot_time_str, sizeof(boot_time_str), "Time not set");
    }
    cJSON_AddStringToObject(system, "bootTime", boot_time_str);

    cJSON_AddItemToObject(root, "system", system);

    // Partition information
    cJSON *partitions = _build_partitions_json();
    if (partitions == NULL)
    {
        JSON_CLEANUP(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "partitions", partitions);

    // Calculate flash summary (total flash size and unused space)
    uint32_t total_flash_size = 0;
    esp_err_t flash_ret = esp_flash_get_size(NULL, &total_flash_size);
    if (flash_ret != ESP_OK)
    {
        ESP_LOGW(LOG_TAG, "esp_flash_get_size() failed: %s (0x%x). Flash summary unavailable.", 
                 esp_err_to_name(flash_ret), flash_ret);
        total_flash_size = 0;
    }
    if (total_flash_size > 0)
    {
        // Calculate total partition size
        uint32_t total_partition_size = 0;
        size_t partition_count = cJSON_GetArraySize(partitions);
        for (size_t i = 0; i < partition_count; i++)
        {
            cJSON *part_obj = cJSON_GetArrayItem(partitions, i);
            if (part_obj != NULL)
            {
                cJSON *size_obj = cJSON_GetObjectItem(part_obj, "size");
                if (size_obj != NULL && cJSON_IsNumber(size_obj))
                {
                    total_partition_size += (uint32_t)cJSON_GetNumberValue(size_obj);
                }
            }
        }

        uint32_t unused_flash = total_flash_size - total_partition_size;
        
        cJSON *flash_summary = cJSON_CreateObject();
        if (flash_summary != NULL)
        {
            cJSON_AddNumberToObject(flash_summary, "totalFlash", (double)total_flash_size);
            cJSON_AddNumberToObject(flash_summary, "totalPartitions", (double)total_partition_size);
            cJSON_AddNumberToObject(flash_summary, "unused", (double)unused_flash);
            if (total_flash_size > 0)
            {
                double unused_pct = ((double)unused_flash / (double)total_flash_size) * 100.0;
                double partitions_pct = ((double)total_partition_size / (double)total_flash_size) * 100.0;
                cJSON_AddNumberToObject(flash_summary, "unusedPct", unused_pct);
                cJSON_AddNumberToObject(flash_summary, "partitionsPct", partitions_pct);
            }
            else
            {
                cJSON_AddNumberToObject(flash_summary, "unusedPct", 0.0);
                cJSON_AddNumberToObject(flash_summary, "partitionsPct", 0.0);
            }
            cJSON_AddItemToObject(root, "flashSummary", flash_summary);
        }
    }

    // WiFi information
    cJSON *wifi = cJSON_CreateObject();
    if (wifi == NULL)
    {
        // If WiFi object creation fails, continue without it
        // Don't fail the entire hardware JSON response
    }
    else
    {
        // Get WiFi SSID
        char ssid_buffer[33] = { 0 };
        esp_err_t ssid_err = _get_wifi_ssid(ssid_buffer, sizeof(ssid_buffer));
        if (ssid_err == ESP_OK)
        {
            cJSON_AddStringToObject(wifi, "ssid", ssid_buffer);
        }
        else
        {
            cJSON_AddStringToObject(wifi, "ssid", "Not Connected");
        }

        // Get WiFi RSSI
        int8_t rssi = 0;
        esp_err_t rssi_err = _get_wifi_rssi(&rssi);
        if (rssi_err == ESP_OK)
        {
            cJSON_AddNumberToObject(wifi, "rssi", (double)rssi);
        }
        else
        {
            cJSON_AddNullToObject(wifi, "rssi");
        }

        // Get WiFi IP address
        char ip_buffer[16] = { 0 };
        esp_err_t ip_err = _get_wifi_ip_info(ip_buffer, sizeof(ip_buffer));
        if (ip_err == ESP_OK)
        {
            cJSON_AddStringToObject(wifi, "ip", ip_buffer);
        }
        else
        {
            cJSON_AddStringToObject(wifi, "ip", "N/A");
        }

        // HTTP server port
        cJSON_AddNumberToObject(wifi, "port", (double)CONFIG_SYSMON_HTTPD_SERVER_PORT);

        cJSON_AddItemToObject(root, "wifi", wifi);
    }

    // Configuration section for frontend
    cJSON *config = cJSON_CreateObject();
    if (config != NULL)
    {
        cJSON_AddNumberToObject(config, "cpuSamplingIntervalMs", (double)CONFIG_SYSMON_CPU_SAMPLING_INTERVAL_MS);
        cJSON_AddNumberToObject(config, "sampleCount", (double)CONFIG_SYSMON_SAMPLE_COUNT);
        cJSON_AddItemToObject(root, "config", config);
    }

    return root;
}
