// Track color assignments to ensure no duplicates until palette is exhausted
const taskColorMap = new Map(); // Maps task name to assigned color
const usedColors = new Set();   // Tracks which colors are currently in use
let nextColorIndex = 0;         // Index for next available color in palette

/**
 * Get a consistent color for a task name.
 *
 * Assigns colors sequentially from the palette to ensure no duplicates until
 * the palette is exhausted. Once exhausted, falls back to hash-based assignment
 * but maintains consistency (same task always gets same color).
 *
 * @param {string} taskName - The name of the task.
 * @returns {string} Hex/RGB color string from chart dataset palette.
 */
function getTaskColor(taskName)
{
  const config = getChartConfig();
  const palette = config.COLORS.DATASET_PALETTE;
  
  // If task already has an assigned color, return it (consistency)
  if (taskColorMap.has(taskName))
  {
    return taskColorMap.get(taskName);
  }

  // If palette is not exhausted, assign next available color
  if (nextColorIndex < palette.length)
  {
    const color = palette[nextColorIndex];
    taskColorMap.set(taskName, color);
    usedColors.add(color);
    nextColorIndex++;
    return color;
  }

  // Palette exhausted - use hash-based assignment but maintain consistency
  let hash = 0;
  for (let i = 0; i < taskName.length; i++)
  {
    hash = ((hash << 5) - hash) + taskName.charCodeAt(i);
    hash = hash & hash; // Convert to 32-bit integer
  }
  const index = Math.abs(hash) % palette.length;
  const color = palette[index];
  taskColorMap.set(taskName, color);
  return color;
}

/**
 * Release a color assignment for a task.
 *
 * Frees up the color when a task is removed, allowing it to be reused
 * if the palette was exhausted. This helps maintain unique colors when possible.
 *
 * @param {string} taskName - The name of the task whose color should be released.
 */
function releaseTaskColor(taskName)
{
  if (taskColorMap.has(taskName))
  {
    const color = taskColorMap.get(taskName);
    taskColorMap.delete(taskName);
    usedColors.delete(color);
    
    // If we released a color from the sequential range, we could potentially
    // reset nextColorIndex, but for simplicity we'll just let it continue
    // and rely on the hash-based fallback once exhausted
  }
}

/**
 * Create a standard dataset configuration object for Chart.js time series charts.
 *
 * @param {string} taskName - The name of the task (used for label and color assignment).
 * @param {Array<number>} data - An array of numeric data points for the dataset.
 * @returns {Object} Chart.js dataset configuration object with color, style, and label set.
 */
function createChartDataset(taskName, data)
{
  const CONFIG = getChartConfig();
  const taskColor = getTaskColor(taskName);
  return {
    label           : taskName,
    data            : data,
    borderColor     : taskColor,
    backgroundColor : taskColor,
    borderWidth     : CONFIG.ELEMENTS.LINE.BORDER_WIDTH,
    fill            : false,
    tension         : CONFIG.ELEMENTS.LINE.TENSION,
    pointRadius     : (context) => {
      // Only show points at the hovered index
      const tooltip = context.chart.tooltip;
      if (tooltip && tooltip._active && tooltip._active.length > 0)
      {
        const hoveredIndex = tooltip._active[0].dataIndex;
        if (context.dataIndex === hoveredIndex)
        {
          return CONFIG.ELEMENTS.POINT.HOVER_RADIUS;
        }
      }
      // All other points are hidden (radius 0)
      return CONFIG.ELEMENTS.POINT.RADIUS;
    },
    pointHoverRadius         : CONFIG.ELEMENTS.POINT.HOVER_RADIUS,
    pointHoverBackgroundColor: getTaskColor(taskName),
    pointHoverBorderColor    : getTaskColor(taskName),
    pointHoverBorderWidth    : 0,
    pointStyle               : 'rect'
  };
}

/**
 * Calculate the major tick interval in samples for 5-second intervals.
 *
 * @returns {number} Number of samples between major ticks (for 5-second intervals).
 */
