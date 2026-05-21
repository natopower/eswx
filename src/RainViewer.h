#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct RadarFrame {
    long long time = 0;
    std::string path;
};

class RainViewer {
public:
    bool FetchLatestFrame();   // also parses lightning frame from the same response
    std::string TileUrl(int z, int x, int y) const;
    std::string LightningTileUrl(int z, int x, int y) const;
    std::vector<uint8_t> FetchTileBytes(const std::string& url) const;

    bool HasFrame()          const { return !m_frame.path.empty(); }
    bool HasLightningFrame() const { return !m_lightningFrame.path.empty(); }
    const RadarFrame& Frame()          const { return m_frame; }
    const RadarFrame& LightningFrame() const { return m_lightningFrame; }

private:
    RadarFrame m_frame;
    RadarFrame m_lightningFrame;

    std::string HttpGetText(const wchar_t* host, const wchar_t* path) const;
    std::vector<uint8_t> HttpGetBytes(const wchar_t* host, const wchar_t* path) const;
};
