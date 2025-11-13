/**
 * Update CPU-related columns in a table row from telemetry data.
 *
 * Updates the CPU percentage cell and CPU usage progress bar for the given row.
 * This function works for all tasks, including system tasks.
 *
 * @param {HTMLElement} row - The table row element to update.
 * @param {Object} taskCurrent - The telemetry data for the current task.
 */
function updateTableRowCpu(row, taskCurrent)
{
  // Format CPU percentage from telemetry data
  const cpuPct = taskCurrent.cpu !== undefined && typeof taskCurrent.cpu === 'number'
    ? taskCurrent.cpu.toFixed(1)
    : '-';

  // Update CPU % cell
  const cpuPctCell = row.querySelector('[data-column="cpu-pct"]');
  if (cpuPctCell)
  {
    cpuPctCell.textContent = cpuPct === '-' ? '-' : `${cpuPct} %`;
  }

  // Update CPU Usage progress bar
  const cpuUsageBar = row.querySelector('[data-column="cpu-usage-bar"]');
  const cpuUsageContainer = cpuUsageBar ? cpuUsageBar.closest('.progress-container') : null;
  if (cpuUsageBar)
  {
    if (cpuPct === '-')
    {
      cpuUsageBar.style.width = '0%';
      cpuUsageBar.classList.remove('progress-bar-low', 'progress-bar-medium', 'progress-bar-high');
      if (cpuUsageContainer)
      {
        cpuUsageContainer.removeAttribute('aria-label');
        cpuUsageContainer.removeAttribute('role');
        cpuUsageContainer.removeAttribute('data-microtip-position');
      }
    }
    else
    {
      const cpuValue = parseFloat(cpuPct);
      if (Number.isFinite(cpuValue))
      {
        updateCpuProgressBar(cpuUsageBar, cpuValue);
        // Get task name for tooltip
        const taskNameCell = row.querySelector('[data-column="task-name"]');
        const taskName = taskNameCell ? taskNameCell.textContent.trim() : 'Task';
        if (cpuUsageContainer)
        {
          cpuUsageContainer.setAttribute('aria-label', `${taskName}: ${cpuValue.toFixed(1)}% CPU usage`);
          cpuUsageContainer.setAttribute('role', 'tooltip');
          cpuUsageContainer.setAttribute('data-microtip-position', 'bottom');
        }
      }
    }
  }
}

/**
 * Update stack-related columns in a table row from telemetry data.
 *
 * Updates stack-related columns (percentage and progress bar) for registered tasks.
 * This function should only be called for registered tasks.
 *
 * @param {HTMLElement} row - The table row element to update.
 * @param {Object} taskCurrent - The telemetry data for the current task.
 * @param {string} taskName - The name of the task.
 */