function getMajorTickIntervalSamples()
{
  const intervalSeconds = CHART_TELEMETRY_UPDATE_INTERVAL_MS / 1000;
  const majorTickIntervalSeconds = 5;
  return Math.round(majorTickIntervalSeconds / intervalSeconds);
}

/**
 * Generate time-based labels for chart x-axis.
 *
 * Creates an array of labels for the chart x-axis, representing time in seconds
 * starting from the most recent sample and counting down to the oldest sample.
 *
 * @returns {Array<number>} Array of time labels (negative numbers from -99 to 0).
 */
function generateTimeLabels()
{
  return Array.from({ length: CHART_SAMPLE_COUNT }, (_, index) => index - CHART_SAMPLE_COUNT + 1);
}

/**
 * Create time-based tooltip callbacks.
 *
 * Creates a tooltip configuration object with a title formatter that shows
 * the number of seconds ago since the sample was taken, and a label formatter
 * that formats the tooltip label as desired.
 *
 * @param {Function} labelFormatter - Function to format the tooltip label.
 * @returns {Object} Tooltip configuration object with title and label callbacks.
 */
function createTooltipCallbacks(labelFormatter)
{
  return {
    title: function(context)
    {
      const label = context[0].label;
      const samplesAgo = Math.abs(parseInt(label));
      const intervalSeconds = CHART_TELEMETRY_UPDATE_INTERVAL_MS / 1000;
      const secondsAgo = Math.round(samplesAgo * intervalSeconds);
      return `${secondsAgo} seconds ago`;
    },
    label: labelFormatter
  };
}

// Create a custom tooltip positioner to center the caret vertically in the chart
// while using the x-position from the items (all items share the same x since they're at the same index)
Chart.Tooltip.positioners.centerVertical = function(items)
{
  if (!items || items.length === 0)
  {
    return false;
  }
  // Find the first valid item to get the x position (all items have the same x at the same index)
  let i, len;
  for (i = 0, len = items.length; i < len; ++i)
  {
    const el = items[i].element;
    if (el && el.hasValue())
    {
      const pos = el.tooltipPosition();
      const chart = this.chart;
      // Calculate vertical center of chart area (where the caret will be)
      const yPosition = (chart.chartArea.top + chart.chartArea.bottom) / 2;
      // Return x position (Chart.js will automatically determine xAlign based on available space)
      return {
        x: pos.x,
        y: yPosition,
        yAlign: 'center'
      };
    }
  }
  // No visible items found
  return false;
};

/**
 * Create tooltip configuration for Chart.js charts.
 *
 * Creates a tooltip configuration object with consistent positioning,
 * styling, and callbacks. The tooltip caret is positioned at the vertical
 * center of the chart area, and the tooltip box appears on the left or right
 * side of the caret based on available space, following the mouse cursor horizontally.
 *
 * @param {Object} tooltipCallbacks - Tooltip configuration object with title and label callbacks.
 * @returns {Object} Tooltip configuration object.
 */
function createTooltipConfig(tooltipCallbacks)
{
  const CONFIG = getChartConfig();
  // Read CSS variables to match microtip styling
  const rootStyle = getComputedStyle(document.documentElement);
  const tooltipBg = rootStyle.getPropertyValue('--color-theme-body-background').trim();
  const tooltipBorder = rootStyle.getPropertyValue('--color-theme-border').trim();
  return {
    enabled        : true,
    intersect      : false,
    mode           : 'index',
    backgroundColor: tooltipBg || CONFIG.COLORS.UI.TOOLTIP_BG,
    titleColor     : CONFIG.COLORS.UI.TOOLTIP_TEXT,
    bodyColor      : CONFIG.COLORS.UI.TOOLTIP_TEXT,
    borderColor    : tooltipBorder || CONFIG.COLORS.UI.GRID_COLOR_MAJOR,
    borderWidth    : CONFIG.PLUGINS.TOOLTIP.BORDER_WIDTH, // Match microtip border-1
    padding        : CONFIG.PLUGINS.TOOLTIP.PADDING,
    caretPadding   : CONFIG.PLUGINS.TOOLTIP.CARET_PADDING,
    boxPadding     : CONFIG.PLUGINS.TOOLTIP.BOX_PADDING,
    position       : 'centerVertical',
    itemSort       : function(a, b)
    {
      // Sort by value in descending order (highest first)
      // This allows our tooltip to show items in the order they're being highlighted
      const aValue = a.parsed.y !== null && a.parsed.y !== undefined ? a.parsed.y : -Infinity;
      const bValue = b.parsed.y !== null && b.parsed.y !== undefined ? b.parsed.y : -Infinity;
      return bValue - aValue;
    },
    callbacks      : {
      ...tooltipCallbacks,
      labelColor: function(context)
      {
        return {
          borderColor    : context.dataset.borderColor,
          backgroundColor: context.dataset.backgroundColor,
          borderWidth    : 0
        };
      }
    }
  };
}

