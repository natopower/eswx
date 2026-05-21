#pragma once
#include <winhttp.h>
#include <string>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cctype>

struct LightningStrike {
    double    lat, lon;
    long long timeMs; // Unix epoch milliseconds
};

// Connects to wss://ws1-ws8.blitzortung.org:443/ (TLS WebSocket via WinHTTP).
// Server streams JSON strike objects; we filter to North America.
class LightningFeed {
public:
    static constexpr long long STRIKE_TTL_MS = 10LL * 60 * 1000; // 10-minute window
    static constexpr int       MAX_STRIKES   = 3000;

    LightningFeed() = default;
    ~LightningFeed() { Stop(); }
    LightningFeed(const LightningFeed&)            = delete;
    LightningFeed& operator=(const LightningFeed&) = delete;

    void Start() {
        if (m_running.exchange(true)) return;
        m_thread = std::thread(&LightningFeed::RunLoop, this);
    }

    void Stop() {
        m_running = false;
        // Close WebSocket handle so any blocking Receive call returns immediately
        HINTERNET ws = m_hWs.exchange(NULL);
        if (ws) { WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0); WinHttpCloseHandle(ws); }
        if (m_thread.joinable()) m_thread.join();
    }

    bool IsConnected() const { return m_connected.load(); }

    std::string GetStatus() const {
        std::lock_guard<std::mutex> lk(m_statusMu);
        return m_status;
    }

    std::vector<LightningStrike> Snapshot(long long nowMs) {
        std::lock_guard<std::mutex> lk(m_mu);
        while (!m_strikes.empty() && nowMs - m_strikes.front().timeMs > STRIKE_TTL_MS)
            m_strikes.pop_front();
        return { m_strikes.begin(), m_strikes.end() };
    }

    static long long UnixTimeMs() {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULONGLONG ull = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        return (long long)(ull / 10000ULL) - 11644473600000LL;
    }

