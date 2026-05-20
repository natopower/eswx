#include "pch.h"
#include "WeatherRadarPlugin.h"
#include "WeatherRadarScreen.h"
#include <cstring>
#include <cstdlib>

static WeatherRadarPlugin* g_plugin = nullptr;

void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugin) {
    *ppPlugin = g_plugin = new WeatherRadarPlugin();
}

void __declspec(dllexport) EuroScopePlugInExit() {
    delete g_plugin;
    g_plugin = nullptr;
}

WeatherRadarPlugin::WeatherRadarPlugin()
    : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE,
              "Weather Radar", "1.0.0", "Nate Power", "Free to use")
{
    DisplayUserMessage("Weather Radar", "Init",
        "Weather Radar plugin loaded. Commands: .eswx on/off, .eswx opacity <0-100>, .eswx refresh",
        true, true, false, false, false);
}

EuroScopePlugIn::CRadarScreen* WeatherRadarPlugin::OnRadarScreenCreated(
    const char*, bool, bool GeoReferenced, bool, bool)
{
    if (!GeoReferenced) return nullptr;
    m_screen = new WeatherRadarScreen();
    return m_screen;
}

bool WeatherRadarPlugin::OnCompileCommand(const char* sCommandLine) {
    if (strncmp(sCommandLine, ".eswx", 5) != 0) return false;

    const char* args = sCommandLine + 5;
    while (*args == ' ') args++;

    if (strcmp(args, "on") == 0) {
        if (m_screen) m_screen->SetEnabled(true);
        DisplayUserMessage("Weather Radar", "Cmd", "Radar ON", true, false, false, false, false);
        return true;
    }
    if (strcmp(args, "off") == 0) {
        if (m_screen) m_screen->SetEnabled(false);
        DisplayUserMessage("Weather Radar", "Cmd", "Radar OFF", true, false, false, false, false);
        return true;
    }
    if (strcmp(args, "refresh") == 0) {
        if (m_screen) m_screen->ForceFrameRefresh();
        DisplayUserMessage("Weather Radar", "Cmd", "Refreshing...", true, false, false, false, false);
        return true;
    }
    if (strncmp(args, "opacity", 7) == 0) {
        const char* valStr = args + 7;
        while (*valStr == ' ') valStr++;
        int pct = atoi(valStr);
        if (m_screen) m_screen->SetOpacity(pct);
        char msg[64]; sprintf_s(msg, "Opacity: %d%%", std::max(0, std::min(100, pct)));
        DisplayUserMessage("Weather Radar", "Cmd", msg, true, false, false, false, false);
        return true;
    }
    if (*args == '\0' && m_screen) {
        bool next = !m_screen->IsEnabled();
        m_screen->SetEnabled(next);
        DisplayUserMessage("Weather Radar", "Cmd",
            next ? "Radar ON" : "Radar OFF", true, false, false, false, false);
        return true;
    }

    DisplayUserMessage("Weather Radar", "Help",
        "Usage: .eswx on|off|refresh|opacity <0-100>", true, false, false, false, false);
    return true;
}