/**
 * Get base chart options with common configuration.
 *
 * Returns a base configuration object for Chart.js time series charts,
 * including animation settings, responsiveness, aspect ratio maintenance,
 * interaction modes, hover event handling, and scale configuration.
 * Also configures Chart.js default tooltip fonts to match app theme on first call.
 *
 * @param {Object} yAxisConfig - Configuration object for the y-axis.
 * @param {Object} tooltipCallbacks - Tooltip configuration object with title and label callbacks.
 * @param {string} chartType - The type of chart (e.g., 'cpu' or 'memory').
 * @returns {Object} Base chart options configuration object.
 */
function getBaseChartOptions(yAxisConfig, tooltipCallbacks, chartType)
{
  const CONFIG = getChartConfig();

  // Configure Chart.js default font family to match app theme (only once)
  if (!getBaseChartOptions._fontsConfigured)
  {
    Chart.defaults.font.family           = CONFIG.FONT.FAMILY;
    Chart.defaults.font.size             = CONFIG.FONT.SIZE;
    Chart.defaults.font.lineHeight       = CONFIG.FONT.LINE_HEIGHT;
    getBaseChartOptions._fontsConfigured = true;
  }

  return {
    animation: false,
    responsive: true,
    maintainAspectRatio: false,
    devicePixelRatio: window.devicePixelRatio * 2,
    interaction: {
      mode      : 'index',
      intersect : false
    },
    onHover: (event, activeElements) => {
      // Update status popup when hovering (hover state is managed by mouseenter/mouseleave)
      updateStatusPopup();
    },
    scales: {
      x: {
        grid: {
          color: (context) => {
            // Convert index to actual label value (negative numbers from -99 to 0)
            const labelValue = context.tick.value - (CHART_SAMPLE_COUNT - 1);
            const majorTickInterval = getMajorTickIntervalSamples();
            // Make grid lines at 5-second intervals more visible (major ticks)
            if (majorTickInterval > 0 && labelValue % majorTickInterval === 0)
            {
              return CONFIG.COLORS.UI.GRID_COLOR_MAJOR;
            }
            return CONFIG.COLORS.UI.GRID_COLOR;
          },
          lineWidth: (context) => {
            // Convert index to actual label value (negative numbers from -99 to 0)
            const labelValue = context.tick.value - (CHART_SAMPLE_COUNT - 1);
            const majorTickInterval = getMajorTickIntervalSamples();
            // Make grid lines at 5-second intervals thicker (major ticks)
            if (majorTickInterval > 0 && labelValue % majorTickInterval === 0)
            {
              return 1.5;
            }
            return 0.5;
          }
        },
        ticks: {
          color       : CONFIG.COLORS.UI.TEXT_COLOR,
          autoSkip    : false,
          maxRotation : 0,

          callback: function(value, index, ticks)
          {
            // Get the actual label value (negative numbers from -99 to 0)
            const labelValue = this.getLabelForValue(value);
            const majorTickInterval = getMajorTickIntervalSamples();
            // Only show labels for major ticks (every 5 seconds)
            if (majorTickInterval > 0 && labelValue % majorTickInterval === 0)
            {
              // Convert sample count to seconds for display
              const intervalSeconds = CHART_TELEMETRY_UPDATE_INTERVAL_MS / 1000;
              const secondsAgo = Math.round(Math.abs(labelValue) * intervalSeconds);
              return secondsAgo;
            }
            return '';
          },
          major: {
            enabled: true
          }
        },
        title: { display: true, text: 'Seconds Ago', color: CONFIG.COLORS.UI.TEXT_COLOR }
      },
      y: {
        beginAtZero: true,
        max        : yAxisConfig.max,
        grid       : { color: CONFIG.COLORS.UI.GRID_COLOR },
        ticks      : { color: CONFIG.COLORS.UI.TEXT_COLOR },
        title      : { display: true, text: yAxisConfig.label, color: CONFIG.COLORS.UI.TEXT_COLOR }
      }
    },
    plugins: {
      legend: {
        position: 'bottom',
        labels  : {
          boxWidth      : CONFIG.PLUGINS.LEGEND.BOX_WIDTH,
          boxHeight     : CONFIG.PLUGINS.LEGEND.BOX_HEIGHT,
          boxPadding    : CONFIG.PLUGINS.LEGEND.BOX_PADDING,
          color         : CONFIG.COLORS.UI.TEXT_COLOR,
          usePointStyle : false,

          generateLabels: function(chart)
          {
            const original = Chart.defaults.plugins.legend.labels.generateLabels;
            const labels = original.call(this, chart);
            // Set borderWidth to 0 for solid color squares
            labels.forEach(label => {
              if (label.fillStyle)
              {
                label.strokeStyle = label.fillStyle;
                label.lineWidth = 0;
              }
            });
            return labels;
          }
        }
      },
      tooltip: createTooltipConfig(tooltipCallbacks)
    }
  };
}

