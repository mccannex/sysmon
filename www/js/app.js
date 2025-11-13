/**
 * Update the sampling configuration info text with sample count and update interval.
 */
function updateSamplingConfigInfo()
{
  const samplingConfigInfo = document.getElementById('samplingConfigInfo');
  if (samplingConfigInfo)
  {
    samplingConfigInfo.textContent = `${CHART_SAMPLE_COUNT} samples, ${CHART_TELEMETRY_UPDATE_INTERVAL_MS} ms interval`;
  }
}

/**
 * Update the status popup message according to dashboard state.
 * 
 * Determines which status notification should be shown based on user interaction
 * (such as chart hover), connection health, and telemetry staleness. Shows the most
 * relevant message (e.g., live hover, reconnecting, telemetry stalled) for the user.
 * Falls back to hiding the status popup when all systems are healthy and up to date.
 */
function updateStatusPopup()
{
  // Priority order: pause > chart hover > connection issues > normal
  if (AppState.ui.isPaused)
  {
    showStatusPopup(STATUS_TYPES.PAUSED);
    return;
  }
  if (AppState.ui.isHoveringCpu || AppState.ui.isHoveringMemory)
  {
    showStatusPopup(STATUS_TYPES.CHARTS_HOVER);
    return;
  }

  const now = Date.now();
  const telemetryTimeout = 5000; // 5 seconds
  const tableTimeout = 15000;    // 15 seconds

  const telemetryStale = AppState.status.lastTelemetrySuccess === null ||
                         (now - AppState.status.lastTelemetrySuccess) > telemetryTimeout;
  const tableStale = AppState.status.lastTableSuccess === null ||
                     (now - AppState.status.lastTableSuccess) > tableTimeout;

  if (telemetryStale || tableStale)
  {
    if (AppState.status.consecutiveFailures > 3)
    {
      showStatusPopup(STATUS_TYPES.RECONNECTING);
    }
    else
    {
      showStatusPopup(STATUS_TYPES.NO_TELEMETRY);
    }
    return;
  }

  // All good, hide popup
  hideStatusPopup();
}

/**
 * Initialize and start the main dashboard application.
 *
 * Performs initial setup by:
 *  - Fetching task info to determine registered and system tasks.
 *  - Retrieving telemetry history for chart population.
 *  - Building and initializing all dashboard charts, widgets, and summaries.
 *  - Wiring up UI controls including filtering, system task toggles, and updating tooltips.
 *  - Setting up periodic timers to refresh chart data, task table, and summary badges.
 *  - Setting status popup for feedback during initialization and connection issues.
 *
 * Should be called once when the dashboard page loads. Handles all one-time bootstrapping
 * and begins ongoing scheduled updates.
 *
 * @async
 * @returns {Promise<void>} Resolves after dashboard initialization is complete.
 */
