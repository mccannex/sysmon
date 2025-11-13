/**
 * Display a status notification popup in the UI.
 *
 * Updates and shows the status popup with a message and style corresponding to the given status type.
 * If an invalid or "none" status is provided, the popup is hidden.
 *
 * @param {string} statusType - Key representing the status to display (must match an entry in STATUS_MESSAGES and STATUS_CLASSES).
 */
function showStatusPopup(statusType)
{
  const popup = document.getElementById('statusPopup');
  const statusText = document.getElementById('statusText');

  if (!popup || !statusText)
  {
    return;
  }

  if (statusType === STATUS_TYPES.NONE || !statusType)
  {
    hideStatusPopup();
    return;
  }

  const message = STATUS_MESSAGES[statusType];

  const statusClass = STATUS_CLASSES[statusType];

  if (!message || !statusClass)
  {
    return;
  }

  // Rebuild classes with base classes and new status styles
  popup.className = `status-popup ${statusClass}`;
  statusText.textContent = message;

  // Show popup with animation
  popup.classList.remove('hidden', 'opacity-0', 'translate-y-[-10px]');
  popup.classList.add('opacity-100', 'translate-y-0');

  AppState.status.currentStatus = statusType;
}

/**
 * Hide the status notification popup in the UI.
 *
 * Hides the status popup with a fade-out animation and resets the current status to "none".
 */
function hideStatusPopup()
{
  const popup = document.getElementById('statusPopup');

  if (!popup)
  {
    return;
  }

  // Hide with animation
  popup.classList.add('opacity-0', 'translate-y-[-10px]');

  setTimeout(() =>
  {
    popup.classList.add('hidden');
    AppState.status.currentStatus = STATUS_TYPES.NONE;
  }, 300);
}


/**
 * Format a size value (in bytes) with various formatting options.
 *
 * @param {number} bytes - The size in bytes to format.
 * @param {string} format - Format type: 'bytes' (comma-formatted), 'kb' (rounded KB), or 'auto' (chooses based on size).
 * @param {boolean|string} suffix - If false, no suffix. If true, adds default suffix (' bytes' or ' KB'). If string, uses custom suffix.
 * @returns {string} Formatted size string, or '-' for invalid values.
 */
function formatSize(bytes, format = 'bytes', suffix = false)
{
  if (!Number.isFinite(bytes) || bytes < 0)
  {
    return '-';
  }

  let value;
  let defaultSuffix;

  if (format === 'kb')
  {
    value = Math.round(bytes / BYTES_PER_KB).toLocaleString('en-US');
    defaultSuffix = ' KB';
  }
  else if (format === 'auto')
  {
    if (bytes < BYTES_PER_KB)
    {
      value = bytes.toLocaleString('en-US');
      defaultSuffix = ' bytes';
    }
    else
    {
      value = Math.round(bytes / BYTES_PER_KB).toLocaleString('en-US');
      defaultSuffix = ' KB';
    }
  }
  else // format === 'bytes' (default)
  {
    value = bytes.toLocaleString('en-US');
    defaultSuffix = ' bytes';
  }

  if (suffix === false)
  {
    return String(value);
  }
  else if (suffix === true)
  {
    return `${value}${defaultSuffix}`;
  }
  else // suffix is a string
  {
    return `${value}${suffix}`;
  }
}

/**
 * Format stack remaining with appropriate units (bytes or KB).
 *
 * Formats the given stack remaining bytes value into a string with appropriate units (bytes or KB).
 * Returns an object with the formatted display value, title, and warning flag.
 *
 * @param {number} stackRemainingBytes - The remaining stack size in bytes.
 * @returns {Object} Object containing the formatted display value, title, and warning flag.
 */
function formatStackRemaining(stackRemainingBytes)
{
  if (stackRemainingBytes <= 0)
  {
    return {
      display: '-',
      title  : 'Remaining stack: -',
      warning: false
    };
  }

  const warning = stackRemainingBytes <= STACK_WARNING_THRESHOLD_BYTES;

  // Use 'auto' format to choose bytes or KB based on size
  return {
    display: formatSize(stackRemainingBytes, 'auto', true),
    title  : `Remaining stack: ${formatSize(stackRemainingBytes, 'bytes', true)}`,
    warning: warning
  };
}