// Plugin: draw a vertical hover line aligned to tooltip index
const verticalHoverLinePlugin =
{
  id: 'verticalHoverLine',
  afterDraw(chartInstance)
  {
    const tooltip = chartInstance.tooltip;
    if (!tooltip || !tooltip._active || tooltip._active.length === 0)
    {
      return;
    }
    const CONFIG = getChartConfig();
    const context   = chartInstance.ctx;
    const xPosition = tooltip._active[0].element.x;
    const topY      = chartInstance.chartArea.top;
    const bottomY   = chartInstance.chartArea.bottom;
    context.save();
    context.beginPath();
    context.moveTo(xPosition, topY);
    context.lineTo(xPosition, bottomY);
    context.lineWidth   = CONFIG.COLORS.UI.HOVER_LINE_WIDTH;
    context.strokeStyle = CONFIG.COLORS.UI.HOVER_LINE;
    context.stroke();
    context.restore();
  }
};
Chart.register(verticalHoverLinePlugin);

/**
 * Create and initialize the CPU usage chart.
 *
 * Builds the Chart.js CPU usage line chart using the provided initial history data.
 * Filters datasets according to the "hide system tasks" UI filter, formats tooltips,
 * applies theme-aware styling for dark/light modes, and registers the chart in AppState.
 * This function should be called with complete CPU/task usage history on dashboard load,
 * and can be called again to recreate the chart with new or filtered data.
 *
 * @param {Object} initialData - CPU history in the form: { taskName: { cpu: [percent, ...], ... }, ... }
 * @returns {void}
 */
