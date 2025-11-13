// API and networking constants
const API_ROUTES = {
  HISTORY   : '/history',
  TELEMETRY : '/telemetry',
  TASKS     : '/tasks',
  HARDWARE  : '/hardware'
};

const TELEMETRY_TIMEOUT_MS = 4000;

// Chart configuration constants (defaults, will be overridden from hardware endpoint)
let CHART_SAMPLE_COUNT                  = 100;
let CHART_TELEMETRY_UPDATE_INTERVAL_MS  = 1000;
const CHART_TASK_TABLE_UPDATE_INTERVAL_MS = 10000;

/**
 * Get chart configuration with theme-aware colors.
 *
 * Returns a merged configuration object that combines static chart configuration
 * values with theme-specific colors based on the current theme mode.
 * Configuration is organized into nested sub-objects following Chart.js patterns:
 * ELEMENTS (line/point), PLUGINS (tooltip/legend), FONT, and COLORS.
 * 
 * COLORS contains:
 * - DATASET_PALETTE: Array of colors for data series (theme-agnostic)
 * - UI: Theme-aware UI colors (grid, text, tooltip, etc.) for current theme
 *
 * This function serves as the single source of truth for all chart configuration.
 * The current theme (dark/light) is automatically detected from the document.
 *
 * @returns {Object} Merged configuration object with nested ELEMENTS, PLUGINS, FONT, and COLORS objects.
 */
function getChartConfig()
{
  const isDark = document.documentElement.classList.contains('dark');
  
  // Chart UI theme colors - normalized to hex notation with alpha channel (#RRGGBBAA format)
  const CHART_THEME_COLORS = {
    dark: {
      gridColor     : '#9ca3af33', // gray-400 with 20% opacity
      gridColorMajor: '#9ca3af66', // gray-400 with 40% opacity
      textColor     : '#e5e7eb',   // gray-100
      tooltipBg     : '#1f2937f2', // gray-800 with 95% opacity
      tooltipText   : '#e5e7eb',   // gray-100
      hoverLine     : '#3b82f633'  // blue-500 with 20% opacity
    },
    light: {
      gridColor     : '#0000001a', // black with 10% opacity
      gridColorMajor: '#00000040', // black with 25% opacity
      textColor     : '#111827',   // gray-900
      tooltipBg     : '#fffffff2', // white with 95% opacity
      tooltipText   : '#111827',   // gray-900
      hoverLine     : '#3b82f633'  // blue-500 with 20% opacity
    }
  };
  
  const themeColors = isDark ? CHART_THEME_COLORS.dark : CHART_THEME_COLORS.light;
  
  return {
    
    // Element configuration (matches Chart.js options.elements structure)
    ELEMENTS: {
      LINE: {
        BORDER_WIDTH : 2,
        TENSION      : 0.4
      },
      POINT: {
        RADIUS       : 0,
        HOVER_RADIUS : 4
      }
    },
    
    // Plugin configuration (matches Chart.js options.plugins structure)
    PLUGINS: {
      TOOLTIP: {
        BORDER_WIDTH  : 1,
        PADDING       : 8,
        CARET_PADDING : 5,
        BOX_PADDING   : 6
      },
      LEGEND: {
        BOX_WIDTH   : 12,
        BOX_HEIGHT  : 12,
        BOX_PADDING : 5
      }
    },
    
    // Font configuration (matches Chart.js defaults.font structure)
    FONT: {
      FAMILY      : "ui-sans-serif, system-ui, sans-serif, 'Apple Color Emoji', 'Segoe UI Emoji', 'Segoe UI Symbol', 'Noto Color Emoji'",
      SIZE        : 13,
      LINE_HEIGHT : 1.4
    },
    
    // All chart colors organized in COLORS sub-object
    COLORS: {
      // Dataset palette - colors for data series (theme-agnostic, work in both light/dark)
      // Using Tailwind colors in 400-600 range for good visibility in both themes
      DATASET_PALETTE: [
        '#3b82f6', // blue-500
        '#10b981', // emerald-500
        '#f59e0b', // amber-500
        '#ef4444', // red-500
        '#8b5cf6', // violet-500
        '#06b6d4', // cyan-500
        '#ec4899', // pink-500
        '#14b8a6', // teal-500
        '#f97316', // orange-500
        '#6366f1', // indigo-500
        '#84cc16', // lime-500
        '#a855f7', // purple-500
        '#22d3ee', // cyan-400
        '#fb7185', // rose-400
        '#34d399', // emerald-400
        '#60a5fa'  // blue-400
      ],
      
      // UI colors - theme-aware colors for chart infrastructure (resolved for current theme)
      UI: {
        GRID_COLOR       : themeColors.gridColor,
        GRID_COLOR_MAJOR : themeColors.gridColorMajor,
        TEXT_COLOR       : themeColors.textColor,
        TOOLTIP_BG       : themeColors.tooltipBg,
        TOOLTIP_TEXT     : themeColors.tooltipText,
        HOVER_LINE       : themeColors.hoverLine,
        HOVER_LINE_WIDTH : 2
      }
    }
  };
}

// Thresholds and limits
const STACK_WARNING_THRESHOLD_BYTES = 1024; // bytes (equivalent to 256 words * 4 bytes/word)
const STACK_PERCENTAGE_WARNING      = 80;   // Threshold for highlighting high stack usage
const INVALID_CORE_VALUE            = 2147483647;
const BYTES_PER_KB                  = 1024;