private:
    void SetStatus(std::string s) {
        std::lock_guard<std::mutex> lk(m_statusMu);
        m_status = std::move(s);
    }

    // -------------------------------------------------------------------------
    void RunLoop() {
        int server = 1;
        while (m_running) {
            wchar_t host[64];
            swprintf_s(host, L"ws%d.blitzortung.org", server);
            char hostA[64];
            sprintf_s(hostA, "ws%d.blitzortung.org", server);
            SetStatus(std::string("connecting to wss://") + hostA);

            HINTERNET hSess = NULL, hConn = NULL, hReq = NULL;
            HINTERNET hWs  = Connect(host, hSess, hConn, hReq);

            if (hWs) {
                m_hWs = hWs;
                m_connected = true;
                SetStatus(std::string("connected: ") + hostA);
                ReceiveLoop(hWs);
                m_connected = false;

                // Drain the atomic and close
                hWs = m_hWs.exchange(NULL);
                if (hWs) WinHttpCloseHandle(hWs);
            }

            if (hReq)  WinHttpCloseHandle(hReq);
            if (hConn) WinHttpCloseHandle(hConn);
            if (hSess) WinHttpCloseHandle(hSess);

            if (!m_running) break;
            server = (server % 8) + 1;
            for (int i = 0; i < 5 && m_running; ++i) Sleep(1000);
        }
    }

    // -------------------------------------------------------------------------
    // Returns the WebSocket handle on success, NULL on failure.
    // hSess/hConn/hReq are left open (caller must close them after done with ws).
    HINTERNET Connect(const wchar_t* host,
                      HINTERNET& hSess, HINTERNET& hConn, HINTERNET& hReq) {
        hSess = WinHttpOpen(L"EuroScope-Lightning/2.0",
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSess) return NULL;

        DWORD to = 20000;
        WinHttpSetOption(hSess, WINHTTP_OPTION_CONNECT_TIMEOUT, &to, sizeof(to));
        WinHttpSetOption(hSess, WINHTTP_OPTION_RECEIVE_TIMEOUT,  &to, sizeof(to));
        WinHttpSetOption(hSess, WINHTTP_OPTION_SEND_TIMEOUT,     &to, sizeof(to));

        hConn = WinHttpConnect(hSess, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConn) return NULL;

        hReq = WinHttpOpenRequest(hConn, L"GET", L"/",
                                  NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  WINHTTP_FLAG_SECURE);
        if (!hReq) return NULL;

        // Mark request as a WebSocket upgrade
        WinHttpSetOption(hReq, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);

        WinHttpAddRequestHeaders(hReq,
            L"Origin: http://www.lightningmaps.org\r\n",
            (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

        if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            return NULL;

        if (!WinHttpReceiveResponse(hReq, NULL))
            return NULL;

        HINTERNET hWs = WinHttpWebSocketCompleteUpgrade(hReq, 0);
        return hWs; // NULL if server didn't return 101
    }

    // -------------------------------------------------------------------------
    void ReceiveLoop(HINTERNET hWs) {
        // Blitzortung expects this small subscription message before streaming.
        const char* sub = "{\"a\":111}";
        DWORD sendErr = WinHttpWebSocketSend(hWs,
            WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
            (PVOID)sub, (DWORD)strlen(sub));
        if (sendErr != ERROR_SUCCESS) {
            char st[64]; sprintf_s(st, "send failed: %lu", sendErr);
            SetStatus(st); return;
        }
        SetStatus("sub sent, waiting for data...");

        char buf[131072];
        std::string accum;
        m_msgCount    = 0;
        m_strikeCount = 0;
        bool loggedFirst = false;
        bool loggedFirstDecoded = false;

        while (m_running) {
            DWORD bytesRead = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE bufType;
            DWORD err = WinHttpWebSocketReceive(hWs, buf, sizeof(buf),
                                                &bytesRead, &bufType);
            if (err != ERROR_SUCCESS) break;
            if (bytesRead == 0) continue;

            accum.append(buf, bytesRead);

            // Log first frame of ANY type so we know what the server sends
            if (!loggedFirst) {
                loggedFirst = true;
                char st[160];
                std::string preview = accum.substr(0, 80);
                sprintf_s(st, "type=%d bytes=%u [%s]",
                    (int)bufType, (unsigned)accum.size(), preview.c_str());
                SetStatus(st);
            }

            if (bufType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                bufType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                std::string decoded = DecodeMessage(accum);
                if (!decoded.empty()) {
                    ParseStrike(decoded);
                    if (!loggedFirstDecoded) {
                        loggedFirstDecoded = true;
                        char st[160];
                        std::string preview = decoded.substr(0, 80);
                        sprintf_s(st, "decoded [%s]", preview.c_str());
                        SetStatus(st);
                    }
                }
                accum.clear();

                int msg = ++m_msgCount;
                if (msg % 50 == 0) {
                    char st[128];
                    sprintf_s(st, "msg:%d strikes:%d", msg, (int)m_strikeCount);
                    SetStatus(st);
                }
            } else if (bufType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                break;
            }
            // fragments: keep accumulating until MESSAGE type arrives
        }
    }

    // -------------------------------------------------------------------------
    static std::vector<int> Utf8CodePoints(const std::string& s) {
        std::vector<int> out;
        for (size_t i = 0; i < s.size(); ) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                out.push_back(c);
                ++i;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
                out.push_back(((c & 0x1F) << 6) |
                              (static_cast<unsigned char>(s[i + 1]) & 0x3F));
                i += 2;
            } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
                out.push_back(((c & 0x0F) << 12) |
                              ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                              (static_cast<unsigned char>(s[i + 2]) & 0x3F));
                i += 3;
            } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
                out.push_back(((c & 0x07) << 18) |
                              ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                              ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
                              (static_cast<unsigned char>(s[i + 3]) & 0x3F));
                i += 4;
            } else {
                out.push_back(c);
                ++i;
            }
        }
        return out;
    }

    static std::string DecodeMessage(const std::string& msg) {
        if (msg.empty()) return {};

        // Some clients report Blitzortung messages as UTF-8 text whose code
        // points are LZW codes. Keep a raw-byte fallback for non-text frames.
        std::vector<int> codes = Utf8CodePoints(msg);
        std::string decoded = DecodeLzw(codes);
        if (!decoded.empty() && decoded.find("\"time\"") != std::string::npos)
            return decoded;

        codes.clear();
        codes.reserve(msg.size());
        for (unsigned char c : msg) codes.push_back((int)c);
        decoded = DecodeLzw(codes);
        if (!decoded.empty() && decoded.find("\"time\"") != std::string::npos)
            return decoded;

        return {};
    }

    static std::string DecodeLzw(const std::vector<int>& codes) {
        if (codes.empty()) return {};

        std::unordered_map<int, std::string> dict;
        int nextCode = 256;

        int first = codes[0];
        if (first < 0 || first > 255) return {};
        std::string prev(1, static_cast<char>(first));
        std::string result = prev;

        for (size_t i = 1; i < codes.size(); ++i) {
            int code = codes[i];
            std::string entry;

            if (code >= 0 && code < 256) {
                entry.assign(1, static_cast<char>(code));
            } else {
                auto it = dict.find(code);
                if (it != dict.end()) {
                    entry = it->second;
                } else if (code == nextCode) {
                    entry = prev + prev[0];
                } else {
                    return {};
                }
            }

            result += entry;
            dict[nextCode++] = prev + entry[0];
            prev = entry;
        }

        return result;
    }

    // -------------------------------------------------------------------------
    void ParseStrike(const std::string& msg) {
        if (msg.empty()) return;

        double    lat     = JsonDouble(msg, "\"lat\"");
        double    lon     = JsonDouble(msg, "\"lon\"");
        long long timeRaw = JsonInt64(msg,  "\"time\"");
        if (timeRaw == 0) return;

        // Blitzortung time field is nanoseconds since Unix epoch
        long long tMs = timeRaw / 1000000LL;

        long long nowMs = UnixTimeMs();
        // Accept strikes up to TTL old, or up to 60 s in the future (clock skew)
        if (nowMs - tMs > STRIKE_TTL_MS || tMs > nowMs + 60000) return;

        // North America + Caribbean — widened to catch edge cases
        if (lat < 5.0 || lat > 90.0 || lon < -170.0 || lon > -40.0) return;

        ++m_strikeCount;
        std::lock_guard<std::mutex> lk(m_mu);
        m_strikes.push_back({ lat, lon, tMs });
        while ((int)m_strikes.size() > MAX_STRIKES) m_strikes.pop_front();
    }

    // -------------------------------------------------------------------------
    static double JsonDouble(const std::string& s, const char* key) {
        size_t kp = s.find(key);
        if (kp == std::string::npos) return 0.0;
        size_t colon = s.find(':', kp + strlen(key));
        if (colon == std::string::npos) return 0.0;
        size_t vp = colon + 1;
        while (vp < s.size() && s[vp] == ' ') ++vp;
        try { return std::stod(s.substr(vp)); } catch (...) { return 0.0; }
    }

    static long long JsonInt64(const std::string& s, const char* key) {
        size_t kp = s.find(key);
        if (kp == std::string::npos) return 0;
        size_t colon = s.find(':', kp + strlen(key));
        if (colon == std::string::npos) return 0;
        size_t vp = colon + 1;
        while (vp < s.size() && s[vp] == ' ') ++vp;
        try { return std::stoll(s.substr(vp)); } catch (...) { return 0; }
    }

    // -------------------------------------------------------------------------
    std::atomic<bool>      m_running{ false };
    std::atomic<bool>      m_connected{ false };
    std::thread            m_thread;
    std::atomic<HINTERNET> m_hWs{ NULL };
    std::atomic<int>       m_msgCount{ 0 };   // raw WebSocket messages received
    std::atomic<int>       m_strikeCount{ 0 }; // strikes accepted after all filters

    mutable std::mutex          m_mu;
    std::deque<LightningStrike> m_strikes;

    mutable std::mutex m_statusMu;
    std::string        m_status{ "not started" };
};
