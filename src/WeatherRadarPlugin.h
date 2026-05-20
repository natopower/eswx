#pragma once
#include "EuroScopePlugIn.h"

class WeatherRadarPlugin : public EuroScopePlugIn::CPlugIn {
public:
    WeatherRadarPlugin();
    ~WeatherRadarPlugin() override = default;

    EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(
        const char* sDisplayName, bool NeedRadarContent,
        bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) override;

    void OnFunctionCall(int FunctionId, const char* sItemString,
                        POINT Pt, RECT Area) override {}

    bool OnCompileCommand(const char* sCommandLine) override;

private:
    class WeatherRadarScreen* m_screen = nullptr;
};
