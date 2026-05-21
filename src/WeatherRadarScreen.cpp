#include "pch.h"
#include "WeatherRadarScreen.h"
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

WeatherRadarScreen::WeatherRadarScreen() {
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::GdiplusStartup(&m_gdipToken, &si, nullptr);
    m_lightningFeed.Start();
    for (int i = 0; i < WORKER_COUNT; ++i)
        m_workers.emplace_back(&WeatherRadarScreen::FetchWorker, this);
}

WeatherRadarScreen::~WeatherRadarScreen() {
    m_stopWorker = true;
    m_fetchCv.notify_all();
    for (auto& w : m_workers)
        if (w.joinable()) w.join();
    m_lightningFeed.Stop();
    Gdiplus::GdiplusShutdown(m_gdipToken);
}

void WeatherRadarScreen::OnAsrContentLoaded(bool) {
    const char* en = GetDataFromAsr(ASR_ENABLED);
    const char* op = GetDataFromAsr(ASR_OPACITY);
    const char* bx = GetDataFromAsr(ASR_BTN_X);
    const char* by = GetDataFromAsr(ASR_BTN_Y);
    if (en) m_enabled  = (strcmp(en, "1") == 0);
    if (op) m_opacity  = std::max(0, std::min(100, atoi(op)));
    if (bx) m_btnPos.x = atoi(bx);
    if (by) m_btnPos.y = atoi(by);
    const char* lx = GetDataFromAsr(ASR_LIGHTNING);
    if (lx) m_lightningEnabled = (strcmp(lx, "1") == 0);
}

void WeatherRadarScreen::OnAsrContentToBeSaved() {
    SaveDataToAsr(ASR_ENABLED,   "Weather Radar Enabled",   m_enabled          ? "1" : "0");
    SaveDataToAsr(ASR_LIGHTNING, "Weather Radar Lightning",  m_lightningEnabled ? "1" : "0");
    char buf[16];
    sprintf_s(buf, "%d", (int)m_opacity); SaveDataToAsr(ASR_OPACITY, "Weather Radar Opacity", buf);
    sprintf_s(buf, "%d", m_btnPos.x);     SaveDataToAsr(ASR_BTN_X,   "Weather Radar Button X", buf);
    sprintf_s(buf, "%d", m_btnPos.y);     SaveDataToAsr(ASR_BTN_Y,   "Weather Radar Button Y", buf);
}

std::vector<VisPoint> WeatherRadarScreen::CollectVisPoints() {
    EuroScopePlugIn::CPosition ld, ru;
    GetDisplayArea(&ld, &ru);
    if ((ld.m_Latitude == 0.0 && ld.m_Longitude == 0.0) ||
        (ru.m_Latitude == 0.0 && ru.m_Longitude == 0.0))
        return {};
    double centerLat = (ld.m_Latitude  + ru.m_Latitude)  / 2.0;
    double centerLon = (ld.m_Longitude + ru.m_Longitude) / 2.0;
    double rangeNm   = TileMath::DistanceNm(ld.m_Latitude, ld.m_Longitude,
                                            ru.m_Latitude, ru.m_Longitude) / 2.0 + 30.0;
    return {{ centerLat, centerLon, rangeNm }};
}