/**
 * Format stack allocated value.
 *
 * Formats the given stack size value into a string with appropriate units (bytes or KB).
 * Returns an object with the formatted display value, title, and bytes value.
 *
 * @param {number} stackSize - The stack size in bytes.
 * @returns {Object} Object containing the formatted display value, title, and bytes value.
 */
function formatStackAllocated(stackSize)
{
  if (stackSize === undefined || stackSize <= 0)
  {
    return {
      display: '?',
      title  : 'Stack allocated: unknown',
      bytes  : 0
    };
  }

  return {
    display: formatSize(stackSize, 'kb', true),
    title  : `Stack allocated: ${formatSize(stackSize, 'bytes', true)}`,
    bytes  : stackSize
  };
}

/**
 * Format stack used value (only for registered tasks).
 *
 * Formats the given stack used bytes value into a string with appropriate units (bytes or KB).
 * Returns an object with the formatted display value, title, and bytes value.
 *
 * @param {number} stackUsed - The stack used size in bytes.
 * @param {number} stackSize - The stack size in bytes.
 * @returns {Object} Object containing the formatted display value, title, and bytes value.
 */
function formatStackUsed(stackUsed, stackSize)
{
  const hasRegisteredSize = stackSize !== undefined && stackSize > 0;

  if (!hasRegisteredSize || stackUsed === undefined || stackUsed <= 0)
  {
    return {
      display: '-',
      title  : 'Stack used: -',
      bytes  : 0
    };
  }

  return {
    display: formatSize(stackUsed, 'kb', true),
    title  : `Stack used: ${formatSize(stackUsed, 'bytes', true)}`,
    bytes  : stackUsed
  };
}

/**
 * Format stack usage percentage (only for registered tasks).
 *
 * Formats the given stack used percentage value into a string with appropriate units (bytes or KB).
 * Returns an object with the formatted display value, title, and warning flag.
 *
 * @param {number} stackUsedPct - The stack used percentage value.
 * @param {number} stackSize - The stack size in bytes.
 * @returns {Object} Object containing the formatted display value, title, and warning flag.
 */
function formatStackUsedPercentage(stackUsedPct, stackSize)
{
  const hasRegisteredSize = stackSize !== undefined && stackSize > 0;

  if (!hasRegisteredSize || stackUsedPct === undefined || stackUsedPct <= 0)
  {
    return {
      display: '-',
      title  : 'Stack used percentage - only shown when stack size is registered',
      warning: false
    };
  }

  return {
    display: `${stackUsedPct.toFixed(1)}%`,
    title  : 'Stack used percentage - only shown when stack size is registered',
    warning: stackUsedPct > STACK_PERCENTAGE_WARNING
  };
}

/**
 * Factory function to create a progress bar update function with configurable thresholds.
 *
 * Creates a function that updates a progress bar element's width and applies color classes
 * based on configurable thresholds. For small usage values, applies a visual minimum width
 * so that very low usage is still visible.
 *
 * @param {Object} config - Configuration object with threshold values.
 * @param {number} config.mediumThreshold - Threshold below which is considered "low" (default: 50).
 * @param {number} config.highThreshold - Threshold below which is considered "medium" (default: 80).
 * @returns {Function} A function that updates a progress bar element.
 * @returns {Function.param} barElement - The DOM element representing the progress bar.
 * @returns {Function.param} value - The usage percentage (0-100).
 */
function createProgressBarUpdater(config)
{
  const mediumThreshold = config.mediumThreshold ?? 50;
  const highThreshold = config.highThreshold ?? 80;

  return function(barElement, value)
  {
    if (!barElement)
    {
      return;
    }

    // Clamp value between 0 and 100
    const clampedValue = Math.max(0, Math.min(100, value));

    // Apply visual minimum: if value is > 0 but < 3%, show at least 3% visually
    // This makes small values more visible while keeping the actual percentage accurate
    let visualWidth = clampedValue;
    if (clampedValue > 0 && clampedValue < 3)
    {
      visualWidth = 3;
    }

    // Set width based on visual percentage
    barElement.style.width = `${visualWidth}%`;

    // Remove existing color classes
    barElement.classList.remove('progress-bar-low', 'progress-bar-medium', 'progress-bar-high');

    // Apply color class based on threshold (use actual value, not visual)
    if (clampedValue < mediumThreshold)
    {
      barElement.classList.add('progress-bar-low');
    }
    else if (clampedValue < highThreshold)
    {
      barElement.classList.add('progress-bar-medium');
    }
    else
    {
      barElement.classList.add('progress-bar-high');
    }
  };
}