function createCpuChart(initialData)
{
  const CONFIG = getChartConfig();
  const canvasContext = document.getElementById('cpuChart').getContext('2d');

  // Handle empty or missing initial data
  // Expected format: { taskName: { cpu: [array of CPU % values], stack: [...] } }
  const datasets = initialData && typeof initialData === 'object' 
    ? Object.entries(initialData)
        .filter(([taskName, taskData]) => {
          // Filter out system tasks if hideSystemTasks is enabled
          const isSystemTask = SYSTEM_TASKS.hasOwnProperty(taskName);
          return !(AppState.filters.hideSystemTasks && isSystemTask);
        })
        .map(([taskName, taskData]) => {
        // Extract CPU history array from task data object
        // taskData should be an object with a 'cpu' property containing an array
        const cpuHistoryArray = taskData && typeof taskData === 'object' && Array.isArray(taskData.cpu)
          ? taskData.cpu
          : []; // Fallback to empty array if structure is unexpected
        
        return createChartDataset(taskName, cpuHistoryArray);
      })
    : [];

  // CPU table specific tooltip generation
  const tooltipCallbacks = createTooltipCallbacks(function(context)
  {
    const label = context.dataset.label || '';
    const value = context.parsed.y;
    if (value === null || value === undefined)
    {
      return label;
    }
    // Show percentage first, right-aligned in a fixed-width monospace span (no zero padding)
    const percentString = value.toFixed(1) + '%';
    return label
      ? `(${percentString}) ${label}`
      : `(${percentString})`;
  });

  const yAxisConfig = {
    max  : 100,
    label: 'CPU %'
  };

  AppState.charts.cpu = window.chartInstance = new Chart(canvasContext, {
    type: 'line',
    data: {
      labels  : generateTimeLabels(),
      datasets: datasets
    },
    options: getBaseChartOptions(yAxisConfig, tooltipCallbacks, 'cpu')
  });

  // Add mouseenter/mouseleave detection to manage hover state
  const cpuCanvas = document.getElementById('cpuChart');
  cpuCanvas.addEventListener('mouseenter', () => 
  {
    AppState.ui.isHoveringCpu = true;
    updateStatusPopup();
  });
  cpuCanvas.addEventListener('mouseleave', () => 
  {
    AppState.ui.isHoveringCpu = false;
    // Immediately update both charts with accumulated data if neither is now hovered and app is not paused
    if (!AppState.ui.isHoveringMemory && !AppState.ui.isPaused)
    {
      AppState.charts.cpu.update('none');
      AppState.charts.memory.update('none');
    }
    updateStatusPopup();
  });
}

/**
 * Create the Memory usage chart with initial history.
 *
 * Constructs a non-stacked line chart showing memory (stack) usage percentages for
 * registered tasks. Uses the initial history from the server to populate the chart datasets.
 * Excludes system tasks without registered stack sizes. Handles unexpected or missing
 * input data gracefully. This chart displays each task's stack usage as a percentage
 * of its total stack size over the sample history window.
 *
 * @param {Object} initialData - Object containing per-task initial stack/history arrays, e.g.:
 *   { taskName: { cpu: [...], stack: [...] }, ... }
 */
