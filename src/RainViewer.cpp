#include "pch.h"
#include "RainViewer.h"
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

static long long ParseFirstIntAfter(const std::string& s, const std::string& key) {
    auto pos = s.find(key);
    if (pos == std::string::npos) return 0;
    pos += key.size();
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == ':')) pos++;
    auto end = s.find_first_of(",} \t\r\n", pos);
    if (end == std::string::npos) end = s.size();
    try { return std::stoll(s.substr(pos, end - pos)); } catch (...) { return 0; }
}

bool RainViewer::FetchLatestFrame() {
    std::string body = HttpGetText(L"api.rainviewer.com", L"/public/weather-maps.json");
    if (body.empty()) return false;

    auto pastStart = body.find("\"past\"");
    if (pastStart == std::string::npos) return false;
    auto pastEnd = body.find(']', pastStart);
    if (pastEnd == std::string::npos) return false;

    long long bestTime = 0;
    std::string bestPath;

    size_t cursor = pastStart;
    while (cursor < pastEnd) {
        auto tpos = body.find("\"time\"", cursor);
        if (tpos == std::string::npos || tpos >= pastEnd) break;
        auto ppos = body.find("\"path\"", tpos);
        if (ppos == std::string::npos || ppos >= pastEnd) break;

        size_t tval = tpos + 7;
        while (tval < body.size() && body[tval] == ' ') tval++;
        auto tvalEnd = body.find_first_of(",} \t\r\n", tval);
        long long tt = 0;
        try { tt = std::stoll(body.substr(tval, tvalEnd - tval)); } catch (...) {}

        size_t pval = ppos + 7;
        while (pval < body.size() && (body[pval] == ' ' || body[pval] == '"')) pval++;
        auto pvalEnd = body.find('"', pval);
        std::string pp = (pvalEnd != std::string::npos) ? body.substr(pval, pvalEnd - pval) : "";

        if (tt > bestTime && !pp.empty()) { bestTime = tt; bestPath = pp; }
        cursor = (pvalEnd != std::string::npos) ? pvalEnd + 1 : pastEnd;
    }

    if (bestPath.empty()) return false;
    m_frame = { bestTime, bestPath };
    return true;
}

std::string RainViewer::TileUrl(int z, int x, int y) const {
    if (!HasFrame()) return {};
    std::ostringstream ss;
    ss << "https://tilecache.rainviewer.com" << m_frame.path
       << "/256/" << z << "/" << x << "/" << y << "/6/0_0.png";
    return ss.str();
}

std::vector<uint8_t> RainViewer::FetchTileBytes(const std::string& url) const {
    const std::string host_str = "tilecache.rainviewer.com";
    const std::string prefix = "https://" + host_str;
    if (url.substr(0, prefix.size()) != prefix) return {};
    std::wstring path(url.begin() + prefix.size(), url.end());
    return HttpGetBytes(L"tilecache.rainviewer.com", path.c_str());
}

static HINTERNET OpenSession() {
    return WinHttpOpen(L"EuroScope-WeatherRadar/1.0",
                       WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
}

std::string RainViewer::HttpGetText(const wchar_t* host, const wchar_t* path) const {
    auto hSess = OpenSession();
    if (!hSess) return {};
    auto hConn = WinHttpConnect(hSess, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return {}; }
    auto hReq = WinHttpOpenRequest(hConn, L"GET", path, NULL,
                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    std::string result;
    if (hReq && WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
             && WinHttpReceiveResponse(hReq, NULL)) {
        DWORD n = 0; char buf[4096];
        while (WinHttpReadData(hReq, buf, sizeof(buf), &n) && n)
            result.append(buf, n);
    }
    if (hReq) WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return result;
}

std::vector<uint8_t> RainViewer::HttpGetBytes(const wchar_t* host, const wchar_t* path) const {
    auto hSess = OpenSession();
    if (!hSess) return {};
    auto hConn = WinHttpConnect(hSess, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return {}; }
    auto hReq = WinHttpOpenRequest(hConn, L"GET", path, NULL,
                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    std::vector<uint8_t> result;
    if (hReq && WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
             && WinHttpReceiveResponse(hReq, NULL)) {
        DWORD n = 0; uint8_t buf[65536];
        while (WinHttpReadData(hReq, buf, sizeof(buf), &n) && n)
            result.insert(result.end(), buf, buf + n);
    }
    if (hReq) WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return result;
}
