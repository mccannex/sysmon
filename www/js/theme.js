/**
 * Theme management - must run before page render to prevent FOUC.
 *
 * Initializes the theme and sets up event listeners for system theme changes and toggle button clicks.
 * Also exposes the updateChartColors function to the window object for external access.
 * 
 * Internal functions:
 * - getSystemPreference()   - returns the current "prefers-color-scheme" media query match
 * - getStoredTheme()        - returns the stored theme preference from localStorage
 * - applyColorsToChart()    - applies the theme colors to a chart instance
 * - updateThemeToggleIcon() - updates the theme toggle icon
 * - updateChartColors()     - updates the colors of the chart instances
 * - applyTheme()            - applies the theme to the document element
 * - initializeTheme()       - initializes the theme
 */
(function()
{
  // Helper functions (internal utilities)
  const getSystemPreference = () =>
  {
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
  };

  const getStoredTheme = () =>
  {
    const stored = localStorage.getItem('theme');
    if (stored === 'dark' || stored === 'light')
    {
      return stored === 'dark';
    }
    return null; // No stored preference
  };

  // Helper to apply colors to a single chart instance
  const applyColorsToChart = (chart) =>
  {
    const CONFIG = getChartConfig();
    // Read CSS variables to match microtip styling
    const rootStyle = getComputedStyle(document.documentElement);
    const tooltipBg = rootStyle.getPropertyValue('--color-theme-body-background').trim();
    const tooltipBorder = rootStyle.getPropertyValue('--color-theme-border').trim();
    
    // X-axis grid uses scriptable colors for major/minor tick differentiation
    chart.options.scales.x.grid.color = (context) =>
    {
      // Convert index to actual label value (negative numbers from -99 to 0)
      const labelValue = context.tick.value - (CHART_SAMPLE_COUNT - 1);
      const majorTickInterval = getMajorTickIntervalSamples();
      if (majorTickInterval > 0 && labelValue % majorTickInterval === 0)
      {
        return CONFIG.COLORS.UI.GRID_COLOR_MAJOR;
      }
      return CONFIG.COLORS.UI.GRID_COLOR;
    };
    chart.options.scales.x.grid.lineWidth = (context) =>
    {
      // Convert index to actual label value (negative numbers from -99 to 0)
      const labelValue = context.tick.value - (CHART_SAMPLE_COUNT - 1);
      const majorTickInterval = getMajorTickIntervalSamples();
      if (majorTickInterval > 0 && labelValue % majorTickInterval === 0)
      {
        return 1.5;
      }
      return 0.5;
    };
    chart.options.scales.x.ticks.color            = CONFIG.COLORS.UI.TEXT_COLOR;
    chart.options.scales.x.title.color            = CONFIG.COLORS.UI.TEXT_COLOR;
    chart.options.scales.y.grid.color             = CONFIG.COLORS.UI.GRID_COLOR;
    chart.options.scales.y.ticks.color            = CONFIG.COLORS.UI.TEXT_COLOR;
    chart.options.scales.y.title.color            = CONFIG.COLORS.UI.TEXT_COLOR;
    chart.options.plugins.tooltip.backgroundColor = tooltipBg || CONFIG.COLORS.UI.TOOLTIP_BG;
    chart.options.plugins.tooltip.borderColor     = tooltipBorder || CONFIG.COLORS.UI.GRID_COLOR_MAJOR;
    chart.options.plugins.tooltip.borderWidth     = 1; // Match microtip border-1
    chart.options.plugins.tooltip.titleColor      = CONFIG.COLORS.UI.TOOLTIP_TEXT;
    chart.options.plugins.tooltip.bodyColor       = CONFIG.COLORS.UI.TOOLTIP_TEXT;
    chart.options.plugins.legend.labels.color     = CONFIG.COLORS.UI.TEXT_COLOR;
    chart.update('none');
  };

  // UI update functions
  const updateThemeToggleIcon = (isDark) =>
  {
    const activeIcon   = document.getElementById('themeSwitchActiveIcon');
    const inactiveIcon = document.getElementById('themeSwitchInactiveIcon');
    if (activeIcon && inactiveIcon)
    {
      if (isDark)
      {
        // Dark mode: moon in thumb (right), sun inactive (left)
        activeIcon.textContent   = MATERIAL_ICONS.DARK_MODE;
        inactiveIcon.textContent = MATERIAL_ICONS.LIGHT_MODE;
      }
      else
      {
        // Light mode: sun in thumb (left), moon inactive (right)
        activeIcon.textContent   = MATERIAL_ICONS.LIGHT_MODE;
        inactiveIcon.textContent = MATERIAL_ICONS.DARK_MODE;
      }
    }
  };

  // Define updateChartColors first so it can be called by applyTheme
  const updateChartColors = () =>
  {
    [window.chartInstance, window.memoryChartInstance].forEach(chart =>
    {
      if (chart)
      {
        applyColorsToChart(chart);
      }
    });
  };

  // Core theme functions
  const applyTheme = (isDark) =>
  {
    if (isDark)
    {
      document.documentElement.classList.add('dark');
    }
    else
    {
      document.documentElement.classList.remove('dark');
    }
    updateThemeToggleIcon(isDark);
    updateChartColors(isDark);
  };

  const initializeTheme = () =>
  {
    const storedTheme = getStoredTheme();
    const isDark = storedTheme !== null ? storedTheme : getSystemPreference();
    applyTheme(isDark);
  };

  const toggleTheme = (event) =>
  {
    const isCurrentlyDark = document.documentElement.classList.contains('dark');
    const newTheme = !isCurrentlyDark;
    localStorage.setItem('theme', newTheme ? 'dark' : 'light');
    applyTheme(newTheme);
    
    // Remove focus from the button to hide the tooltip
    // This happens because clicking a button gives it focus, and microtip
    // shows tooltips on both :hover and :focus states
    // Use currentTarget to ensure we blur the button, not a child element
    if (event && event.currentTarget)
    {
      event.currentTarget.blur();
    }
  };

  // Event listeners and initialization
  // Initialize theme immediately (before DOM is ready to prevent FOUC)
  initializeTheme();

  // Listen for system theme changes
  if (window.matchMedia)
  {
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) =>
    {
      // Only apply system preference if user hasn't manually set a preference
      if (getStoredTheme() === null)
      {
        applyTheme(e.matches);
      }
    });
  }

  // Wire up toggle button when DOM is ready
  if (document.readyState === 'loading')
  {
    document.addEventListener('DOMContentLoaded', () =>
    {
      const toggleButton = document.getElementById('themeToggle');
      if (toggleButton)
      {
        toggleButton.addEventListener('click', toggleTheme);
      }
    });
  }
  else
  {
    const toggleButton = document.getElementById('themeToggle');
    if (toggleButton)
    {
      toggleButton.addEventListener('click', toggleTheme);
    }
  }

  // Expose updateChartColors function to window for external access
  window.updateChartColors = updateChartColors;
})();