void WeatherRadarScreen::OnRefresh(HDC hDC, int Phase) {
    if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_LISTS) {
        DrawPanel(hDC);
        return;
    }
    if (Phase == EuroScopePlugIn::REFRESH_PHASE_BEFORE_TAGS) {
        if (m_lightningEnabled) DrawLightningStrikes(hDC);
        return;
    }

    if (Phase != EuroScopePlugIn::REFRESH_PHASE_BACK_BITMAP) return;
    if (!m_enabled) return;

    SYSTEMTIME _st; GetSystemTime(&_st);
    long long nowSec = (long long)_st.wHour * 3600 + _st.wMinute * 60 + _st.wSecond;

    if (m_lastFrameFetch == 0 || m_forceFrameRefresh) {
        TileCacheKey sentinel{ {-1, 0, 0}, 0 };
        { std::lock_guard<std::mutex> lk(m_fetchMu); m_fetchQueue.push(sentinel); }
        m_fetchCv.notify_one();
        m_forceFrameRefresh = false;
        m_lastFrameFetch = 1;
        m_lastTileRetry  = nowSec;
    } else {
        if (m_lastFrameFetch > 1 && nowSec - m_lastFrameFetch > FRAME_TTL_SEC) {
            TileCacheKey sentinel{ {-1, 0, 0}, 0 };
            { std::lock_guard<std::mutex> lk(m_fetchMu); m_fetchQueue.push(sentinel); }
            m_fetchCv.notify_one();
            m_lastFrameFetch = nowSec;
        }
        // Retry failed tiles every 60 s so transient network errors self-heal
        if (m_lastTileRetry > 0 && nowSec - m_lastTileRetry > TILE_RETRY_SEC) {
            m_tileCache.ClearFailed();
            m_lastTileRetry = nowSec;
        }
        if (m_lastTileRetry == 0) m_lastTileRetry = nowSec;
    }

    // Log any lightning status change automatically so it's visible without RF
    {
        std::string lxStatus = m_lightningFeed.GetStatus();
        if (lxStatus != m_lastLightningStatus) {
            m_lastLightningStatus = lxStatus;
            char lxMsg[256];
            sprintf_s(lxMsg, "Lightning status: %s", lxStatus.c_str());
            GetPlugIn()->DisplayUserMessage("Weather Radar", "Info",
                lxMsg, true, false, false, false, false);
        }
    }

    long long ts;
    { std::lock_guard<std::mutex> lk(m_frameMu); ts = m_frameTimestamp; }
    if (ts == 0) return;

    std::vector<VisPoint> visPoints = CollectVisPoints();
    if (visPoints.empty()) return;

    EuroScopePlugIn::CPosition dispLD, dispRU;
    GetDisplayArea(&dispLD, &dispRU);
    double screenWidthNm = TileMath::DistanceNm(dispLD.m_Latitude, dispLD.m_Longitude,
                                                 dispLD.m_Latitude, dispRU.m_Longitude);
    int zoom = TileMath::RangeToZoom(screenWidthNm);

    std::set<TileCacheKey> neededKeys;
    for (auto& vp : visPoints) {
        auto tiles = TileMath::TilesForCircle(vp.lat, vp.lon, vp.range_nm, zoom);
        for (auto& tc : tiles)
            neededKeys.insert({ tc, ts });
    }

    {
        std::lock_guard<std::mutex> lk(m_fetchMu);
        for (auto& key : neededKeys) {
            if (!m_tileCache.Get(key) && !m_tileCache.IsPending(key) && !m_tileCache.IsFailed(key)) {
                m_tileCache.MarkPending(key);
                m_fetchQueue.push(key);
            }
        }
    }
    m_fetchCv.notify_all();

    Gdiplus::Graphics g(hDC);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);

    BYTE alpha = (BYTE)(m_opacity * 255 / 100);
    Gdiplus::ColorMatrix cm = {
        1,0,0,0,0,
        0,1,0,0,0,
        0,0,1,0,0,
        0,0,0,(float)alpha/255.0f,0,
        0,0,0,0,1
    };
    Gdiplus::ImageAttributes attrs;
    attrs.SetColorMatrix(&cm, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

    auto renderTile = [&](const TileCacheKey& key, Gdiplus::Bitmap* bmp) {
        auto [nwLat, nwLon] = TileMath::TileNWCorner(key.coord.x,     key.coord.y,     key.coord.z);
        auto [seLat, seLon] = TileMath::TileNWCorner(key.coord.x + 1, key.coord.y + 1, key.coord.z);
        EuroScopePlugIn::CPosition posNW, posSE;
        posNW.m_Latitude = nwLat; posNW.m_Longitude = nwLon;
        posSE.m_Latitude = seLat; posSE.m_Longitude = seLon;
        POINT ptNW = ConvertCoordFromPositionToPixel(posNW);
        POINT ptSE = ConvertCoordFromPositionToPixel(posSE);
        int w = ptSE.x - ptNW.x;
        int h = ptSE.y - ptNW.y;
        if (w <= 0 || h <= 0) return;
        Gdiplus::Rect destRect(ptNW.x, ptNW.y, w, h);
        g.DrawImage(bmp, destRect, 0, 0, bmp->GetWidth(), bmp->GetHeight(),
                    Gdiplus::UnitPixel, &attrs);
    };

    for (auto& key : neededKeys) {
        Gdiplus::Bitmap* bmp = m_tileCache.Get(key);
        if (bmp) renderTile(key, bmp);
    }

    m_tileCache.EvictOldTimestamps(ts);
}

