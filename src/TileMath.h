#pragma once
#include <cmath>
#include <set>
#include <utility>

struct TileCoord {
    int z, x, y;
    bool operator<(const TileCoord& o) const {
        if (z != o.z) return z < o.z;
        if (x != o.x) return x < o.x;
        return y < o.y;
    }
    bool operator==(const TileCoord& o) const { return z == o.z && x == o.x && y == o.y; }
};

namespace TileMath {

constexpr double PI = 3.14159265358979323846;

inline std::pair<int, int> LatLonToTile(double lat, double lon, int z) {
    int n = 1 << z;
    int x = (int)floor((lon + 180.0) / 360.0 * n);
    double lat_rad = lat * PI / 180.0;
    int y = (int)floor((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / PI) / 2.0 * n);
    x = std::max(0, std::min(n - 1, x));
    y = std::max(0, std::min(n - 1, y));
    return { x, y };
}

inline std::pair<double, double> TileNWCorner(int x, int y, int z) {
    int n = 1 << z;
    double lon = (double)x / n * 360.0 - 180.0;
    double lat_rad = atan(sinh(PI * (1.0 - 2.0 * (double)y / n)));
    return { lat_rad * 180.0 / PI, lon };
}

inline double DistanceNm(double lat1, double lon1, double lat2, double lon2) {
    double dlat = (lat2 - lat1) * PI / 180.0;
    double dlon = (lon2 - lon1) * PI / 180.0;
    double a = sin(dlat / 2) * sin(dlat / 2)
             + cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0)
             * sin(dlon / 2) * sin(dlon / 2);
    return 2.0 * atan2(sqrt(a), sqrt(1.0 - a)) * 3440.065;
}

inline std::set<TileCoord> TilesForRect(double latMin, double lonMin, double latMax, double lonMax, int z) {
    // NW corner has higher lat (lower y tile), SE corner has lower lat (higher y tile)
    auto [x0, y0] = LatLonToTile(latMax, lonMin, z);
    auto [x1, y1] = LatLonToTile(latMin, lonMax, z);
    std::set<TileCoord> tiles;
    for (int tx = x0; tx <= x1; tx++)
        for (int ty = y0; ty <= y1; ty++)
            tiles.insert({ z, tx, ty });
    return tiles;
}

inline int RangeToZoom(double range_nm) {
    if (range_nm > 600) return 4;
    if (range_nm > 300) return 5;
    if (range_nm > 150) return 6;
    return 7;
}

}