function updateTableRowStack(row, taskCurrent, taskName)
{
  // Get task info for stackSize (needed for tooltip)
  const taskInfo = AppState.data.taskInfo[taskName];
  const stackSize = taskInfo ? taskInfo.stackSize : undefined;

  // Format stack values from telemetry data
  const stackRemainingBytes = taskCurrent.stackRemaining !== undefined ? taskCurrent.stackRemaining : 0;
  const stackPct            = formatStackUsedPercentage(taskCurrent.stackPct, stackSize);

  // Update stack usage percentage cell
  const usagePctCell = row.querySelector('[data-column="stack-usage-pct"]');
  if (usagePctCell)
  {
    usagePctCell.textContent = stackPct.display;
    // Update warning class
    usagePctCell.classList.remove('stack-low');
    if (stackPct.warning)
    {
      usagePctCell.classList.add('stack-low');
    }
  }

  // Update Stack Usage progress bar
  const stackUsageBar = row.querySelector('[data-column="stack-usage-bar"]');
  const stackUsageContainer = stackUsageBar ? stackUsageBar.closest('.progress-container') : null;
  if (stackUsageBar)
  {
    if (!stackPct.display || stackPct.display === '-')
    {
      stackUsageBar.style.width = '0%';
      stackUsageBar.classList.remove('progress-bar-low', 'progress-bar-medium', 'progress-bar-high');
      if (stackUsageContainer)
      {
        stackUsageContainer.removeAttribute('aria-label');
        stackUsageContainer.removeAttribute('role');
        stackUsageContainer.removeAttribute('data-microtip-position');
      }
    }
    else
    {
      // Extract numeric value from stackPct.display (format: "X.X %" or "X %")
      const stackPctValue = taskCurrent.stackPct !== undefined && typeof taskCurrent.stackPct === 'number'
        ? taskCurrent.stackPct
        : parseFloat(stackPct.display);
      if (Number.isFinite(stackPctValue))
      {
        // Use CPU progress bar updater (same thresholds: 50/80)
        // For stack usage, higher is worse, but we'll use the same visual thresholds
        updateCpuProgressBar(stackUsageBar, stackPctValue);

        // Update title tooltip with used/remaining values
        if (stackSize !== undefined && stackSize > 0)
        {
          // Use stackUsed directly if available, otherwise calculate from stackRemaining
          const stackUsedBytes = taskCurrent.stackUsed !== undefined && typeof taskCurrent.stackUsed === 'number'
            ? taskCurrent.stackUsed
            : (stackRemainingBytes >= 0 ? stackSize - stackRemainingBytes : 0);
          const remainingPct        = (stackRemainingBytes / stackSize) * 100;
          const usedFormatted       = formatSize(stackUsedBytes, 'bytes', true);
          const totalFormatted      = formatSize(stackSize, 'bytes', true);
          const remainingFormatted  = formatSize(stackRemainingBytes, 'bytes', true);
          if (stackUsageContainer)
          {
            stackUsageContainer.setAttribute('aria-label', `Used ${usedFormatted} out of ${totalFormatted} total stack, ${remainingFormatted} (${remainingPct.toFixed(1)}%) remaining`);
            stackUsageContainer.setAttribute('role', 'tooltip');
            stackUsageContainer.setAttribute('data-microtip-position', 'bottom');
          }
        }
      }
    }
  }
}

/**
 * Create a new table row for a task.
 *
 * Creates a complete table row with all cells for the given task information.
 * Does not append the row to the table - caller must do that.
 *
 * @param {string} taskName - The name of the task.
 * @param {Object} taskInfo - The task information object from the /tasks endpoint.
 * @returns {HTMLElement} The created table row element.
 */