function createMemoryChart(initialData)
{
  const canvasContext = document.getElementById('memoryChart').getContext('2d');

  // Only include registered tasks (those with known stack sizes)
  // Handle empty or missing initial data
  // Expected format: { taskName: { cpu: [...], stack: [array of stack bytes] } }
  const datasets = initialData && typeof initialData === 'object'
    ? Object.entries(initialData)
        .filter(([taskName, taskData]) => AppState.data.registeredTasks.has(taskName))
        .map(([taskName, taskData]) => {
          // Extract stack history array from task data object
          // taskData should be an object with a 'stack' property containing an array of bytes
          const stackDataBytes = taskData && typeof taskData === 'object' && Array.isArray(taskData.stack)
            ? taskData.stack
            : []; // Fallback to empty array if structure is unexpected
          
          // Get the registered stack size for this task (needed to calculate percentages)
          const stackSize = AppState.data.taskInfo[taskName]?.stackSize || 0;
          
          // Convert stack bytes to percentages for display
          const percentageData = stackDataBytes.map(bytes => {
            if (stackSize > 0 && Number.isFinite(bytes))
            {
              return (bytes / stackSize) * 100;
            }
            return 0;
          });
          
          return createChartDataset(taskName, percentageData);
        })
    : [];

  const tooltipCallbacks = createTooltipCallbacks(function(context)
  {
    const label = context.dataset.label || '';
    const value = context.parsed.y;
    if (value === null || value === undefined)
    {
      return label;
    }
    // Show percentage first, right-aligned in a fixed-width monospace span (no zero padding)
    const percentString = value.toFixed(1) + '%';
    return label
      ? `(${percentString}) ${label}`
      : `(${percentString})`;
  });

  const yAxisConfig = {
    max  : 100,
    label: 'Task Stack Usage (%)'
  };

  AppState.charts.memory = window.memoryChartInstance = new Chart(canvasContext, {
    type: 'line',
    data: {
      labels  : generateTimeLabels(),
      datasets: datasets
    },
    options: getBaseChartOptions(yAxisConfig, tooltipCallbacks, 'memory')
  });

  // Add mouseenter/mouseleave detection to manage hover state
  const memoryCanvas = document.getElementById('memoryChart');
  memoryCanvas.addEventListener('mouseenter', () => 
  {
    AppState.ui.isHoveringMemory = true;
    updateStatusPopup();
  });
  memoryCanvas.addEventListener('mouseleave', () => 
  {
    AppState.ui.isHoveringMemory = false;
    // Immediately update both charts with accumulated data if neither is now hovered and app is not paused
    if (!AppState.ui.isHoveringCpu && !AppState.ui.isPaused)
    {
      AppState.charts.cpu.update('none');
      AppState.charts.memory.update('none');
    }
    updateStatusPopup();
  });
}

/**
 * Update chart datasets with new telemetry data.
 *
 * Updates both CPU and Memory chart datasets with the latest telemetry data,
 * handles task filtering (system tasks, low usage), manages dataset lifecycle
 * (add/remove tasks), and updates chart labels. Only updates visual display if
 * charts are not being hovered.
 *
 * @param {Object} telemetryCurrent - The current telemetry data for tasks.
 * @param {Set} currentTaskNames - Set of task names present in current telemetry.
 */