/**
 * Update a CPU progress bar element to reflect the given usage percentage.
 *
 * Updates the bar's width and applies a color class ('progress-bar-low', 'progress-bar-medium', 'progress-bar-high')
 * based on the value. For small usage values, applies a visual minimum width so that very
 * low usage is still visible.
 *
 * CPU thresholds: low < 50%, medium 50-80%, high >= 80%
 *
 * @param {HTMLElement} barElement - The DOM element representing the CPU progress bar.
 * @param {number} value - The CPU usage percentage (0-100).
 */
const updateCpuProgressBar = createProgressBarUpdater({
  mediumThreshold: 50,
  highThreshold: 80
});

/**
 * Update a DRAM progress bar element to reflect the given usage percentage.
 *
 * Updates the bar's width and applies a color class ('progress-bar-low', 'progress-bar-medium', 'progress-bar-high')
 * based on the value. For small usage values, applies a visual minimum width so that very
 * low usage is still visible.
 *
 * Memory thresholds: low < 70%, medium 70-90%, high >= 90%
 *
 * @param {HTMLElement} barElement - The DOM element representing the DRAM progress bar.
 * @param {number} value - The DRAM usage percentage (0-100).
 */
const updateDramProgressBar = createProgressBarUpdater({
  mediumThreshold: 70,
  highThreshold: 90
});

/**
 * Update a PSRAM progress bar element to reflect the given usage percentage.
 *
 * Updates the bar's width and applies a color class ('progress-bar-low', 'progress-bar-medium', 'progress-bar-high')
 * based on the value. For small usage values, applies a visual minimum width so that very
 * low usage is still visible.
 *
 * Memory thresholds: low < 70%, medium 70-90%, high >= 90%
 *
 * @param {HTMLElement} barElement - The DOM element representing the PSRAM progress bar.
 * @param {number} value - The PSRAM usage percentage (0-100).
 */
const updatePsramProgressBar = createProgressBarUpdater({
  mediumThreshold: 70,
  highThreshold: 90
});

/**
 * Update a Flash progress bar element to reflect the given usage percentage.
 *
 * Updates the bar's width and applies a color class ('progress-bar-low', 'progress-bar-medium', 'progress-bar-high')
 * based on the value. For small usage values, applies a visual minimum width so that very
 * low usage is still visible.
 *
 * Flash thresholds: low < 70%, medium 70-90%, high >= 90%
 *
 * @param {HTMLElement} barElement - The DOM element representing the Flash progress bar.
 * @param {number} value - The Flash usage percentage (0-100).
 */
const updateFlashProgressBar = createProgressBarUpdater({
  mediumThreshold: 70,
  highThreshold: 90
});

/**
 * Update flash partition size warning indicator.
 *
 * Shows a warning icon when there's significant unused flash space available (>5% of total flash
 * or >256KB unused). This indicates the partition table could be resized to use more available space.
 * A few percent unused is reasonable for safety margins, but more than that is wasteful.
 *
 * @param {Object} flashSummary - Flash summary object with totalFlash, unused, unusedPct.
 * @param {Array} partitions - Array of partition objects to find app partition usage.
 */
