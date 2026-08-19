# XY-SK120 Web UI Development Guide

This document explains the components and patterns used in the XY-SK120 Web UI, focusing on how to maintain and extend the interface in a consistent way.

## Table of Contents

- [Tab Component System](#tab-component-system)
- [CSS Implementation with Tailwind](#css-implementation-with-tailwind)
- [Adding New Tabs](#adding-new-tabs)
- [Responsive Design](#responsive-design)
- [Component Reference](#component-reference)
- [Dark Mode Support](#dark-mode-support)
- [WebSocket Handling](#websocket-handling)
- [JavaScript Module Pattern](#javascript-module-pattern)
- [Error Handling Best Practices](#error-handling-best-practices)

## Tab Component System

The Web UI uses a reusable tab component system based on the Meraki UI "Line with Icons" design pattern. The system supports proper accessibility through ARIA attributes and maintains state persistence through localStorage.

### How Tabs Work

The tab system consists of two main parts:

1. **Tab Navigation** - A set of buttons with the `role="tab"` attribute
2. **Tab Panels** - Content areas with the `role="tabpanel"` attribute

Each tab group is contained in a `div` with a `data-tabs` attribute that identifies the group.

### Tab Initialization

Tab groups are initialized automatically during page load via the `initAllTabGroups()` function in `tab_component.js`. The system finds all containers with the `data-tabs` attribute and initializes them.

### Tab State Persistence

The system automatically saves the last active tab for each group in localStorage using:
- `lastActiveTab-{groupId}` for the newer component-based tabs
- `lastActiveTab` and `lastActiveSettingsTab` for backward compatibility with older tab implementations

## CSS Implementation with Tailwind

The Web UI uses Tailwind CSS for styling. Custom components follow these best practices:

### Tab Styling

Tabs use these Tailwind CSS classes for consistent styling:

```html
<button role="tab" aria-selected="false"
    class="inline-flex items-center h-10 px-2 py-2 -mb-px text-center bg-transparent border-b-2 border-transparent sm:px-4 whitespace-nowrap focus:outline-none hover:border-gray-400 dark:hover:border-gray-500 dark:text-gray-400 text-gray-500">
    <svg class="w-4 h-4 mx-1 sm:w-6 sm:h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
        <!-- SVG path data -->
    </svg>
    <span class="mx-1 text-sm sm:text-base">Tab Label</span>
</button>
```

Active tab state:

```html
<button role="tab" aria-selected="true"
    class="inline-flex items-center h-10 px-2 py-2 -mb-px text-center bg-transparent border-b-2 border-blue-500 sm:px-4 whitespace-nowrap focus:outline-none dark:text-blue-300 dark:border-blue-400 text-blue-600">
    <svg class="w-4 h-4 mx-1 sm:w-6 sm:h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
        <!-- SVG path data -->
    </svg>
    <span class="mx-1 text-sm sm:text-base">Active Tab</span>
</button>
```

## Adding New Tabs

You can add new tab groups in two ways:

### 1. HTML-Based Approach (Recommended for Static Content)

```html
<div data-tabs="your-tab-group-id">
    <!-- Tab navigation -->
    <div class="flex overflow-x-auto overflow-y-hidden border-b border-gray-200 whitespace-nowrap dark:border-gray-700" role="tablist" aria-label="Your tab group description">
        <!-- First tab (initially active) -->
        <button id="tab-id-1" role="tab" aria-selected="true" aria-controls="panel-id-1" 
            class="inline-flex items-center h-10 px-2 py-2 -mb-px text-center bg-transparent border-b-2 border-blue-500 sm:px-4 dark:text-blue-300 dark:border-blue-400 text-blue-600 whitespace-nowrap focus:outline-none">
            <svg xmlns="http://www.w3.org/2000/svg" class="w-4 h-4 mx-1 sm:w-6 sm:h-6" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <!-- SVG path data here -->
            </svg>
            <span class="mx-1 text-sm sm:text-base">Tab 1</span>
        </button>
        
        <!-- Second tab (initially inactive) -->
        <button id="tab-id-2" role="tab" aria-selected="false" aria-controls="panel-id-2" 
            class="inline-flex items-center h-10 px-2 py-2 -mb-px text-center bg-transparent border-b-2 border-transparent sm:px-4 dark:text-white text-gray-700 whitespace-nowrap focus:outline-none hover:border-gray-400">
            <svg xmlns="http://www.w3.org/2000/svg" class="w-4 h-4 mx-1 sm:w-6 sm:h-6" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <!-- SVG path data here -->
            </svg>
            <span class="mx-1 text-sm sm:text-base">Tab 2</span>
        </button>
    </div>
    
    <!-- Tab panels -->
    <div id="panel-id-1" role="tabpanel" aria-labelledby="tab-id-1" tabindex="0" aria-hidden="false" class="p-4">
        <!-- Tab 1 content here -->
    </div>
    
    <div id="panel-id-2" role="tabpanel" aria-labelledby="tab-id-2" tabindex="0" aria-hidden="true" class="hidden p-4">
        <!-- Tab 2 content here -->
    </div>
</div>
```

### 2. JavaScript-Based Approach (For Dynamic Content)

You can create tabs programmatically using the `createTabGroup()` helper function:

```javascript
document.addEventListener('DOMContentLoaded', function() {
    // Define your tabs
    const exampleTabs = [
        {
            id: 'tab-example1',
            label: 'Example 1',
            icon: '<svg class="w-4 h-4 mx-1 sm:w-6 sm:h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 14v3m4-3v3m4-3v3M3 21h18M3 10h18M3 7l9-4 9 4M4 10h16v11H4V10z"></path></svg>',
            panelId: 'panel-example1'
        },
        {
            id: 'tab-example2',
            label: 'Example 2',
            icon: '<svg class="w-4 h-4 mx-1 sm:w-6 sm:h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z"></path></svg>',
            panelId: 'panel-example1'
        }
    ];
    
    // Get the container element
    const container = document.getElementById('my-tab-container');
    if (container) {
        // Create and insert the tab HTML
        container.innerHTML = window.createTabGroup('example-tabs', 'example', exampleTabs);
        
        // Add content to the panels
        document.getElementById('panel-example1').innerHTML = '<p>Content for tab 1</p>';
        document.getElementById('panel-example2').innerHTML = '<p>Content for tab 2</p>';
        
        // Initialize the tabs
        window.initTabGroup('example');
    }
});
```

## Responsive Design

The tab component has been designed to be responsive across different screen sizes:

### Mobile (< 640px)
- Smaller icons (16x16px)
- Smaller text (14px) with abbreviations for mode tabs (CV, CC, CP)
- Reduced horizontal padding
- Equal width tabs for mode selection (flex-1)
- Horizontally scrollable tab list for many tabs

### Tablet/Desktop (≥ 640px)
- Larger icons (20x20px or 24x24px)
- Larger text (16px) with full labels
- More horizontal padding
- Still horizontally scrollable if needed

### Mode Tab Optimization

For operating mode tabs, we use abbreviations on mobile to save space:

```html
<button data-mode="cv" class="mode-tab flex-1 ...">
    <svg ... ></svg>
    <span class="mx-1 text-sm sm:text-base">
        <span class="hidden sm:inline">Constant Voltage</span>
        <span class="sm:hidden">CV</span>
    </span>
</button>
```

This approach:
- Shows only "CV", "CC", and "CP" on mobile screens
- Expands to "Constant Voltage", "Constant Current", and "Constant Power" on larger screens
- Uses the `flex-1` class to make all tabs equal width
- Eliminates the need for horizontal scrolling on most devices

### CSS Media Queries

The responsive behavior is achieved through Tailwind's `sm:` prefix which applies styles at screen widths of 640px and above:

```css
/* Base styles (mobile) */
.w-4 h-4 /* 16px icons */
.text-sm /* 14px text */
.px-2 /* 8px horizontal padding */

/* Tablet/desktop styles (640px and above) */
.sm:w-6 sm:h-6 /* 24px icons */
.sm:text-base /* 16px text */
.sm:px-4 /* 16px horizontal padding */
```

### Testing Responsive Behavior

Always test tab behavior at various screen sizes, from small phones (~320px) to large desktops. The tab component should:
- Maintain consistent vertical spacing at all sizes
- Show all content comfortably at larger sizes
- Allow horizontal scrolling to access all tabs on small screens

## Component Reference

### Main Tab Components

The Web UI includes these tab-based components:

1. **Settings Tabs** - For device, UI, WiFi, and device management settings
2. **Mode Tabs** - For switching between Constant Voltage (CV), Constant Current (CC), and Constant Power (CP) modes

### Extending Existing Components

When adding new tabs to existing components, follow the established pattern:

1. Add a new tab button with proper ARIA attributes
2. Add a corresponding tab panel
3. Ensure the new tab uses the same styling classes as existing tabs

## Dark Mode Support

The Web UI fully supports dark mode, which can be toggled in the Web UI settings and is persisted in localStorage.

### Dark Mode-Compatible Styles

When adding new components, ensure they're dark mode compatible by using the Tailwind dark mode classes:

```html
<div class="bg-white dark:bg-gray-800 text-gray-800 dark:text-white">
    <!-- Your component content here -->
</div>
```

Classes to use for consistent dark mode support:

- Background colors: `bg-white dark:bg-gray-800` (primary), `bg-gray-50 dark:bg-gray-700` (secondary)
- Text colors: `text-gray-800 dark:text-white` (primary), `text-gray-600 dark:text-gray-300` (secondary)
- Border colors: `border-gray-200 dark:border-gray-700`
- Form input background: `bg-white dark:bg-gray-700`

## WebSocket Handling

The Web UI uses WebSockets for real-time communication with the XY-SK120 power supply. All WebSocket communication is now consolidated in `data/js/core.js`, which serves as the single source of truth for WebSocket initialization and message handling.

### Core WebSocket Logic (`data/js/core.js`)

*   **`initWebSocket()`:** Initializes the WebSocket connection and sets up event listeners for `open`, `close`, `error`, and `message` events.
*   **`sendCommand(command)`:** Sends a JSON command to the power supply over the WebSocket connection.
*   **`handleMessage(event)`:** Handles incoming WebSocket messages, parses the JSON data, and dispatches custom events for other modules to handle.

### Module Communication

Other JavaScript modules should not directly initialize or interact with the WebSocket connection. Instead, they should:

1.  **Dispatch Custom Events:** When a module needs to send a command to the power supply, it should call the `window.sendCommand(command)` function.
2.  **Handle Custom Events:** Modules should listen for custom events dispatched by `core.js` (e.g., `websocket-message`, `websocket-connected`, `websocket-disconnected`) and update their UI accordingly.

### Example

To send a command from a module:

```javascript
// In your module
function setVoltage(voltage) {
  window.sendCommand({ action: 'setVoltage', voltage: voltage });
}
```

To handle a WebSocket message in a module:

```javascript
// In your module
document.addEventListener('websocket-message', function(event) {
  const data = event.detail;
  if (data.action === 'voltageResponse') {
    // Update UI with the new voltage value
    updateVoltageDisplay(data.voltage);
  }
});
```

### Benefits of this Approach

*   **Centralized WebSocket Logic:** Easier to maintain and debug WebSocket-related issues.
*   **Loose Coupling:** Modules are decoupled from the WebSocket implementation, making the code more modular and testable.
*   **Real-Time Updates:** WebSockets enable real-time updates of power supply status and settings in the UI.

## JavaScript Module Pattern

### Avoiding ES6 Modules

The web interface for XY-SK120 is designed to work directly in the browser without build tools or transpilers. For this reason, we **avoid using ES6 module syntax** (`import`/`export`) which may not be supported in all browser environments, especially when served from an ESP32.

**DON'T** use ES6 module syntax:
```javascript
// Don't do this
export function myFunction() { ... }
import { otherFunction } from './other_file.js';
```

### Using IIFE Pattern

Instead, use the Immediately Invoked Function Expression (IIFE) pattern to create module-like scopes and expose functions via the global `window` object:

**DO** use the IIFE pattern:
```javascript
// Do this instead
(function() {
    // Private scope - variables defined here are not accessible outside
    let privateVariable = 'not accessible outside';
    
    // Function we want to make public
    function publicFunction() {
        console.log('This function will be accessible globally');
    }
    
    // Expose functions to global scope via window object
    window.myModule = {
        publicFunction: publicFunction
    };
})();

// Usage from another file:
window.myModule.publicFunction();
```

## WebSocket Connection Management

### Using window.whenWebsocketReady()

Always use the `window.whenWebsocketReady()` helper method before sending WebSocket commands. This ensures requests are only made when the WebSocket connection is fully established, preventing errors and race conditions.

```javascript
// Correct way to send WebSocket commands
function sendMyCommand() {
    window.whenWebsocketReady(() => {
        // This code will only execute when WebSocket is connected
        window.sendCommand({
            action: 'myAction',
            data: 'myData'
        });
    });
}
```

In contrast to the older approach:

```javascript
// Don't do this - may fail if connection isn't ready
function sendMyCommand() {
    // This might fail if WebSocket isn't connected yet
    if (window.websocketConnected) {
        window.sendCommand({
            action: 'myAction',
            data: 'myData'
        });
    }
}
```

### Handling WebSocket Events

For WebSocket events, use the standard event system:

```javascript
// Listen for WebSocket messages
document.addEventListener('websocket-message', function(event) {
    const data = event.detail;
    if (data.action === 'myResponseAction') {
        // Handle the response
    }
});

// Listen for connection events
document.addEventListener('websocket-connected', function() {
    console.log('WebSocket connected, can now send commands');
});

document.addEventListener('websocket-disconnected', function() {
    console.log('WebSocket disconnected, pause activities requiring connection');
});
```

## Error Handling Best Practices

Always handle promise rejections when using WebSocket communication:

```javascript
fetchDataFromDevice()
    .then(data => {
        // Handle successful response
    })
    .catch(error => {
        console.warn("Error fetching data:", error.message);
        // Show appropriate user feedback
    });
```

By following these patterns, your code will be more robust and work reliably with the XY-SK120 device.

## Icon Usage

The Web UI uses SVG icons for a consistent look. When adding new icons:

1. Use SVG icons with the proper sizing classes: `class="w-4 h-4 sm:w-5 sm:h-5"`
2. Ensure icons use `currentColor` for proper color inheritance
3. Keep stroke width consistent (typically `stroke-width="2"`)

## JavaScript Files Reference

- `tab_component.js` - Main tab implementation and helper functions
- `core.js` - Core functionality for device communication
- `web_interface.js` - Main UI interaction logic
- `connectivity_check.js` - Connection monitoring
- `log_viewer.js` - WebSocket log viewer functionality