function createTableRow(taskName, taskInfo)
{
  const row = document.createElement('tr');

  // Format stack values (only need for percentage and tooltip)
  const stackRemainingBytes = taskInfo.stackRemaining !== undefined ? taskInfo.stackRemaining : 0;
  const stackPct            = formatStackUsedPercentage(taskInfo.stackUsedPct, taskInfo.stackSize);

  // Get core display value
  const coreDisplay = taskInfo.core == INVALID_CORE_VALUE ? '-' : taskInfo.core;

  // Get system task description if this is a system task
  const systemTaskDescription = SYSTEM_TASKS[taskName] || '';

  // Add system-task class to row if this is a system task
  if (systemTaskDescription)
  {
    row.classList.add('system-task');
  }

  // Task name cell with abbr element
  const taskNameCell = document.createElement('td');
  taskNameCell.className = 'panel-table-cell text-left';
  taskNameCell.setAttribute('data-column', 'task-name');
    if (systemTaskDescription)
    {
      const abbr = document.createElement('abbr');
      abbr.textContent = taskName;
      abbr.setAttribute('aria-label', systemTaskDescription);
      abbr.setAttribute('role', 'tooltip');
      abbr.setAttribute('data-microtip-position', 'bottom');
      taskNameCell.appendChild(abbr);
    }
  else
  {
    taskNameCell.textContent = taskName;
  }
  row.appendChild(taskNameCell);

  // Core cell
  const coreCell = document.createElement('td');
  coreCell.className = 'panel-table-cell text-center';
  coreCell.setAttribute('data-column', 'core');
  coreCell.textContent = coreDisplay;
  row.appendChild(coreCell);

  // Priority cell
  const prioCell = document.createElement('td');
  prioCell.className = 'panel-table-cell text-center';
  prioCell.setAttribute('data-column', 'priority');
  prioCell.textContent = taskInfo.prio;
  row.appendChild(prioCell);

  // CPU Usage cell (progress bar)
  const cpuUsageCell = document.createElement('td');
  cpuUsageCell.className = 'panel-table-cell text-right';
  cpuUsageCell.setAttribute('data-column', 'cpu-usage');
  const cpuUsageContainer = document.createElement('div');
  cpuUsageContainer.className = 'progress-container progress-container-sm';
  // Initial tooltip will be set when telemetry updates
  cpuUsageContainer.setAttribute('aria-label', `${taskName}: - % CPU usage`);
  cpuUsageContainer.setAttribute('role', 'tooltip');
  cpuUsageContainer.setAttribute('data-microtip-position', 'bottom');
  const cpuUsageBar = document.createElement('div');
  cpuUsageBar.className = 'progress-bar';
  cpuUsageBar.setAttribute('data-column', 'cpu-usage-bar');
  cpuUsageBar.style.width = '3%';
  cpuUsageContainer.appendChild(cpuUsageBar);
  cpuUsageCell.appendChild(cpuUsageContainer);
  row.appendChild(cpuUsageCell);

  // CPU % cell - show '-' initially (will be updated by telemetry)
  const cpuPctCell = document.createElement('td');
  cpuPctCell.className = 'panel-table-cell text-right';
  cpuPctCell.setAttribute('data-column', 'cpu-pct');
  cpuPctCell.textContent = '-';
  row.appendChild(cpuPctCell);

  // Stack Usage cell (progress bar) - only create if we have valid stack usage data
  const hasValidStackUsage = stackPct.display !== '-';
  if (hasValidStackUsage)
  {
    const stackUsageCell = document.createElement('td');
    stackUsageCell.className = 'panel-table-cell text-right';
    stackUsageCell.setAttribute('data-column', 'stack-usage');
    const stackUsageContainer = document.createElement('div');
    stackUsageContainer.className = 'progress-container progress-container-sm';
    const stackUsageBar = document.createElement('div');
    stackUsageBar.className = 'progress-bar';
    stackUsageBar.setAttribute('data-column', 'stack-usage-bar');
    stackUsageBar.style.width = '3%';

    // Set initial tooltip with used/remaining values on container
    if (taskInfo.stackSize !== undefined && taskInfo.stackSize > 0)
    {
      // Use stackUsed directly if available, otherwise calculate from stackRemaining
      const stackUsedBytes = taskInfo.stackUsed !== undefined && typeof taskInfo.stackUsed === 'number'
        ? taskInfo.stackUsed
        : (stackRemainingBytes >= 0 ? taskInfo.stackSize - stackRemainingBytes : 0);
      const remainingPct = (stackRemainingBytes / taskInfo.stackSize) * 100;
      const usedFormatted = formatSize(stackUsedBytes, 'bytes', true);
      const totalFormatted = formatSize(taskInfo.stackSize, 'bytes', true);
      const remainingFormatted = formatSize(stackRemainingBytes, 'bytes', true);
      stackUsageContainer.setAttribute('aria-label', `Used ${usedFormatted} out of ${totalFormatted} total stack, ${remainingFormatted} (${remainingPct.toFixed(1)}%) remaining`);
      stackUsageContainer.setAttribute('role', 'tooltip');
      stackUsageContainer.setAttribute('data-microtip-position', 'bottom');
    }

    stackUsageContainer.appendChild(stackUsageBar);
    stackUsageCell.appendChild(stackUsageContainer);
    row.appendChild(stackUsageCell);
  }
  else
  {
    // Create empty cell to maintain table structure
    const stackUsageCell = document.createElement('td');
    stackUsageCell.className = 'panel-table-cell text-right';
    stackUsageCell.setAttribute('data-column', 'stack-usage');
    row.appendChild(stackUsageCell);
  }

  // Stack usage percentage cell
  const usagePctCell = document.createElement('td');
  usagePctCell.className = 'panel-table-cell text-right';
  if (stackPct.warning)
  {
    usagePctCell.classList.add('stack-low');
  }
  usagePctCell.setAttribute('data-column', 'stack-usage-pct');
  usagePctCell.textContent = stackPct.display;
  row.appendChild(usagePctCell);

  return row;
}

/**
 * Update an existing table row with new task information.
 *
 * Updates only the cells that come from the /tasks endpoint data.
 * Does not update CPU % or CPU Usage (those come from telemetry).
 * Does not update stack usage % if it has a telemetry value (preserves it).
 *
 * @param {HTMLElement} row - The existing table row element to update.
 * @param {string} taskName - The name of the task.
 * @param {Object} taskInfo - The task information object from the /tasks endpoint.
 */