function updateCharts(telemetryCurrent, currentTaskNames)
{
  if (!AppState.charts.cpu || !AppState.charts.memory)
  {
    return;
  }

  // Update CPU chart datasets
  // Expected format: { taskName: { cpu: <number>, stack: <number>, stackPct: <number>, ... } }
  for (const [taskName, taskCurrent] of Object.entries(telemetryCurrent))
  {
    // Skip system tasks if hideSystemTasks filter is enabled
    const isSystemTask = SYSTEM_TASKS.hasOwnProperty(taskName);
    if (AppState.filters.hideSystemTasks && isSystemTask)
    {
      continue;
    }

    // Extract CPU usage percentage value from task data object
    // taskCurrent should be an object with a 'cpu' property containing a number
    const cpuValue = taskCurrent && typeof taskCurrent === 'object' && typeof taskCurrent.cpu === 'number'
      ? taskCurrent.cpu
      : 0; // Fallback to 0 if CPU value is missing or invalid
    let dataset = AppState.charts.cpu.data.datasets.find(d => d.label === taskName);
    if (!dataset)
    {
      dataset = createChartDataset(
        taskName,
        Array(AppState.charts.cpu.data.labels.length - 1).fill(0)
      );
      AppState.charts.cpu.data.datasets.push(dataset);
    }
    dataset.data.push(cpuValue);
    if (dataset.data.length > CHART_SAMPLE_COUNT)
    {
      dataset.data.shift();
    }

    // Calculate average usage over all data points in the dataset
    let averageUsage = 0.0;
    if (dataset.data.length > 0)
    {
      const validValues = dataset.data.filter(v => Number.isFinite(v));
      if (validValues.length > 0)
      {
        const sum = validValues.reduce((acc, val) => acc + val, 0);
        averageUsage = sum / validValues.length;
      }
    }

    // Hide dataset if average usage is below threshold
    const isLowUsage = averageUsage < AppState.filters.thresholdPercent;
    dataset.hidden = AppState.filters.hideLowUsage && isLowUsage;
  }

  // Remove CPU chart datasets for tasks that no longer exist
  // Also remove system tasks if hideSystemTasks filter is enabled
  const removedCpuTasks = new Set();
  AppState.charts.cpu.data.datasets = AppState.charts.cpu.data.datasets.filter(dataset => {
    // Remove if task no longer exists
    if (!currentTaskNames.has(dataset.label))
    {
      removedCpuTasks.add(dataset.label);
      return false;
    }
    // Remove if it's a system task and filter is enabled
    const isSystemTask = SYSTEM_TASKS.hasOwnProperty(dataset.label);
    if (AppState.filters.hideSystemTasks && isSystemTask)
    {
      removedCpuTasks.add(dataset.label);
      return false;
    }
    return true;
  });

  // Update Memory chart datasets (non-stacked, showing percentages) - only for registered tasks
  const currentRegisteredTaskNames = new Set();
  for (const [taskName, taskCurrent] of Object.entries(telemetryCurrent))
  {
    // Skip non-registered tasks - we only chart tasks with known stack sizes
    if (!AppState.data.registeredTasks.has(taskName))
    {
      continue;
    }

    currentRegisteredTaskNames.add(taskName);

    // Use stack percentage (available for registered tasks)
    let stackPct = 0;
    if (taskCurrent.stackPct !== undefined && taskCurrent.stackPct > 0)
    {
      stackPct = taskCurrent.stackPct;
    }
    
    let dataset = AppState.charts.memory.data.datasets.find(d => d.label === taskName);
    if (!dataset)
    {
      dataset = createChartDataset(
        taskName,
        Array(AppState.charts.memory.data.labels.length - 1).fill(0)
      );
      AppState.charts.memory.data.datasets.push(dataset);
    }
    dataset.data.push(stackPct);
    if (dataset.data.length > CHART_SAMPLE_COUNT)
    {
      dataset.data.shift();
    }
  }

  // Remove Memory chart datasets for tasks that no longer exist or are no longer registered
  const removedMemoryTasks = new Set();
  AppState.charts.memory.data.datasets = AppState.charts.memory.data.datasets.filter(dataset => {
    if (currentRegisteredTaskNames.has(dataset.label))
    {
      return true;
    }
    // Task no longer exists or is no longer registered - remove it
    removedMemoryTasks.add(dataset.label);
    return false;
  });

  // Release colors for tasks that are removed from both charts
  // A task might be in one chart but not the other, so only release if removed from both
  const allRemovedTasks = new Set([...removedCpuTasks, ...removedMemoryTasks]);
  for (const taskName of allRemovedTasks)
  {
    // Only release if task is removed from both charts (or not in either)
    const inCpuChart = AppState.charts.cpu.data.datasets.some(d => d.label === taskName);
    const inMemoryChart = AppState.charts.memory.data.datasets.some(d => d.label === taskName);
    if (!inCpuChart && !inMemoryChart)
    {
      releaseTaskColor(taskName);
    }
  }
  
  AppState.charts.cpu.data.labels.push(AppState.charts.cpu.data.labels.length);
  if (AppState.charts.cpu.data.labels.length > CHART_SAMPLE_COUNT)
  {
    AppState.charts.cpu.data.labels.shift();
  }
  AppState.charts.cpu.data.labels = generateTimeLabels();

  // Update memory chart labels
  AppState.charts.memory.data.labels.push(AppState.charts.memory.data.labels.length);
  if (AppState.charts.memory.data.labels.length > CHART_SAMPLE_COUNT)
  {
    AppState.charts.memory.data.labels.shift();
  }
  AppState.charts.memory.data.labels = generateTimeLabels();
  
  // Only update visual display if neither chart is being hovered and not paused
  const isAnyChartHovered = AppState.ui.isHoveringCpu || AppState.ui.isHoveringMemory || AppState.ui.isPaused;
  if (!isAnyChartHovered)
  {
    AppState.charts.cpu.update('none');
    AppState.charts.memory.update('none');
  }
}


