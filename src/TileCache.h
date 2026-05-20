#pragma once
#include "TileMath.h"
#include <gdiplus.h>
#include <map>
#include <set>
#include <mutex>
#include <vector>
#include <utility>

struct TileCacheKey {
    TileCoord coord;
    long long timestamp;
    bool operator<(const TileCacheKey& o) const {
        if (timestamp != o.timestamp) return timestamp < o.timestamp;
        return coord < o.coord;
    }
};

class TileCache {
public:
    ~TileCache();

    Gdiplus::Bitmap* Get(const TileCacheKey& key) const;
    void Insert(const TileCacheKey& key, Gdiplus::Bitmap* bmp);

    bool IsPending(const TileCacheKey& key) const;
    void MarkPending(const TileCacheKey& key);

    bool IsFailed(const TileCacheKey& key) const;

    void EvictOldTimestamps(long long ts);
    std::vector<std::pair<TileCacheKey, Gdiplus::Bitmap*>> GetAllAtTimestamp(long long ts) const;

private:
    struct Entry {
        Gdiplus::Bitmap* bmp = nullptr;
        bool failed = false;
    };

    mutable std::mutex m_mu;
    std::map<TileCacheKey, Entry> m_tiles;
    std::set<TileCacheKey> m_pending;
};
