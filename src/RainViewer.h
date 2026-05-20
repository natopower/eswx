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
    bool FetchLatestFrame();
    std::string TileUrl(int z, int x, int y) const;
    std::vector<uint8_t> FetchTileBytes(const std::string& url) const;

    bool HasFrame() const { return !m_frame.path.empty(); }
    const RadarFrame& Frame() const { return m_frame; }

private:
    RadarFrame m_frame;

    std::string HttpGetText(const wchar_t* host, const wchar_t* path) const;
    std::vector<uint8_t> HttpGetBytes(const wchar_t* host, const wchar_t* path) const;
};
