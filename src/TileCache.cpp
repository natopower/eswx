#include "pch.h"
#include "TileCache.h"

TileCache::~TileCache() {
    std::lock_guard<std::mutex> lk(m_mu);
    for (auto& [k, e] : m_tiles) delete e.bmp;
}

Gdiplus::Bitmap* TileCache::Get(const TileCacheKey& key) const {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = m_tiles.find(key);
    return (it != m_tiles.end()) ? it->second.bmp : nullptr;
}

void TileCache::Insert(const TileCacheKey& key, Gdiplus::Bitmap* bmp) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_pending.erase(key);
    auto& e = m_tiles[key];
    delete e.bmp;
    e.bmp    = bmp;
    e.failed = (bmp == nullptr);
}

bool TileCache::IsPending(const TileCacheKey& key) const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_pending.count(key) > 0;
}

void TileCache::MarkPending(const TileCacheKey& key) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_pending.insert(key);
}

bool TileCache::IsFailed(const TileCacheKey& key) const {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = m_tiles.find(key);
    return (it != m_tiles.end()) && it->second.failed;
}

std::vector<std::pair<TileCacheKey, Gdiplus::Bitmap*>> TileCache::GetAllAtTimestamp(long long ts) const {
    std::lock_guard<std::mutex> lk(m_mu);
    std::vector<std::pair<TileCacheKey, Gdiplus::Bitmap*>> out;
    for (auto& [k, e] : m_tiles)
        if (k.timestamp == ts && e.bmp)
            out.push_back({ k, e.bmp });
    return out;
}

void TileCache::EvictOldTimestamps(long long ts) {
    std::lock_guard<std::mutex> lk(m_mu);
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ) {
        if (it->first.timestamp != ts) {
            delete it->second.bmp;
            it = m_tiles.erase(it);
        } else {
            ++it;
        }
    }
}