// Material Icons - centralized icon name management
const MATERIAL_ICONS = {
  DARK_MODE  : 'dark_mode',
  LIGHT_MODE : 'light_mode',
  PLAY_ARROW : 'play_arrow',
  PAUSE      : 'pause'
};

// System task descriptions for hover tooltips
const SYSTEM_TASKS = {
  'IDLE0'    : 'Idle task for Core 0. Runs when Core 0 has no other tasks. Handles watchdog feeding and power management hooks.',
  'IDLE1'    : 'Idle task for Core 1. Runs when Core 1 has no other tasks. Similar to IDLE0 but for the second core.',
  'tiT'      : 'TCP/IP stack task (LwIP). Main task of the LwIP TCP/IP stack. Processes TCP/IP packets and network protocol handling.',
  'Tmr Svc'  : 'FreeRTOS Timer Service Task. Processes FreeRTOS software timers. Created when timer APIs are used.',
  'ipc0'     : 'Inter-Processor Call task for Core 0. Handles inter-core communication for Core 0. Allows tasks to call functions on the other core.',
  'ipc1'     : 'Inter-Processor Call task for Core 1. Handles inter-core communication for Core 1. Similar to ipc0 but for the second core.',
  'wifi'     : 'Wi-Fi driver task. Handles Wi-Fi hardware driver and stack. Processes Wi-Fi events, data packets, and management operations.',
  'esp_timer': 'ESP Timer task. High-resolution timer task. Processes ESP timer callbacks for precise timing events.',
  'httpd'    : 'HTTP server task. Handles HTTP requests and responses from ESP-IDF HTTP server component.',
  'sys_evt'  : 'System event loop task. Processes system events (Wi-Fi, network, etc.). Created by esp_event_loop_create_default().'
};

// ESP-IDF default partition descriptions for hover tooltips
const PARTITION_DESCRIPTIONS = {
  'nvs'      : 'NVS (Non-Volatile Storage) partition. Stores key-value pairs in flash. Used by the NVS library for persistent configuration data.',
  'otadata'  : 'OTA data partition. Stores information about which OTA partition to boot. The bootloader consults this to determine which app partition to execute.',
  'phy_init' : 'PHY initialization data partition. Stores RF calibration data for Wi-Fi and Bluetooth. Contains PHY initialization parameters.',
  'factory'  : 'Factory app partition. The default application partition that boots if OTA data is empty or invalid. Typically located at offset 0x10000.',
  'ota_0'    : 'OTA partition 0. Used for over-the-air firmware updates. One of two OTA app partitions that can be swapped during updates.',
  'ota_1'    : 'OTA partition 1. Used for over-the-air firmware updates. One of two OTA app partitions that can be swapped during updates.',
  'nvs_key'  : 'NVS encryption keys partition. Stores encryption keys used by the NVS library when flash encryption is enabled.'
};

// Application state (keep at end as it depends on above constants)
const AppState = {
  charts: {
    cpu   : null,  // Chart.js instance for CPU
    memory: null   // Chart.js instance for Memory
  },
  filters: {
    hideLowUsage    : true, // Whether to hide low-utilization datasets
    thresholdPercent: 0.05,  // Threshold (%) for low utilization
    hideSystemTasks : true   // Whether to hide system task datasets
  },
  data: {
    registeredTasks: new Set(), // Set of registered task names (those with known stack sizes)
    taskInfo       : {},         // Cached task info data for calculating percentages
    lastTelemetryTaskNames: new Set() // Track task names from last telemetry to detect changes
  },
  ui: {
    tableSorter: {
      tasks     : null,  // Tablesort instance for task table
      partitions: null   // Tablesort instance for partitions table
    },
    isHoveringCpu    : false, // True when mouse is over CPU chart
    isHoveringMemory : false, // True when mouse is over memory chart
    isPaused         : false  // True when updates are paused
  },
  status: {
    lastTelemetrySuccess: null,  // Timestamp of last successful telemetry fetch
    lastTableSuccess    : null,  // Timestamp of last successful table fetch
    consecutiveFailures : 0,     // Count of consecutive API failures
    currentStatus       : null   // Current status message type
  }
};

// Status popup constants (keep at end as they don't affect state)
const STATUS_TYPES = {
  NONE         : null,
  CHARTS_HOVER : 'charts-hover',
  PAUSED       : 'paused',
  NO_TELEMETRY : 'no-telemetry',
  RECONNECTING : 'reconnecting',
  INITIALIZING : 'initializing'
};

const STATUS_MESSAGES = {
  [STATUS_TYPES.CHARTS_HOVER] : 'Chart updates paused, collecting data in background',
  [STATUS_TYPES.PAUSED]       : 'Updates paused, collecting data in background',
  [STATUS_TYPES.NO_TELEMETRY] : 'No telemetry received',
  [STATUS_TYPES.RECONNECTING] : 'Reconnecting...',
  [STATUS_TYPES.INITIALIZING] : 'Initializing...'
};

const STATUS_CLASSES = {
  [STATUS_TYPES.CHARTS_HOVER] : 'status-charts-hover',
  [STATUS_TYPES.PAUSED]       : 'status-paused',
  [STATUS_TYPES.NO_TELEMETRY] : 'status-no-telemetry',
  [STATUS_TYPES.RECONNECTING] : 'status-reconnecting',
  [STATUS_TYPES.INITIALIZING] : 'status-initializing'
};

