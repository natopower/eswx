#pragma once
#include "EuroScopePlugIn.h"
#include "TileMath.h"
#include "TileCache.h"
#include "RainViewer.h"
#include "LightningFeed.h"
#include <gdiplus.h>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>


class WeatherRadarScreen : public EuroScopePlugIn::CRadarScreen {
public:
    WeatherRadarScreen();
    ~WeatherRadarScreen();

    void OnRefresh(HDC hDC, int Phase) override;
    void OnAsrContentToBeSaved() override;
    void OnAsrContentLoaded(bool Loaded) override;

    void OnAsrContentToBeClosed() override { delete this; }

    void OnMoveScreenObject(int ObjectType, const char* sObjectId,
                            POINT Pt, RECT Area, bool Released) override;
    void OnClickScreenObject(int ObjectType, const char* sObjectId,
                             POINT Pt, RECT Area, int Button) override;
    void OnButtonDownScreenObject(int ObjectType, const char* sObjectId,
                                  POINT Pt, RECT Area, int Button) override;

    void OnOverScreenObject(int, const char*, POINT, RECT) override {}
    void OnDoubleClickScreenObject(int, const char*, POINT, RECT, int) override {}
    void OnButtonUpScreenObject(int, const char*, POINT, RECT, int) override {}
    void OnFunctionCall(int, const char*, POINT, RECT) override {}

    void SetEnabled(bool v)  { m_enabled = v; RefreshMapContent(); RequestRefresh(); }
    void SetOpacity(int pct) { m_opacity = std::max(0, std::min(100, pct)); RefreshMapContent(); RequestRefresh(); }
    void ForceFrameRefresh() { m_forceFrameRefresh = true; RefreshMapContent(); RequestRefresh(); }

    bool IsEnabled()  const { return m_enabled; }
    int  GetOpacity() const { return m_opacity; }

private:
    static Gdiplus::Bitmap* DecodePng(const std::vector<uint8_t>& bytes);
    void FetchWorker();
    void DrawPanel(HDC hDC);
    void DrawLightningStrikes(HDC hDC);

    RainViewer    m_rainViewer;
    TileCache     m_tileCache;
    LightningFeed m_lightningFeed;

    std::atomic<bool> m_enabled{ true };
    std::atomic<int>  m_opacity{ 60 };
    std::atomic<bool> m_forceFrameRefresh{ false };

    static constexpr int WORKER_COUNT = 4;
    std::vector<std::thread> m_workers;
    std::atomic<bool>        m_stopWorker{ false };
    std::queue<TileCacheKey> m_fetchQueue;
    std::mutex               m_fetchMu;
    std::condition_variable  m_fetchCv;

    std::mutex   m_frameMu;
    long long    m_frameTimestamp{ 0 };
    std::string  m_framePath;
    long long    m_lastFrameFetch{ 0 };
    long long    m_lastTileRetry{ 0 };
    static constexpr int FRAME_TTL_SEC  = 300;
    static constexpr int TILE_RETRY_SEC = 60;

    std::atomic<bool> m_lightningEnabled{ true };
    std::string       m_lastLightningStatus;

    ULONG_PTR m_gdipToken{ 0 };

    static constexpr int BTN_OBJECT_TYPE = 900;
    static constexpr int BTN_W_WX  = 30;
    static constexpr int BTN_W_OP  = 38;
    static constexpr int BTN_W_RF  = 24;
    static constexpr int BTN_W_LX  = 22;
    static constexpr int BTN_W     = BTN_W_WX + BTN_W_OP + BTN_W_RF + BTN_W_LX;
    static constexpr int BTN_H     = 16;

    POINT m_btnPos{ -1, -1 };
    POINT m_dragOffset{ 0, 0 };

    static constexpr const char* ASR_ENABLED   = "WxRadar_Enabled";
    static constexpr const char* ASR_OPACITY   = "WxRadar_Opacity";
    static constexpr const char* ASR_BTN_X     = "WxRadar_BtnX";
    static constexpr const char* ASR_BTN_Y     = "WxRadar_BtnY";
    static constexpr const char* ASR_LIGHTNING = "WxRadar_Lightning";
};