async function initializeDashboard()
{
  showStatusPopup(STATUS_TYPES.INITIALIZING);

  // Fetch hardware info first to get configuration values (must be before chart creation)
  await updateHardwareInfo();
  
  // Update sampling configuration info text with sample count and interval
  updateSamplingConfigInfo();

  // Fetch task info to identify registered tasks (those with known stack sizes)
  try
  {
    const taskInfoResponse = await fetch(API_ROUTES.TASKS);
    if (taskInfoResponse.ok)
    {
      AppState.data.taskInfo = await taskInfoResponse.json();
      AppState.status.lastTableSuccess = Date.now();
      AppState.status.consecutiveFailures = 0;
      // Populate registeredTasks set with tasks that have stackSize > 0
      AppState.data.registeredTasks.clear();
      for (const [taskName, taskInfo] of Object.entries(AppState.data.taskInfo))
      {
        if (taskInfo.stackSize !== undefined && taskInfo.stackSize > 0)
        {
          AppState.data.registeredTasks.add(taskName);
        }
      }
    }
    else
    {
      AppState.status.consecutiveFailures++;
    }
  }
  catch (error)
  {
    // If we can't fetch task info, continue anyway - charts will just be empty
    console.warn("Failed to fetch task info:", error);
    AppState.status.consecutiveFailures++;
  }

  try
  {
    const response = await fetch(API_ROUTES.HISTORY);
    if (response.ok)
    {
      const data = await response.json();
      createCpuChart(data);
      createMemoryChart(data); // Only includes registered tasks now
      AppState.status.lastTelemetrySuccess = Date.now();
      AppState.status.consecutiveFailures = 0;
    }
    else
    {
      AppState.status.consecutiveFailures++;
    }
  }
  catch (error)
  {
    console.warn("Failed to fetch initial data:", error);
    AppState.status.consecutiveFailures++;
  }

  // Load table and summary right away
  updateTable();
  updateDashboard();
  updateStatusPopup();

  // Initialize tracked task names from first telemetry call
  // This will be set in updateDashboard after first fetch

  // Wire up filter controls
  const hideCheckbox = document.getElementById('hideLowToggle');
  const thresholdInput = document.getElementById('thresholdPct');
  const hideSystemTasksToggle = document.getElementById('hideSystemTasksToggle');
  const taskTable = document.getElementById('taskTable');

  if (hideCheckbox)
  {
    hideCheckbox.addEventListener('change', () => {
      AppState.filters.hideLowUsage = hideCheckbox.checked;
    });
  }

  if (thresholdInput)
  {
    thresholdInput.addEventListener('input', () => {
      const value = parseFloat(thresholdInput.value);
      if (Number.isFinite(value))
      {
        AppState.filters.thresholdPercent = value;
      }
    });
    // Initialize threshold from input value
    const initialValue = parseFloat(thresholdInput.value);
    if (Number.isFinite(initialValue))
    {
      AppState.filters.thresholdPercent = initialValue;
    }
  }

  if (hideSystemTasksToggle && taskTable)
  {
    // Initialize table state based on checkbox
    if (hideSystemTasksToggle.checked)
    {
      taskTable.classList.add('hide-system-tasks');
      AppState.filters.hideSystemTasks = true;
    }
    else
    {
      taskTable.classList.remove('hide-system-tasks');
      AppState.filters.hideSystemTasks = false;
    }

    hideSystemTasksToggle.addEventListener('change', async () => {
      if (hideSystemTasksToggle.checked)
      {
        taskTable.classList.add('hide-system-tasks');
        AppState.filters.hideSystemTasks = true;
        // Remove system task datasets from chart when filter is enabled
        if (AppState.charts.cpu)
        {
          AppState.charts.cpu.data.datasets = AppState.charts.cpu.data.datasets.filter(dataset => {
            const isSystemTask = SYSTEM_TASKS.hasOwnProperty(dataset.label);
            return !(AppState.filters.hideSystemTasks && isSystemTask);
          });
          AppState.charts.cpu.update('none');
        }
      }
      else
      {
        taskTable.classList.remove('hide-system-tasks');
        AppState.filters.hideSystemTasks = false;
        // When disabled, fetch history to get system task data and add them to the chart
        if (AppState.charts.cpu)
        {
          try
          {
            const response = await fetch(API_ROUTES.HISTORY);
            if (response.ok)
            {
              const historyData = await response.json();
              // Add system task datasets from history
              for (const [taskName, taskData] of Object.entries(historyData))
              {
                const isSystemTask = SYSTEM_TASKS.hasOwnProperty(taskName);
                if (isSystemTask)
                {
                  // Check if dataset already exists
                  let dataset = AppState.charts.cpu.data.datasets.find(d => d.label === taskName);
                  if (!dataset)
                  {
                    // Extract CPU history array from task data object
                    const cpuHistoryArray = taskData && typeof taskData === 'object' && Array.isArray(taskData.cpu)
                      ? taskData.cpu
                      : [];

                    // Pad or truncate to match current chart length
                    const currentLength = AppState.charts.cpu.data.labels.length;
                    let paddedData = [...cpuHistoryArray];
                    if (paddedData.length < currentLength)
                    {
                      // Pad with zeros at the beginning
                      paddedData = Array(currentLength - paddedData.length).fill(0).concat(paddedData);
                    }
                    else if (paddedData.length > currentLength)
                    {
                      // Truncate from the beginning
                      paddedData = paddedData.slice(-currentLength);
                    }

                    dataset = createChartDataset(taskName, paddedData);
                    AppState.charts.cpu.data.datasets.push(dataset);
                  }
                }
              }
              AppState.charts.cpu.update('none');
            }
          }
          catch (error)
          {
            console.warn("Failed to fetch history for system tasks:", error);
          }
        }
      }
    });
  }

  // Hardware information already loaded at start of initializeDashboard()

  // Wire up pause toggle button
  const pauseToggle = document.getElementById('pauseToggle');
  const pauseToggleIcon = document.getElementById('pauseToggleIcon');
  if (pauseToggle && pauseToggleIcon)
  {
    pauseToggle.addEventListener('click', (event) => {
      AppState.ui.isPaused = !AppState.ui.isPaused;
      
      if (AppState.ui.isPaused)
      {
        // Paused state: show play icon
        pauseToggleIcon.textContent = MATERIAL_ICONS.PLAY_ARROW;
        pauseToggle.setAttribute('aria-label', 'Resume visual data updates.');
        pauseToggle.setAttribute('role', 'tooltip');
        pauseToggle.setAttribute('data-microtip-position', 'bottom-left');
      }
      else
      {
        // Active state: show pause icon
        pauseToggleIcon.textContent = MATERIAL_ICONS.PAUSE;
        pauseToggle.setAttribute('aria-label', 'Pause visual data updates while continuing to collect data in the background.');
        pauseToggle.setAttribute('role', 'tooltip');
        pauseToggle.setAttribute('data-microtip-position', 'bottom-left');
        
        // Immediately update charts with accumulated data when unpausing
        if (AppState.charts.cpu && AppState.charts.memory)
        {
          AppState.charts.cpu.update('none');
          AppState.charts.memory.update('none');
        }
      }
      
      updateStatusPopup();
      
      // Remove focus from the button to hide the tooltip
      // This happens because clicking a button gives it focus, and microtip
      // shows tooltips on both :hover and :focus states
      // Use currentTarget to ensure we blur the button, not a child element
      if (event && event.currentTarget)
      {
        event.currentTarget.blur();
      }
    });
  }

  // Keep updating charts, summary, and table
  setInterval(updateDashboard, CHART_TELEMETRY_UPDATE_INTERVAL_MS);
  setInterval(updateTable, CHART_TASK_TABLE_UPDATE_INTERVAL_MS);
}