function updateFlashWarning(flashSummary, partitions)
{
  const warningIcon = document.getElementById('flashWarningIcon');
  const flashUnusedEl = document.getElementById('flashUnused');
  if (!warningIcon || !flashSummary)
  {
    if (warningIcon)
    {
      warningIcon.classList.add('hidden');
    }
    if (flashUnusedEl)
    {
      flashUnusedEl.classList.remove('theme-warning-text');
    }
    return;
  }

  const unusedPct = flashSummary.unusedPct !== undefined ? flashSummary.unusedPct : 0;
  const unusedBytes = flashSummary.unused || 0;
  const totalFlash = flashSummary.totalFlash || 0;

  // Show warning if there's significant unused flash space:
  // >5% of total flash OR >256KB unused
  // A few percent is reasonable for safety margins, but more is wasteful
  const shouldShowWarning = unusedPct > 5 || unusedBytes > (1024 * 256);

  if (shouldShowWarning)
  {
    warningIcon.classList.remove('hidden');
    const unusedSize = formatSize(unusedBytes, 'bytes', true);
    const totalSize = formatSize(totalFlash, 'bytes', true);
    
    // Warning message about unallocated flash space
    const warningMessage = `${unusedSize} (${unusedPct.toFixed(1)}%) of total flash (${totalSize}) is not allocated to any partition and is effectively wasted. Consider resizing your partition table to use more available space.`;
    
    warningIcon.setAttribute('aria-label', warningMessage);
    warningIcon.setAttribute('role', 'tooltip');
    warningIcon.setAttribute('data-microtip-position', 'bottom-left');
    
    // Apply warning color to unused value - get fresh reference to ensure element exists
    const flashUnusedElement = document.getElementById('flashUnused');
    if (flashUnusedElement)
    {
      flashUnusedElement.classList.add('theme-warning-text');
      // Update aria-label to include warning explanation
      const unusedSizeBytes = formatSize(unusedBytes, 'bytes', true);
      flashUnusedElement.setAttribute('aria-label', `${unusedSizeBytes}. Significant unused flash space is not allocated to any partition and is effectively wasted. Consider resizing your partition table to use more available space.`);
    }
  }
  else
  {
    warningIcon.classList.add('hidden');
    // Get fresh reference to ensure we're removing from the right element
    const flashUnusedElement = document.getElementById('flashUnused');
    if (flashUnusedElement)
    {
      flashUnusedElement.classList.remove('theme-warning-text');
      // Restore simple aria-label (will be updated by updateFlashSummary if called)
      const unusedSizeBytes = formatSize(unusedBytes, 'bytes', true);
      flashUnusedElement.setAttribute('aria-label', unusedSizeBytes);
    }
  }
}

/**
 * Update flash summary information in the UI.
 *
 * Updates the overall flash usage progress bar and statistics (total flash, partitions, unused)
 * based on the flash summary data.
 *
 * @param {Object} flashSummary - Flash summary object with totalFlash, totalPartitions, unused, unusedPct, partitionsPct.
 */
function updateFlashSummary(flashSummary)
{
  if (!flashSummary)
  {
    return;
  }

  const flashTotalEl      = document.getElementById('flashTotal');
  const flashPartitionsEl = document.getElementById('flashPartitions');
  const flashUnusedEl     = document.getElementById('flashUnused');
  const flashPartitionsPctEl = document.getElementById('flashPartitionsPct');
  const flashPartitionsBar  = document.getElementById('flashPartitionsBar');
  const flashPartitionsContainer = flashPartitionsBar ? flashPartitionsBar.closest('.progress-container') : null;

  // Update text elements
  if (flashTotalEl)
  {
    flashTotalEl.textContent = formatSize(flashSummary.totalFlash, 'kb', true);
    flashTotalEl.setAttribute('aria-label', formatSize(flashSummary.totalFlash, 'bytes', true));
    flashTotalEl.setAttribute('role', 'tooltip');
    flashTotalEl.setAttribute('data-microtip-position', 'bottom-left');
  }
  if (flashPartitionsEl)
  {
    flashPartitionsEl.textContent = formatSize(flashSummary.totalPartitions, 'kb', true);
    flashPartitionsEl.setAttribute('aria-label', formatSize(flashSummary.totalPartitions, 'bytes', true));
    flashPartitionsEl.setAttribute('role', 'tooltip');
    flashPartitionsEl.setAttribute('data-microtip-position', 'bottom');
  }
  if (flashUnusedEl)
  {
    flashUnusedEl.textContent = formatSize(flashSummary.unused, 'kb', true);
    flashUnusedEl.setAttribute('aria-label', formatSize(flashSummary.unused, 'bytes', true));
    flashUnusedEl.setAttribute('role', 'tooltip');
    flashUnusedEl.setAttribute('data-microtip-position', 'bottom');
    
    // Preserve warning class if it was already set (will be re-evaluated by updateFlashWarning)
    // Don't remove it here - let updateFlashWarning handle the warning state
  }
  if (flashPartitionsPctEl && flashSummary.partitionsPct !== undefined)
  {
    flashPartitionsPctEl.textContent = `${flashSummary.partitionsPct.toFixed(1)} %`;
  }

  // Update progress bar (showing partitions percentage)
  if (flashPartitionsBar && flashSummary.partitionsPct !== undefined)
  {
    updateFlashProgressBar(flashPartitionsBar, flashSummary.partitionsPct);
    // Update tooltip with partitions/unused/total on container
    const flashPartitionsContainer = flashPartitionsBar ? flashPartitionsBar.closest('.progress-container') : null;
    if (flashPartitionsContainer)
    {
      flashPartitionsContainer.setAttribute('aria-label', `Flash: ${formatSize(flashSummary.totalPartitions, 'bytes', true)} partitions (${flashSummary.partitionsPct.toFixed(1)}%), ${formatSize(flashSummary.unused, 'bytes', true)} unused, ${formatSize(flashSummary.totalFlash, 'bytes', true)} total`);
      flashPartitionsContainer.setAttribute('role', 'tooltip');
      flashPartitionsContainer.setAttribute('data-microtip-position', 'bottom');
    }
  }
}