void WeatherRadarScreen::DrawPanel(HDC hDC) {
    if (m_btnPos.x == -1) {
        RECT clip{}; GetClipBox(hDC, &clip);
        m_btnPos.x = clip.right  - BTN_W - 10;
        m_btnPos.y = clip.bottom - BTN_H - 10;
        if (m_btnPos.x < 0) m_btnPos.x = 10;
        if (m_btnPos.y < 0) m_btnPos.y = 10;
    }

    const int x0 = m_btnPos.x;
    const int y0 = m_btnPos.y;

    auto fillSection = [&](int xOff, int w, COLORREF bg) {
        RECT rc{ x0 + xOff, y0, x0 + xOff + w, y0 + BTN_H };
        HBRUSH hBr = CreateSolidBrush(bg);
        FillRect(hDC, &rc, hBr);
        DeleteObject(hBr);
    };

    fillSection(0,                              BTN_W_WX, m_enabled          ? RGB(0, 160, 60)   : RGB(55, 55, 55));
    fillSection(BTN_W_WX,                       BTN_W_OP, RGB(40, 40, 40));
    fillSection(BTN_W_WX + BTN_W_OP,            BTN_W_RF, RGB(30, 80, 150));
    fillSection(BTN_W_WX + BTN_W_OP + BTN_W_RF, BTN_W_LX, m_lightningEnabled ? RGB(160, 140, 0) : RGB(55, 55, 55));

    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
    HPEN hOld = (HPEN)SelectObject(hDC, hPen);
    RECT full{ x0, y0, x0 + BTN_W, y0 + BTN_H };
    MoveToEx(hDC, full.left,      full.top,        nullptr);
    LineTo  (hDC, full.right - 1, full.top);
    LineTo  (hDC, full.right - 1, full.bottom - 1);
    LineTo  (hDC, full.left,      full.bottom - 1);
    LineTo  (hDC, full.left,      full.top);
    MoveToEx(hDC, x0 + BTN_W_WX,                       y0, nullptr); LineTo(hDC, x0 + BTN_W_WX,                       y0 + BTN_H);
    MoveToEx(hDC, x0 + BTN_W_WX + BTN_W_OP,            y0, nullptr); LineTo(hDC, x0 + BTN_W_WX + BTN_W_OP,            y0 + BTN_H);
    MoveToEx(hDC, x0 + BTN_W_WX + BTN_W_OP + BTN_W_RF, y0, nullptr); LineTo(hDC, x0 + BTN_W_WX + BTN_W_OP + BTN_W_RF, y0 + BTN_H);
    SelectObject(hDC, hOld);
    DeleteObject(hPen);

    SetBkMode(hDC, TRANSPARENT);
    SetTextColor(hDC, RGB(255, 255, 255));

    RECT rcWX  { x0,                                          y0, x0 + BTN_W_WX,                              y0 + BTN_H };
    RECT rcOP  { x0 + BTN_W_WX,                               y0, x0 + BTN_W_WX + BTN_W_OP,                  y0 + BTN_H };
    RECT rcRF  { x0 + BTN_W_WX + BTN_W_OP,                    y0, x0 + BTN_W_WX + BTN_W_OP + BTN_W_RF,       y0 + BTN_H };
    RECT rcLX  { x0 + BTN_W_WX + BTN_W_OP + BTN_W_RF,         y0, x0 + BTN_W,                                y0 + BTN_H };

    DrawTextA(hDC, "WX", 2, &rcWX, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    char opBuf[8];
    sprintf_s(opBuf, "%d%%", (int)m_opacity);
    DrawTextA(hDC, opBuf, -1, &rcOP, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DrawTextA(hDC, "RF", 2, &rcRF, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "LX", 2, &rcLX, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    AddScreenObject(BTN_OBJECT_TYPE, "WxPanel", full, true, "");
}

void WeatherRadarScreen::OnButtonDownScreenObject(int ObjectType, const char*,
                                                   POINT Pt, RECT Area, int) {
    if (ObjectType != BTN_OBJECT_TYPE) return;
    m_dragOffset.x = Pt.x - Area.left;
    m_dragOffset.y = Pt.y - Area.top;
}

void WeatherRadarScreen::OnClickScreenObject(int ObjectType, const char*,
                                              POINT Pt, RECT Area, int Button) {
    if (ObjectType != BTN_OBJECT_TYPE) return;

    int relX = Pt.x - Area.left;
    bool backgroundChanged = false;

    if (Button == EuroScopePlugIn::BUTTON_LEFT) {
        if (relX < BTN_W_WX) {
            m_enabled = !m_enabled;
            backgroundChanged = true;
        } else if (relX < BTN_W_WX + BTN_W_OP) {
            int newOp = (int)m_opacity - 10;
            m_opacity = std::max(0, std::min(100, newOp));
            backgroundChanged = true;
        } else if (relX < BTN_W_WX + BTN_W_OP + BTN_W_RF) {
            m_tileCache.ClearFailed();
            m_forceFrameRefresh = true;
            backgroundChanged = true;
            GetPlugIn()->DisplayUserMessage("Weather Radar", "Info",
                "Refresh requested.", true, false, false, false, false);
        } else {
            m_lightningEnabled = !m_lightningEnabled;
        }
    } else if (Button == EuroScopePlugIn::BUTTON_RIGHT) {
        if (relX >= BTN_W_WX && relX < BTN_W_WX + BTN_W_OP) {
            int newOp = (int)m_opacity + 10;
            m_opacity = std::max(0, std::min(100, newOp));
            backgroundChanged = true;
        }
    }

    if (backgroundChanged) RefreshMapContent();
    RequestRefresh();
}

void WeatherRadarScreen::OnMoveScreenObject(int ObjectType, const char*,
                                             POINT Pt, RECT, bool Released) {
    if (ObjectType != BTN_OBJECT_TYPE) return;
    m_btnPos.x = std::max(0L, Pt.x - m_dragOffset.x);
    m_btnPos.y = std::max(0L, Pt.y - m_dragOffset.y);
    (void)Released;
}

static float HueToRadarIntensity(float h) {
    static const struct { float h, i; } pts[] = {
        {   0, 0.80f }, {  30, 0.65f }, {  60, 0.50f },
        { 120, 0.30f }, { 180, 0.05f }, { 240, 0.15f },
        { 300, 1.00f }, { 360, 0.80f }
    };
    for (int k = 0; k < 7; ++k) {
        if (h <= pts[k + 1].h) {
            float t = (h - pts[k].h) / (pts[k + 1].h - pts[k].h);
            return pts[k].i + t * (pts[k + 1].i - pts[k].i);
        }
    }
    return 0.5f;
}

static void RemapToBlue(Gdiplus::Bitmap* bmp) {
    Gdiplus::BitmapData bd{};
    Gdiplus::Rect r(0, 0, (INT)bmp->GetWidth(), (INT)bmp->GetHeight());
    if (bmp->LockBits(&r, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd) != Gdiplus::Ok)
        return;

    BYTE* rows = reinterpret_cast<BYTE*>(bd.Scan0);
    for (UINT y = 0; y < bd.Height; ++y) {
        BYTE* px = rows + y * bd.Stride;
        for (UINT x = 0; x < bd.Width; ++x, px += 4) {
            if (px[3] < 5) continue;

            float rf = px[2] / 255.0f;
            float gf = px[1] / 255.0f;
            float bf = px[0] / 255.0f;

            float maxC = std::max({ rf, gf, bf });
            float minC = std::min({ rf, gf, bf });
            float delta = maxC - minC;

            float intensity = 0.25f;
            if (delta > 0.05f) {
                float h;
                if      (maxC == rf) h = 60.0f * fmodf((gf - bf) / delta, 6.0f);
                else if (maxC == gf) h = 60.0f * ((bf - rf) / delta + 2.0f);
                else                 h = 60.0f * ((rf - gf) / delta + 4.0f);
                if (h < 0.0f) h += 360.0f;
                intensity = HueToRadarIntensity(h);
            }

            float v = 0.90f - intensity * 0.75f;  // brightness: 0.9 (light) → 0.15 (heavy)
            px[2] = (BYTE)(0.20f * v * 255.0f);   // R
            px[1] = (BYTE)(0.60f * v * 255.0f);   // G
            px[0] = (BYTE)(1.00f * v * 255.0f);   // B
        }
    }
    bmp->UnlockBits(&bd);
}

Gdiplus::Bitmap* WeatherRadarScreen::DecodePng(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return nullptr;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!hMem) return nullptr;
    void* p = GlobalLock(hMem);
    if (!p) { GlobalFree(hMem); return nullptr; }
    memcpy(p, bytes.data(), bytes.size());
    GlobalUnlock(hMem);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK) { GlobalFree(hMem); return nullptr; }

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(stream);
    stream->Release();

    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return nullptr; }

    Gdiplus::BitmapData bd{};
    Gdiplus::Rect r(0, 0, (INT)bmp->GetWidth(), (INT)bmp->GetHeight());
    if (bmp->LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
        bool hasData = false;
        const BYTE* rows = reinterpret_cast<const BYTE*>(bd.Scan0);
        for (UINT y = 0; y < bd.Height && !hasData; ++y) {
            const BYTE* px = rows + y * bd.Stride;
            for (UINT x = 0; x < bd.Width && !hasData; ++x, px += 4)
                if (px[3] > 4) hasData = true;
        }
        bmp->UnlockBits(&bd);
        if (!hasData) { delete bmp; return nullptr; }
    }

    RemapToBlue(bmp);
    return bmp;
}