/**
 * Update the main dashboard UI with the latest telemetry data.
 *
 * Retrieves the latest combined telemetry data (CPU, memory, PSRAM, etc.) from the API and 
 * uses it to update the summary badges, progress bars, and time series chart datasets in the 
 * UI. Handles server communication with timeouts, updates AppState with success or failure, 
 * and visually refreshes the main dashboard metrics to reflect the most recent device measurements.
 */
async function updateDashboard()
{
  try
  {
    // By default, fetch() does not support a timeout natively.
    // To enforce a timeout, we use AbortController to cancel the request if it takes too long.
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), TELEMETRY_TIMEOUT_MS);
    let response;
    try {
      response = await fetch(API_ROUTES.TELEMETRY, { signal: controller.signal });
    } finally {
      clearTimeout(timeoutId);
    }
    if (!response.ok)
    {
      AppState.status.consecutiveFailures++;
      updateStatusPopup();
      return;
    }
    const telemetryData = await response.json();
    AppState.status.lastTelemetrySuccess = Date.now();
    AppState.status.consecutiveFailures = 0;

    // Compute current task names once for both paused and active paths
    const currentTaskNames = new Set(Object.keys(telemetryData.current));

    // Skip visual updates if paused (data collection continues)
    if (!AppState.ui.isPaused)
    {
      // Update summary badges with progress bars
      const cpuOverall    = document.getElementById('cpuOverall');
      const cpuC0         = document.getElementById('cpuC0');
      const cpuC1         = document.getElementById('cpuC1');
      const cpuOverallBar = document.getElementById('cpuOverallBar');
      const cpuC0Bar      = document.getElementById('cpuC0Bar');
      const cpuC1Bar      = document.getElementById('cpuC1Bar');

      const overallValue = telemetryData.summary.cpu.overall;
    const core0Value   = telemetryData.summary.cpu.cores[0];
    const core1Value   = telemetryData.summary.cpu.cores[1];

    cpuOverall.textContent = `${overallValue.toFixed(1)} %`;
    cpuC0.textContent      = `${core0Value.toFixed(1)} %`;
    cpuC1.textContent      = `${core1Value.toFixed(1)} %`;

    // Update progress bars with color coding
    updateCpuProgressBar(cpuOverallBar, overallValue);
    updateCpuProgressBar(cpuC0Bar, core0Value);
    updateCpuProgressBar(cpuC1Bar, core1Value);

    // Update tooltips on containers (containers are always full width and hoverable)
    const cpuOverallContainer = cpuOverallBar ? cpuOverallBar.closest('.progress-container') : null;
    const cpuC0Container = cpuC0Bar ? cpuC0Bar.closest('.progress-container') : null;
    const cpuC1Container = cpuC1Bar ? cpuC1Bar.closest('.progress-container') : null;

    if (cpuOverallContainer)
    {
      cpuOverallContainer.setAttribute('aria-label', `Overall CPU: ${overallValue.toFixed(1)}%`);
      cpuOverallContainer.setAttribute('role', 'tooltip');
      cpuOverallContainer.setAttribute('data-microtip-position', 'bottom');
    }
    if (cpuC0Container)
    {
      cpuC0Container.setAttribute('aria-label', `Core 0: ${core0Value.toFixed(1)}%`);
      cpuC0Container.setAttribute('role', 'tooltip');
      cpuC0Container.setAttribute('data-microtip-position', 'bottom');
    }
    if (cpuC1Container)
    {
      cpuC1Container.setAttribute('aria-label', `Core 1: ${core1Value.toFixed(1)}%`);
      cpuC1Container.setAttribute('role', 'tooltip');
      cpuC1Container.setAttribute('data-microtip-position', 'bottom');
    }

    // Update DRAM visualizations
    const dramTotal   = telemetryData.summary.mem.dram.total;
    const dramFree    = telemetryData.summary.mem.dram.free;
    const dramUsed    = dramTotal - dramFree;
    const dramUsedPct = telemetryData.summary.mem.dram.usedPct;
    const dramLargest = telemetryData.summary.mem.dram.largest;

    // Update text elements
    const dramUsedPctEl = document.getElementById('dramUsedPct');
    const dramFreeEl    = document.getElementById('dramFree');
    const dramUsedEl    = document.getElementById('dramUsed');
    const dramLargestEl = document.getElementById('dramLargest');
    const dramTotalEl   = document.getElementById('dramTotal');

    dramUsedPctEl.textContent = `${dramUsedPct.toFixed(1)} %`;
    dramFreeEl.textContent    = formatSize(dramFree, 'kb', true);
    dramFreeEl.setAttribute('aria-label', formatSize(dramFree, 'bytes', true));
    dramFreeEl.setAttribute('role', 'tooltip');
    dramFreeEl.setAttribute('data-microtip-position', 'bottom');
    dramUsedEl.textContent    = formatSize(dramUsed, 'kb', true);
    dramUsedEl.setAttribute('aria-label', formatSize(dramUsed, 'bytes', true));
    dramUsedEl.setAttribute('role', 'tooltip');
    dramUsedEl.setAttribute('data-microtip-position', 'bottom');
    dramLargestEl.textContent = formatSize(dramLargest, 'kb', true);
    dramLargestEl.setAttribute('aria-label', formatSize(dramLargest, 'bytes', true));
    dramLargestEl.setAttribute('role', 'tooltip');
    dramLargestEl.setAttribute('data-microtip-position', 'bottom');
    dramTotalEl.textContent   = formatSize(dramTotal, 'kb', true);
    dramTotalEl.setAttribute('aria-label', formatSize(dramTotal, 'bytes', true));
    dramTotalEl.setAttribute('role', 'tooltip');
    dramTotalEl.setAttribute('data-microtip-position', 'bottom-left');

    // Update WiFi RSSI icon if available in telemetry
    if (telemetryData.summary && telemetryData.summary.wifiRssi !== undefined)
    {
      updateWifiRssi(telemetryData.summary.wifiRssi);
    }

    // Update usage progress bar (green for used, grey background for free)
    const dramUsedBar = document.getElementById('dramUsedBar');
    const dramUsedContainer = dramUsedBar ? dramUsedBar.closest('.progress-container') : null;
    if (dramUsedBar)
    {
      updateDramProgressBar(dramUsedBar, dramUsedPct);
      // Update tooltip with used/free/total on container
      if (dramUsedContainer)
      {
        dramUsedContainer.setAttribute('aria-label', `DRAM: ${formatSize(dramUsed, 'bytes', true)} used (${dramUsedPct.toFixed(1)}%), ${formatSize(dramFree, 'bytes', true)} free, ${formatSize(dramTotal, 'bytes', true)} total`);
        dramUsedContainer.setAttribute('role', 'tooltip');
        dramUsedContainer.setAttribute('data-microtip-position', 'bottom');
      }
    }

    // Update fragmentation bar (largest block as percentage of total, positioned from right)
    const dramFragmentationBar = document.getElementById('dramFragmentationBar');
    if (dramFragmentationBar && dramTotal > 0)
    {
      // Show largest block as a percentage of total, positioned from the right edge
      const largestPct = (dramLargest / dramTotal) * 100;
      dramFragmentationBar.style.width = `${largestPct}%`;
      dramFragmentationBar.style.display = (largestPct > 0 && largestPct <= 100) ? 'block' : 'none';
    }

    // Update PSRAM visualizations
    const psramSection = document.getElementById('psramSection');
    if (telemetryData.summary.mem.psram.present)
    {
      const psramTotal   = telemetryData.summary.mem.psram.total;
      const psramFree    = telemetryData.summary.mem.psram.free;
      const psramUsed    = psramTotal - psramFree;
      const psramUsedPct = telemetryData.summary.mem.psram.usedPct;

      // Show PSRAM section
      psramSection.classList.remove('hidden');

      // Update text elements
      const psramUsedPctEl = document.getElementById('psramUsedPct');
      const psramFreeEl    = document.getElementById('psramFree');
      const psramUsedEl    = document.getElementById('psramUsed');
      const psramTotalEl   = document.getElementById('psramTotal');

      psramUsedPctEl.textContent = `${psramUsedPct.toFixed(1)} %`;
      psramFreeEl.textContent    = formatSize(psramFree, 'kb', true);
      psramFreeEl.setAttribute('aria-label', formatSize(psramFree, 'bytes', true));
      psramFreeEl.setAttribute('role', 'tooltip');
      psramFreeEl.setAttribute('data-microtip-position', 'bottom');
      psramUsedEl.textContent    = formatSize(psramUsed, 'kb', true);
      psramUsedEl.setAttribute('aria-label', formatSize(psramUsed, 'bytes', true));
      psramUsedEl.setAttribute('role', 'tooltip');
      psramUsedEl.setAttribute('data-microtip-position', 'bottom');
      psramTotalEl.textContent   = formatSize(psramTotal, 'kb', true);
      psramTotalEl.setAttribute('aria-label', formatSize(psramTotal, 'bytes', true));
      psramTotalEl.setAttribute('role', 'tooltip');
      psramTotalEl.setAttribute('data-microtip-position', 'bottom-left');

      // Update usage progress bar (green for used, grey background for free)
      const psramUsedBar = document.getElementById('psramUsedBar');
      const psramUsedContainer = psramUsedBar ? psramUsedBar.closest('.progress-container') : null;
      if (psramUsedBar)
      {
        updatePsramProgressBar(psramUsedBar, psramUsedPct);
        // Update tooltip with used/free/total on container
        if (psramUsedContainer)
        {
          psramUsedContainer.setAttribute('aria-label', `PSRAM: ${formatSize(psramUsed, 'bytes', true)} used (${psramUsedPct.toFixed(1)}%), ${formatSize(psramFree, 'bytes', true)} free, ${formatSize(psramTotal, 'bytes', true)} total`);
          psramUsedContainer.setAttribute('role', 'tooltip');
          psramUsedContainer.setAttribute('data-microtip-position', 'bottom');
        }
      }
    }
    else
    {
      psramSection.classList.add('hidden');
    }

    // Detect task changes: if new tasks appeared or tasks disappeared, refresh table immediately
    const previousTaskNames = AppState.data.lastTelemetryTaskNames;
    const hasNewTasks       = [...currentTaskNames].some(name => !previousTaskNames.has(name));
    const hasRemovedTasks   = [...previousTaskNames].some(name => !currentTaskNames.has(name));

    if (hasNewTasks || hasRemovedTasks)
    {
      // Task was added or removed - refresh table immediately to show current state
      updateTable();
    }

    // Update tracked task names for next comparison
    AppState.data.lastTelemetryTaskNames = new Set(currentTaskNames);

      // Update charts with new telemetry data
      updateCharts(telemetryData.current, currentTaskNames);

      // Update table rows for registered tasks with telemetry data
      updateTableRowsFromTelemetry(telemetryData.current);
    }
    else
    {
      // When paused, still update chart data but don't trigger visual update
      // This allows data to accumulate in the background
      updateCharts(telemetryData.current, currentTaskNames);
    }

    updateStatusPopup();
  }
  catch (error)
  {
    AppState.status.consecutiveFailures++;
    updateStatusPopup();
  }
}

// Application startup (entry point)
window.addEventListener('load', initializeDashboard);