/**
 * Create or update a partition info row element.
 *
 * Creates a new partition info row with partition name, progress bar, and percentage value, or updates an existing one.
 * Handles partitions with and without usage data.
 *
 * @param {HTMLElement} container - Container element to add/update partition rows.
 * @param {Object} partition - Partition object with label, size, and optionally used, free, usedPct.
 * @param {number} index - Index of the partition in the list.
 */
function updatePartitionProgressBar(container, partition, index)
{
  if (!container || !partition)
  {
    return;
  }

  // Find existing partition row or create new one
  let partitionRow = container.querySelector(`[data-partition-index="${index}"]`);
  
  if (!partitionRow)
  {
    // Create new partition info row
    partitionRow = document.createElement('div');
    partitionRow.className = 'info-row';
    partitionRow.setAttribute('data-partition-index', index);
    container.appendChild(partitionRow);
  }

  // Clear existing content
  partitionRow.innerHTML = '';

  // Format address as hex if it's a number
  let addressStr = 'N/A';
  if (partition.address !== undefined && partition.address !== null)
  {
    if (typeof partition.address === 'number')
    {
      addressStr = `0x${partition.address.toString(16)}`;
    }
    else if (typeof partition.address === 'string')
    {
      addressStr = partition.address;
    }
  }

  // Partition name label with abbr element
  const partitionLabel = partition.label || `Partition ${index + 1}`;
  const partitionDescription = PARTITION_DESCRIPTIONS[partitionLabel] || '';
  
  let partitionLabelEl;
  if (partitionDescription)
  {
    // Combine partition description with address and size info
    const combinedTooltip = `${partitionDescription} Address: ${addressStr}, Size: ${formatSize(partition.size, 'bytes', true)}`;
    partitionLabelEl = document.createElement('abbr');
    partitionLabelEl.className = 'info-label';
    partitionLabelEl.textContent = partitionLabel;
    partitionLabelEl.setAttribute('aria-label', combinedTooltip);
    partitionLabelEl.setAttribute('role', 'tooltip');
    partitionLabelEl.setAttribute('data-microtip-position', 'bottom');
  }
  else
  {
    partitionLabelEl = document.createElement('abbr');
    partitionLabelEl.className = 'info-label';
    partitionLabelEl.textContent = partitionLabel;
    partitionLabelEl.setAttribute('aria-label', `Partition: ${partitionLabel}, Address: ${addressStr}, Size: ${formatSize(partition.size, 'bytes', true)}`);
    partitionLabelEl.setAttribute('role', 'tooltip');
    partitionLabelEl.setAttribute('data-microtip-position', 'bottom');
  }
  
  partitionRow.appendChild(partitionLabelEl);

  // Create progress bar container and bar (only if usage data is available)
  // Check if usage is available and usedPct is a valid number
  const hasUsageData = partition.usageAvailable !== false && 
                       partition.usedPct !== undefined && 
                       typeof partition.usedPct === 'number' &&
                       Number.isFinite(partition.usedPct);
  
  if (hasUsageData)
  {
    const usedPct = partition.usedPct;
    const used = partition.used || 0;
    const free = partition.free || 0;
    
    const progressContainer = document.createElement('div');
    progressContainer.className = 'progress-container progress-container-lg flex-1';
    
    const progressBar = document.createElement('div');
    progressBar.className = 'progress-bar';
    
    progressContainer.appendChild(progressBar);
    partitionRow.appendChild(progressContainer);

    // Update progress bar
    updateFlashProgressBar(progressBar, usedPct);

    // Set tooltip on container (container is always full width and hoverable)
    const freePct = partition.size > 0 ? (free / partition.size) * 100 : 0;
    progressContainer.setAttribute('aria-label', `${partition.label}: ${formatSize(used, 'bytes', true)} used (${usedPct.toFixed(1)}%), ${formatSize(free, 'bytes', true)} free (${freePct.toFixed(1)}%), ${formatSize(partition.size, 'bytes', true)} total`);
    progressContainer.setAttribute('role', 'tooltip');
    progressContainer.setAttribute('data-microtip-position', 'bottom');

    // Percent Value
    const percentValueEl = document.createElement('span');
    percentValueEl.className = 'info-value';
    percentValueEl.textContent = `${usedPct.toFixed(1)} %`;
    partitionRow.appendChild(percentValueEl);
  }
  else
  {
    // No usage data - show dash for progress bar
    const progressContainer = document.createElement('div');
    progressContainer.className = 'progress-container progress-container-lg flex-1';
    partitionRow.appendChild(progressContainer);

    const percentValueEl = document.createElement('span');
    percentValueEl.className = 'info-value';
    percentValueEl.textContent = '-';
    partitionRow.appendChild(percentValueEl);
  }
}

