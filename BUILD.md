# Weather Radar Plugin — Build Guide

## Prerequisites

- Visual Studio 2019 or 2022 (Desktop C++ workload, x86 tools)
- EuroScope SDK — two files: `EuroScopePlugIn.h` and `EuroScopePlugInDll.lib`

## Setup

1. Unzip the EuroScope SDK so `EuroScopePlugIn.h` and `EuroScopePlugIn.lib` sit in a folder,
   e.g. `C:\EuroScopeSDK\`

2. Open `WeatherRadarPlugin.vcxproj`. In the project properties (or by editing the vcxproj),
   set the `EuroscopeDir` property to your SDK folder:

   ```
   <EuroscopeDir>C:\EuroScopeSDK\</EuroscopeDir>
   ```

   Make sure the path ends with `\`.

3. Build target: **Release | Win32** (EuroScope is a 32-bit process).

4. The output DLL will be at `Release\WeatherRadarPlugin.dll`.

## Loading in EuroScope

1. Copy `WeatherRadarPlugin.dll` to your EuroScope plugins folder.
2. In EuroScope → Other SET → Plug-ins → Add plug-in → select the DLL.
3. The plugin registers a radar screen type called **"Weather Radar"**.
   Open a new display window and set its type to "Weather Radar".

## Dot commands

| Command | Effect |
|---|---|
| `.wx` | Toggle overlay on/off |
| `.wx on` | Enable overlay |
| `.wx off` | Disable overlay |
| `.wx opacity 70` | Set opacity 0–100 (default 60) |
| `.wx refresh` | Force-fetch the latest radar frame now |

## How it works

The plugin reads your active **vis points** (the same circles shown by `.showvis`)
via `GetVisibilityCentersNumber()` / `GetVisibilityCenter(i)` / `GetVisibilityDistance(i)`.

For each circle it calculates which Web Mercator tiles at the current zoom level cover
that area, then fetches them from RainViewer's public tile API on a background thread.
Tiles are drawn behind the scope content using GDI+ with configurable alpha.

The radar frame is refreshed every 5 minutes automatically.

## EuroScope SDK method names

The vis point API used is:

```cpp
int    GetPlugIn()->GetVisibilityCentersNumber()
CPosition GetPlugIn()->GetVisibilityCenter(int i)
double GetPlugIn()->GetVisibilityDistance(int i)   // returns nm
```

If your SDK version uses different names, update `CollectVisPoints()` in
`WeatherRadarScreen.cpp` accordingly.