function updateTableRow(row, taskName, taskInfo)
{
  // Format stack values (only need for percentage)
  const stackPct = formatStackUsedPercentage(taskInfo.stackUsedPct, taskInfo.stackSize);

  // Get core display value
  const coreDisplay = taskInfo.core == INVALID_CORE_VALUE ? '-' : taskInfo.core;

  // Get system task description if this is a system task
  const systemTaskDescription = SYSTEM_TASKS[taskName] || '';

  // Update system-task class
  if (systemTaskDescription)
  {
    row.classList.add('system-task');
  }
  else
  {
    row.classList.remove('system-task');
  }

  // Update task name cell (handle abbr element)
  const taskNameCell = row.querySelector('[data-column="task-name"]');
  if (taskNameCell)
  {
    taskNameCell.innerHTML = '';
    if (systemTaskDescription)
    {
      const abbr = document.createElement('abbr');
      abbr.textContent = taskName;
      abbr.setAttribute('aria-label', systemTaskDescription);
      abbr.setAttribute('role', 'tooltip');
      abbr.setAttribute('data-microtip-position', 'bottom');
      taskNameCell.appendChild(abbr);
    }
    else
    {
      taskNameCell.textContent = taskName;
    }
  }

  // Update core cell
  const coreCell = row.querySelector('[data-column="core"]');
  if (coreCell)
  {
    coreCell.textContent = coreDisplay;
  }

  // Update priority cell
  const prioCell = row.querySelector('[data-column="priority"]');
  if (prioCell)
  {
    prioCell.textContent = taskInfo.prio;
  }

  // CPU % and CPU Usage are NOT updated here - they come from telemetry
  // They will be updated by updateTableRowsFromTelemetry()

  // Update stack usage percentage cell
  // Only update if it doesn't have a telemetry value (preserve telemetry values)
  const usagePctCell = row.querySelector('[data-column="stack-usage-pct"]');
  if (usagePctCell)
  {
    const currentText = usagePctCell.textContent.trim();
    // Only update if current value is '-' or looks like it came from task data (not telemetry)
    // Telemetry values have format "X.X %" or "X %", task data has format "X.X%"
    const isTelemetryValue = currentText !== '-' && currentText.includes(' %');
    if (!isTelemetryValue)
    {
      usagePctCell.textContent = stackPct.display;
      // Update warning class
      usagePctCell.classList.remove('stack-low');
      if (stackPct.warning)
      {
        usagePctCell.classList.add('stack-low');
      }
    }
  }

  // Stack Usage progress bar is NOT updated here - it comes from telemetry
  // It will be updated by updateTableRowsFromTelemetry()
}

/**
 * Update all table rows from telemetry data.
 *
 * Updates all table rows with the latest telemetry data.
 * This function should be called periodically to keep the table up to date.
 *
 * @param {Object} telemetryCurrent - The telemetry data for all tasks.
 */
function updateTableRowsFromTelemetry(telemetryCurrent)
{
  const tbody = document.querySelector('#taskTable tbody');
  if (!tbody)
  {
    return;
  }

  const rows = tbody.querySelectorAll('tr');
  for (const row of rows)
  {
    // Get task name from task-name data column
    const taskNameCell = row.querySelector('[data-column="task-name"]');
    if (!taskNameCell)
    {
      continue;
    }
    const taskName = taskNameCell.textContent.trim();
    if (!taskName)
    {
      continue;
    }

    // Get telemetry data for this task
    const taskCurrent = telemetryCurrent[taskName];
    if (!taskCurrent)
    {
      continue;
    }

    // Update CPU columns for all tasks (including system tasks)
    updateTableRowCpu(row, taskCurrent);

    // Only update stack-related columns for registered tasks
    if (AppState.data.registeredTasks.has(taskName))
    {
      updateTableRowStack(row, taskCurrent, taskName);
    }
  }
}

/**
 * Periodically refresh task info table.
 *
 * Periodically fetches the latest task information from the server and updates the task info table in the UI.
 * Uses incremental updates to preserve telemetry values and avoid unnecessary DOM operations.
 * Also updates the set of registered tasks, detects removed or added tasks, and refreshes the displayed stack usage,
 * warning classes, and associated tooltips for each task row accordingly. Handles communication errors and keeps
 * internal state in sync with the actual task list on the device.
 */