/**
 * Update all partition info rows in the UI.
 *
 * Clears existing partition rows and creates new ones for all partitions.
 * Handles partitions with and without usage data.
 *
 * @param {Array} partitions - Array of partition objects.
 */
function updatePartitionsList(partitions)
{
  const partitionsContainer = document.getElementById('partitionsContainer');
  if (!partitionsContainer)
  {
    return;
  }

  // Clear existing partitions
  partitionsContainer.innerHTML = '';

  if (!partitions || !Array.isArray(partitions) || partitions.length === 0)
  {
    return;
  }

  // Create info row for each partition
  partitions.forEach((partition, index) => {
    updatePartitionProgressBar(partitionsContainer, partition, index);
  });
}

/**
 * Fetch and display hardware information (static data, fetched once).
 *
 * Retrieves hardware information from the server and updates the corresponding
 * fields in the UI. This includes chip model, chip features, ESP-IDF version, and
 * compile time details.
 *
 * @function updateHardwareInfo
 * @returns {Promise<void>}
 */
async function updateHardwareInfo()
{
  try
  {
    const response = await fetch(API_ROUTES.HARDWARE);
    if (!response.ok)
    {
      console.warn("Failed to fetch hardware info:", response.status);
      return;
    }
    const hardwareData = await response.json();

    // Extract and apply configuration values from backend
    if (hardwareData.config)
    {
      if (hardwareData.config.cpuSamplingIntervalMs !== undefined)
      {
        CHART_TELEMETRY_UPDATE_INTERVAL_MS = hardwareData.config.cpuSamplingIntervalMs;
      }
      if (hardwareData.config.sampleCount !== undefined)
      {
        CHART_SAMPLE_COUNT = hardwareData.config.sampleCount;
      }
    }

    // Update chip information
    const chipModel    = document.getElementById('hwChipModel');
    const chipCores    = document.getElementById('hwChipCores');
    const chipCpuFreq  = document.getElementById('hwChipCpuFreq');
    const chipFeatures = document.getElementById('hwChipFeatures');

    if (chipModel && hardwareData.chip)
    {
      // Show variant if available, otherwise show base model, combined with revision
      const displayModel = hardwareData.chip.variant || hardwareData.chip.model || '-';
      const revision = hardwareData.chip.revision !== undefined ? ` v0.${hardwareData.chip.revision}` : '';
      chipModel.textContent = displayModel + revision;
    }
    if (chipCores && hardwareData.chip)
    {
      chipCores.textContent = hardwareData.chip.cores || '-';
    }
    if (chipCpuFreq && hardwareData.chip)
    {
      const cpuFreqMHz = hardwareData.chip.cpuFreqMHz;
      chipCpuFreq.textContent = cpuFreqMHz !== undefined ? `${cpuFreqMHz} MHz` : '-';
    }
    if (chipFeatures && hardwareData.chip && hardwareData.chip.features)
    {
      chipFeatures.textContent = hardwareData.chip.features.join(', ') || 'None';
    }

    // Update system information
    const idfVersion  = document.getElementById('hwIdfVersion');
    const compileTime = document.getElementById('hwCompileTime');
    const bootTime    = document.getElementById('hwBootTime');

    if (idfVersion && hardwareData.system)
    {
      idfVersion.textContent = hardwareData.system.idfVersion || '-';
    }
    if (compileTime && hardwareData.system)
    {
      compileTime.textContent = hardwareData.system.compileTime || '-';
    }
    if (bootTime && hardwareData.system)
    {
      const bootTimeValue = hardwareData.system.bootTime || '-';
      
      // Check if boot time is Unix epoch (Jan 1 1970)
      // Format is now "MMM DD YYYY HH:MM:SS" (e.g., "Jan 01 1970 00:00:00")
      const isEpoch = (bootTimeValue.includes('Jan') && bootTimeValue.includes('1970')) || 
                      bootTimeValue === 'Time not set' || bootTimeValue === 'Time not available';
      
      if (isEpoch && bootTimeValue !== '-')
      {
        // Clear existing content
        bootTime.innerHTML = '';
        
        // Create abbr element with tooltip
        const abbr = document.createElement('abbr');
        abbr.textContent = bootTimeValue;
        abbr.setAttribute('aria-label', 'System time appears to be unset (Unix epoch). Consider implementing NTP timekeeping to synchronize with network time servers.');
        abbr.setAttribute('role', 'tooltip');
        abbr.setAttribute('data-microtip-position', 'top');
        abbr.style.textDecoration = 'underline';
        abbr.style.textDecorationStyle = 'dotted';
        abbr.style.cursor = 'help';
        
        bootTime.appendChild(abbr);
      }
      else
      {
        bootTime.textContent = bootTimeValue;
      }
    }

    // Update memory information
    const dramTotal = document.getElementById('hwDramTotal');
    if (dramTotal && hardwareData.memory)
    {
      dramTotal.textContent = formatSize(hardwareData.memory.dramTotal, 'kb', true);
      dramTotal.setAttribute('aria-label', formatSize(hardwareData.memory.dramTotal, 'bytes', true));
      dramTotal.setAttribute('role', 'tooltip');
      dramTotal.setAttribute('data-microtip-position', 'bottom');
    }

    // PSRAM information (if present)
    const psramRow      = document.getElementById('hwPsramRow');
    const psramSpeedRow = document.getElementById('hwPsramSpeedRow');
    const psramTotal    = document.getElementById('hwPsramTotal');
    const psramSpeed    = document.getElementById('hwPsramSpeed');

    if (hardwareData.memory && hardwareData.memory.psramTotal > 0)
    {
      if (psramRow)
      {
        psramRow.classList.remove('hidden');
      }
      if (psramTotal)
      {
        psramTotal.textContent = formatSize(hardwareData.memory.psramTotal, 'kb', true);
        psramTotal.setAttribute('aria-label', formatSize(hardwareData.memory.psramTotal, 'bytes', true));
        psramTotal.setAttribute('role', 'tooltip');
        psramTotal.setAttribute('data-microtip-position', 'bottom');
      }

      if (hardwareData.memory.psramSpeed !== undefined && hardwareData.memory.psramSpeed > 0)
      {
        if (psramSpeedRow)
        {
          psramSpeedRow.classList.remove('hidden');
        }
        if (psramSpeed)
        {
          psramSpeed.textContent = `${hardwareData.memory.psramSpeed} MHz`;
        }
      }
      else
      {
        if (psramSpeedRow)
        {
          psramSpeedRow.classList.add('hidden');
        }
      }
    }
    else
    {
      if (psramRow)
      {
        psramRow.classList.add('hidden');
      }
      if (psramSpeedRow)
      {
        psramSpeedRow.classList.add('hidden');
      }
    }

    // Update flash summary and partition information
    if (hardwareData.flashSummary)
    {
      updateFlashSummary(hardwareData.flashSummary);
    }
    
    if (hardwareData.partitions && Array.isArray(hardwareData.partitions))
    {
      updatePartitionsList(hardwareData.partitions);
    }
    
    // Update warning indicator if flash summary is available (always check, even without partitions)
    if (hardwareData.flashSummary)
    {
      updateFlashWarning(hardwareData.flashSummary, hardwareData.partitions);
    }

    // Update WiFi information
    if (hardwareData.wifi)
    {
      updateWifiInfo(hardwareData.wifi);
    }
    
    // Update sampling configuration info text with sample count and interval
    if (typeof updateSamplingConfigInfo === 'function')
    {
      updateSamplingConfigInfo();
    }
  }
  catch (error)
  {
    console.warn("Failed to fetch hardware info:", error);
  }
}

