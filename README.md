<p align="center">
  <img src="https://winnipegfir.ca/storage/files/uploads/1779257646.png" alt="ESWX" width="180"/>
</p>

<h1 align="center">ESWX — EuroScope Weather Radar</h1>

<p align="center">
Real-time weather radar overlay for EuroScope. Fetches live precipitation data from the RainViewer public API and renders it behind your scope as a blue-intensity overlay — light sky blue for drizzle, dark navy for heavy cells. Draggable control panel, adjustable opacity, and auto-refresh every 5 minutes.
</p>

---

## Features

- Live radar tiles from the [RainViewer](https://www.rainviewer.com/api.html) public API — no key required
- Blue-intensity colour scale: light rain → sky blue, severe cells → dark navy
- Rendered behind all scope content so aircraft, labels, and airways stay readable
- Pixelated (nearest-neighbour) rendering for a classic radar look
- Tile pyramid fallback — old-zoom tiles stay visible while new ones load
- 4 parallel download threads for fast coverage across the visible area
- Auto-refreshes radar frame every 5 minutes
- Draggable **WX / opacity / RF** control panel
- ASR persistence — enabled state, opacity, and panel position saved per sector file

## Control Panel

| Section | Left-click | Right-click |
|---------|-----------|-------------|
| **WX** | Toggle radar on/off | — |
| **60%** | Opacity −10 | Opacity +10 |
| **RF** | Force immediate frame refresh | — |

Drag the panel anywhere on the scope. Position is saved to the ASR file.

## Commands

| Command | Effect |
|---------|--------|
| `.eswx` | Toggle radar on/off |
| `.eswx on` / `.eswx off` | Enable / disable |
| `.eswx opacity <0-100>` | Set opacity |
| `.eswx refresh` | Force frame refresh |

## Building

Requirements:
- Visual Studio 2022 (or Build Tools v143)
- EuroScope SDK — place `EuroScopePlugIn.h` and `EuroScopePlugInDll.lib` in `SDK\`

```
msbuild WeatherRadarPlugin.vcxproj /p:Configuration=Release /p:Platform=Win32
```

Output: `Release\WeatherRadarPlugin.dll`

## Installation

1. Copy `WeatherRadarPlugin.dll` to your EuroScope plugins folder
2. Load via **Other Set** → **Plug-ins** → **Add/Remove**
3. The radar activates automatically on any geo-referenced scope

## Plugin Info

| Field | Value |
|-------|-------|
| Plugin Name | Weather Radar |
| Version | 1.0.0 |
| Author | Nate Power |
| Copyright | Free to use |

## Credits

Radar data provided by [RainViewer](https://www.rainviewer.com) · Free to use
