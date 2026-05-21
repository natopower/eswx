<p align="center">
  <img src="https://winnipegfir.ca/storage/files/uploads/1779257646.png" alt="ESWX" width="300"/>
</p>

# Weather Radar Plugin

EuroScope weather overlay plugin for ESWX. It draws RainViewer radar tiles behind the scope content and live Blitzortung lightning strikes over the radar display.

## Controls

| Panel | Left-click | Right-click |
|---|---|---|
| **WX** | Toggle radar on/off | - |
| **%** | Opacity -10 | Opacity +10 |
| **RF** | Force immediate radar refresh | - |
| **LX** | Toggle lightning strikes on/off | - |

Drag the panel anywhere on the scope. Panel position, radar state, lightning state, and opacity are saved to the ASR file.

## Dot Commands

| Command | Effect |
|---|---|
| `.eswx` | Toggle radar on/off |
| `.eswx on` | Enable radar |
| `.eswx off` | Disable radar |
| `.eswx opacity 70` | Set opacity from 0 to 100 |
| `.eswx refresh` | Force-fetch the latest radar frame |

## Installation

1. Download `WeatherRadarPlugin.dll` from the [latest release](https://github.com/natopower/eswx/releases/latest).
2. Copy it to your EuroScope plugins folder.
3. Load it via **Other Set** -> **Plug-ins** -> **Add/Remove**.
4. Open a georeferenced radar display. The radar enables automatically.

## How It Works

Radar data comes from RainViewer public weather map tiles. The plugin chooses tile detail from the current screen range, caches fetched tiles, and refreshes the radar frame every 5 minutes.

Lightning data comes from the Blitzortung live WebSocket feed. Strikes are decoded, filtered to North America and the Caribbean, then drawn as white cross marks using the same opacity setting as the radar. Strikes stay visible for 10 minutes and shrink as they age.

## Build

Build `WeatherRadarPlugin.vcxproj` as **Release | Win32**. EuroScope is a 32-bit process, so Win32 is required.

The project expects the EuroScope SDK files in `SDK\`, including `EuroScopePlugIn.h` and `EuroScopePlugInDll.lib`. The built DLL is written to `Release\WeatherRadarPlugin.dll`.

## Plugin Info

| Field | Value |
|---|---|
| Plugin Name | Weather Radar |
| Version | 1.0.0 |
| Author | Nate Power |
| Copyright | Free to use |