/**
 * Update WiFi information display in the header.
 *
 * Updates the WiFi SSID, RSSI signal strength, and IP:PORT address
 * in the header WiFi info container.
 *
 * @function updateWifiInfo
 * @param {Object} wifiData - WiFi information object from hardware endpoint
 * @param {string} wifiData.ssid - WiFi SSID
 * @param {number|null} wifiData.rssi - Signal strength in dBm
 * @param {string} wifiData.ip - IP address
 * @param {number} wifiData.port - HTTP server port
 * @returns {void}
 */
/**
 * Update WiFi RSSI icon based on signal strength.
 *
 * Updates the Material Icon to show signal strength visually:
 * - "wifi" (3 bars) for RSSI >= -70 dBm (good connection)
 * - "wifi_2_bar" (2 bars) for RSSI < -70 dBm (shaky connection)
 * - Hides icon if RSSI is unavailable
 *
 * @function updateWifiRssi
 * @param {number|null|undefined} rssi - WiFi signal strength in dBm
 * @returns {void}
 */
function getSignalQuality(rssi)
{
  if (rssi >= -50)
  {
    return 'Excellent';
  }
  else if (rssi >= -60)
  {
    return 'Good';
  }
  else if (rssi >= -70)
  {
    return 'Fair';
  }
  else if (rssi >= -80)
  {
    return 'Poor';
  }
  else
  {
    return 'Very Poor';
  }
}