async function updateTable()
{
  try
  {
    const response = await fetch(API_ROUTES.TASKS);
    if (!response.ok)
    {
      AppState.status.consecutiveFailures++;
      updateStatusPopup();
      throw new Error("Fetch failed");
    }
    const taskData = await response.json();
    AppState.status.lastTableSuccess = Date.now();
    AppState.status.consecutiveFailures = 0;

    // Sync registeredTasks set and taskInfo with current task list
    // Remove tasks that no longer exist
    const currentTaskNames = new Set(Object.keys(taskData));
    const tasksToRemove = [];
    
    // Find tasks that are no longer present
    for (const taskName of AppState.data.registeredTasks)
    {
      if (!currentTaskNames.has(taskName))
      {
        tasksToRemove.push(taskName);
      }
    }
    
    // Remove tasks that no longer exist
    for (const taskName of tasksToRemove)
    {
      AppState.data.registeredTasks.delete(taskName);
      delete AppState.data.taskInfo[taskName];
    }
    
    // Update taskInfo and registeredTasks for current tasks
    AppState.data.taskInfo = {};
    AppState.data.registeredTasks.clear();
    for (const [taskName, taskInfo] of Object.entries(taskData))
    {
      AppState.data.taskInfo[taskName] = taskInfo;
      if (taskInfo.stackSize !== undefined && taskInfo.stackSize > 0)
      {
        AppState.data.registeredTasks.add(taskName);
      }
    }

    const tbody = document.querySelector('#taskTable tbody');
    if (!tbody)
    {
      return;
    }

    // Build a map of existing rows by task name
    const existingRows = tbody.querySelectorAll('tr');
    const rowMap = new Map();
    for (const row of existingRows)
    {
      const taskNameCell = row.querySelector('[data-column="task-name"]');
      if (taskNameCell)
      {
        const taskName = taskNameCell.textContent.trim();
        if (taskName)
        {
          rowMap.set(taskName, row);
        }
      }
    }

    // Separate tasks into system and non-system tasks
    const nonSystemTasks = [];
    const systemTasks = [];
    for (const [taskName, taskInfo] of Object.entries(taskData))
    {
      const isSystemTask = SYSTEM_TASKS.hasOwnProperty(taskName);
      if (isSystemTask)
      {
        systemTasks.push([taskName, taskInfo]);
      }
      else
      {
        nonSystemTasks.push([taskName, taskInfo]);
      }
    }

    // Combine both non-system and system tasks into one array with a flag
    const allTasks = [
      ...nonSystemTasks.map(([taskName, taskInfo]) => ({ taskName, taskInfo, isSystem: false })),
      ...systemTasks.map(([taskName, taskInfo]) => ({ taskName, taskInfo, isSystem: true })),
    ];

    for (const { taskName, taskInfo, isSystem } of allTasks)
    {
      const existingRow = rowMap.get(taskName);
      if (existingRow)
      {
        // Row exists - update it incrementally
        updateTableRow(existingRow, taskName, taskInfo);

        // For system tasks, move the row to the bottom if it's not already there
        if (isSystem)
        {
          existingRow.remove();
          tbody.appendChild(existingRow);
        }
        // Remove from map so we know it's been processed
        rowMap.delete(taskName);
      }
      else
      {
        // Row doesn't exist - create a new one (at the bottom for both)
        const newRow = createTableRow(taskName, taskInfo);
        tbody.appendChild(newRow);
      }
    }


    // Remove rows for tasks that no longer exist
    for (const [taskName, row] of rowMap.entries())
    {
      row.remove();
    }

    // Initialize or refresh Tablesort
    const tableElement = document.getElementById('taskTable');
    if (tableElement && typeof Tablesort !== 'undefined')
    {
      if (!AppState.ui.tableSorter.tasks)
      {
        AppState.ui.tableSorter.tasks = new Tablesort(tableElement);
      }
      else
      {
        AppState.ui.tableSorter.tasks.refresh();
      }
    }
    
    updateStatusPopup();
  }
  catch (error)
  {
    console.error("updateTable error:", error);
    AppState.status.consecutiveFailures++;
    updateStatusPopup();
  }
}