void WeatherRadarScreen::DrawLightningStrikes(HDC hDC) {
    long long nowMs = LightningFeed::UnixTimeMs();
    auto strikes = m_lightningFeed.Snapshot(nowMs);
    if (strikes.empty()) return;

    Gdiplus::Graphics g(hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);

    BYTE alpha = (BYTE)(m_opacity * 255 / 100);
    Gdiplus::Pen pen(Gdiplus::Color(alpha, 255, 255, 255), 1.0f);

    for (auto& s : strikes) {
        long long ageMs = nowMs - s.timeMs;
        if (ageMs < 0) ageMs = 0;
        int bucket = (int)(ageMs * 5 / LightningFeed::STRIKE_TTL_MS);
        if (bucket < 0) bucket = 0;
        if (bucket > 4) bucket = 4;

        EuroScopePlugIn::CPosition pos;
        pos.m_Latitude  = s.lat;
        pos.m_Longitude = s.lon;
        POINT pt = ConvertCoordFromPositionToPixel(pos);

        int sz = 5 - bucket;
        g.DrawLine(&pen, pt.x - sz, pt.y, pt.x + sz + 1, pt.y);
        g.DrawLine(&pen, pt.x, pt.y - sz, pt.x, pt.y + sz + 1);
    }
}

void WeatherRadarScreen::FetchWorker() {
    while (true) {
        TileCacheKey key;
        {
            std::unique_lock<std::mutex> lk(m_fetchMu);
            m_fetchCv.wait(lk, [&] { return m_stopWorker || !m_fetchQueue.empty(); });
            if (m_stopWorker && m_fetchQueue.empty()) break;
            key = m_fetchQueue.front();
            m_fetchQueue.pop();
        }

        if (key.coord.z == -1) {
            if (m_rainViewer.FetchLatestFrame()) {
                {
                    std::lock_guard<std::mutex> lk(m_frameMu);
                    m_frameTimestamp = m_rainViewer.Frame().time;
                    m_framePath      = m_rainViewer.Frame().path;
                }
                m_tileCache.ClearFailed();
                SYSTEMTIME st; GetSystemTime(&st);
                m_lastFrameFetch = (long long)st.wHour * 3600 + st.wMinute * 60 + st.wSecond;
                char msg[192];
                sprintf_s(msg, "Radar frame updated. Lightning: %s.",
                    m_lightningFeed.GetStatus().c_str());
                GetPlugIn()->DisplayUserMessage("Weather Radar", "Info",
                    msg, true, false, false, false, false);
            } else {
                GetPlugIn()->DisplayUserMessage("Weather Radar", "Warn",
                    "Failed to fetch radar frame.", true, false, false, false, false);
            }
            continue;
        }

        std::string url = m_rainViewer.TileUrl(key.coord.z, key.coord.x, key.coord.y);
        if (url.empty()) { m_tileCache.Insert(key, nullptr); continue; }
        auto bytes = m_rainViewer.FetchTileBytes(url);
        m_tileCache.Insert(key, DecodePng(bytes));
    }
}