function updateWifiRssi(rssi)
{
  const rssiElement = document.getElementById('wifiRssi');
  const wifiInfoElement = document.getElementById('wifiInfo');
  
  if (!rssiElement || !wifiInfoElement)
  {
    return;
  }

  if (rssi !== null && rssi !== undefined)
  {
    // Show icon based on signal strength
    if (rssi >= -70)
    {
      rssiElement.textContent = 'wifi';
      rssiElement.style.display = '';
    }
    else
    {
      rssiElement.textContent = 'wifi_2_bar';
      rssiElement.style.display = '';
    }
    
    // Update tooltip with RSSI value and quality indicator
    const quality = getSignalQuality(rssi);
    wifiInfoElement.setAttribute('aria-label', `Signal strength: ${rssi} dBm (${quality})`);
  }
  else
  {
    // Show wifi_off icon if RSSI unavailable
    rssiElement.textContent = 'wifi_off';
    rssiElement.style.display = '';
    wifiInfoElement.setAttribute('aria-label', 'Not Connected');
  }
}

function updateWifiInfo(wifiData)
{
  if (!wifiData)
  {
    return;
  }

  // Update SSID
  const ssidElement = document.getElementById('wifiSsid');
  if (ssidElement)
  {
    ssidElement.textContent = wifiData.ssid || '-';
  }

  // Update RSSI icon
  updateWifiRssi(wifiData.rssi);

  // Update IP:PORT address
  const addressElement = document.getElementById('wifiAddress');
  if (addressElement)
  {
    if (wifiData.ip && wifiData.port)
    {
      addressElement.textContent = `${wifiData.ip}:${wifiData.port}`;
    }
    else if (wifiData.ip)
    {
      addressElement.textContent = wifiData.ip;
    }
    else
    {
      addressElement.textContent = '-';
    }
  }
}
