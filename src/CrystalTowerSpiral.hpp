#pragma once

#include "resonance/Core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace resonance::chapter_eight {
inline constexpr Vec3 rest_stop_approach{13100.0, 495.0, 9640.0};

namespace detail {
// Controller-measured feet along the Crystal Tower spiral in map 3281, build 24250053.
// The west turns include closer samples from native floor and capsule sweeps.
inline constexpr std::array<Vec3, 123> spiral_centerline{{
    {11580.0, 400.0, 52.2},
    {11608.4, -39.3, 195.6},
    {11666.2, -485.8, 331.0},
    {11857.0, -895.0, 455.7},
    {12131.3, -1252.0, 592.8},
    {12460.9, -1553.4, 736.4},
    {12805.0, -1843.0, 857.6},
    {13197.1, -2080.8, 989.5},
    {13643.3, -2190.0, 1126.6},
    {14100.0, -2170.0, 1260.4},
    {14541.1, -2101.4, 1392.0},
    {14999.5, -2071.4, 1529.1},
    {15405.0, -1860.3, 1659.1},
    {15764.8, -1584.1, 1786.3},
    {16084.1, -1264.8, 1926.4},
    {16343.0, -895.0, 2061.8},
    {16524.4, -482.4, 2190.3},
    {16650.7, -49.7, 2322.2},
    {16620.0, 400.0, 2464.3},
    {16601.4, 841.1, 2589.5},
    {16543.2, 1289.3, 2724.0},
    {16360.3, 1705.0, 2858.3},
    {16076.4, 2058.4, 2994.5},
    {15745.5, 2361.1, 3121.2},
    {15395.0, 2643.0, 3257.6},
    {15016.6, 2918.4, 3395.0},
    {14556.7, 2990.0, 3523.9},
    {14100.0, 2970.0, 3657.6},
    {13680.0, 2898.0, 3782.021},
    {13640.0, 2894.0, 3795.109},
    {13560.0, 2892.0, 3814.848},
    {13520.0, 2895.0, 3824.400},
    {13460.0, 2901.0, 3838.608},
    {13380.0, 2902.0, 3858.109},
    {13310.0, 2902.0, 3875.241},
    {13193.6, 2900.0, 3923.6},
    {12795.0, 2660.3, 4054.5},
    {12422.3, 2399.4, 4191.5},
    {12093.0, 2084.1, 4323.2},
    {11848.3, 1700.0, 4451.4},
    {11694.4, 1275.6, 4587.0},
    {11549.3, 849.7, 4731.2},
    {11570.0, 400.0, 4858.9},
    {11608.4, -39.3, 4995.6},
    {11666.2, -485.8, 5131.0},
    {11857.0, -895.0, 5255.7},
    {12131.3, -1252.0, 5392.8},
    {12460.9, -1553.4, 5536.4},
    {12805.0, -1843.0, 5657.6},
    {13197.1, -2080.8, 5789.5},
    {13643.3, -2190.0, 5926.6},
    {14100.0, -2170.0, 6060.4},
    {14541.1, -2101.4, 6192.0},
    {14999.5, -2071.4, 6329.1},
    {15405.0, -1860.3, 6459.1},
    {15764.8, -1584.1, 6586.3},
    {16084.1, -1264.8, 6726.4},
    {16343.0, -895.0, 6861.8},
    {16524.4, -482.4, 6990.3},
    {16650.7, -49.7, 7122.2},
    {16620.0, 400.0, 7264.3},
    {16601.4, 841.1, 7389.5},
    {16543.2, 1289.3, 7524.0},
    {16360.3, 1705.0, 7658.3},
    {16076.4, 2058.4, 7794.5},
    {15745.5, 2361.1, 7921.2},
    {15395.0, 2643.0, 8057.6},
    {15016.6, 2918.4, 8195.0},
    {14556.7, 2990.0, 8323.9},
    {14100.0, 2970.0, 8457.6},
    {13680.0, 2898.0, 8582.021},
    {13640.0, 2894.0, 8595.109},
    {13560.0, 2892.0, 8614.848},
    {13520.0, 2895.0, 8624.400},
    {13460.0, 2901.0, 8638.608},
    {13380.0, 2902.0, 8658.109},
    {13310.0, 2902.0, 8675.241},
    {13193.6, 2900.0, 8723.6},
    {12795.0, 2660.3, 8854.5},
    {12422.3, 2399.4, 8991.5},
    {12093.0, 2084.1, 9123.2},
    {11848.3, 1700.0, 9251.4},
    {11713.2, 1268.7, 9387.1},
    {11578.9, 844.5, 9529.7},
    {11760.0, 400.0, 9663.0},
    {11608.4, -39.3, 9795.6},
    {11666.2, -485.8, 9931.0},
    {11857.0, -895.0, 10055.7},
    {12131.3, -1252.0, 10192.8},
    {12460.9, -1553.4, 10336.4},
    {12805.0, -1843.0, 10457.6},
    {13197.1, -2080.8, 10589.5},
    {13643.3, -2190.0, 10726.6},
    {14100.0, -2170.0, 10860.4},
    {14541.1, -2101.4, 10992.0},
    {14999.5, -2071.4, 11129.1},
    {15405.0, -1860.3, 11259.1},
    {15764.8, -1584.1, 11386.3},
    {16084.1, -1264.8, 11526.4},
    {16343.0, -895.0, 11661.8},
    {16524.4, -482.4, 11790.3},
    {16650.7, -49.7, 11922.2},
    {16620.0, 400.0, 12064.3},
    {16601.4, 841.1, 12189.5},
    {16543.2, 1289.3, 12324.0},
    {16360.3, 1705.0, 12458.3},
    {16076.4, 2058.4, 12594.5},
    {15745.5, 2361.1, 12721.2},
    {15395.0, 2643.0, 12857.6},
    {15016.6, 2918.4, 12995.0},
    {14556.7, 2990.0, 13123.9},
    {14100.0, 2970.0, 13257.6},
    {13680.0, 2898.0, 13382.021},
    {13640.0, 2894.0, 13395.109},
    {13560.0, 2892.0, 13414.848},
    {13520.0, 2895.0, 13424.400},
    {13460.0, 2901.0, 13438.608},
    {13380.0, 2902.0, 13458.109},
    {13310.0, 2902.0, 13475.241},
    {13193.6, 2900.0, 13523.6},
    {12795.0, 2660.3, 13654.5},
    {12422.3, 2399.4, 13791.5},
    {12165.0, 2145.0, 13894.1},
}};
inline constexpr std::size_t rest_stop_junction = 84;
inline constexpr std::array<Vec3, 3> rest_stop_centerline{{
    spiral_centerline[rest_stop_junction],
    {12600.0, 400.0, 9640.0},
    rest_stop_approach,
}};
}

inline std::optional<std::vector<Vec3>> spiral_route(Vec3 start, bool rest_stop) {
    if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(start.z))
        return std::nullopt;

    using detail::rest_stop_centerline;
    using detail::rest_stop_junction;
    using detail::spiral_centerline;

    constexpr double max_horizontal_distance = 350.0;
    constexpr double max_vertical_distance = 135.0;
    struct Projection {
        Vec3 point;
        double distance_squared;
        std::size_t segment_end;
        bool on_rest_stop_branch;
    };
    std::optional<Projection> closest;
    auto consider = [&](const auto& centerline, bool on_rest_stop_branch) {
        for (std::size_t end = 1; end < centerline.size(); ++end) {
            const Vec3 a = centerline[end - 1];
            const Vec3 b = centerline[end];
            const double dx = b.x - a.x;
            const double dy = b.y - a.y;
            // Steering follows the horizontal lane. Height verifies the tier
            // without pulling the join backward along a sloped segment.
            const double length_squared = dx * dx + dy * dy;
            const double along = std::clamp(
                ((start.x - a.x) * dx + (start.y - a.y) * dy) /
                    length_squared,
                0.0, 1.0);
            const Vec3 point{std::lerp(a.x, b.x, along), std::lerp(a.y, b.y, along), std::lerp(a.z, b.z, along)};
            const double x_distance = start.x - point.x;
            const double y_distance = start.y - point.y;
            const double z_distance = start.z - point.z;
            const double horizontal_squared = x_distance * x_distance + y_distance * y_distance;
            if (!(horizontal_squared <= max_horizontal_distance * max_horizontal_distance &&
                  std::abs(z_distance) <= max_vertical_distance))
                continue;
            const double distance_squared = horizontal_squared + z_distance * z_distance;
            if (!closest || distance_squared < closest->distance_squared)
                closest = Projection{point, distance_squared, end, on_rest_stop_branch};
        }
    };
    consider(spiral_centerline, false);
    consider(rest_stop_centerline, true);
    if (!closest) return std::nullopt;

    // The same railing narrows the westward seam on each tier. Keep the
    // lateral join there even when it is small enough to skip on a wide turn.
    const auto& join = closest->point;
    const bool narrow_seam = !closest->on_rest_stop_branch &&
        join.x >= 13190.0 && join.x <= 13720.0 && join.y >= 2868.0 && join.y <= 2928.0;
    const double close_to_start = narrow_seam ? 8.0 : 35.0;
    std::vector<Vec3> route{start};
    const auto append = [&](Vec3 point, bool endpoint = false) {
        const auto& previous = route.back();
        if (!endpoint && route.size() == 1 &&
            std::hypot(point.x - start.x, point.y - start.y) <= close_to_start)
            return;
        if (previous.x != point.x || previous.y != point.y || previous.z != point.z)
            route.push_back(point);
    };
    append(closest->point);

    auto next_spiral = closest->segment_end;
    if (closest->on_rest_stop_branch) {
        if (rest_stop) {
            for (auto i = closest->segment_end; i < rest_stop_centerline.size(); ++i)
                append(rest_stop_centerline[i], i + 1 == rest_stop_centerline.size());
            return route;
        }
        for (auto i = closest->segment_end; i > 0; --i)
            append(rest_stop_centerline[i - 1]);
        next_spiral = rest_stop_junction + 1;
    }

    const auto destination = rest_stop ? rest_stop_junction : spiral_centerline.size() - 1;
    for (auto i = next_spiral; i <= destination; ++i)
        append(spiral_centerline[i], !rest_stop && i == destination);
    for (auto i = next_spiral; i > destination; --i)
        append(spiral_centerline[i - 1]);
    if (rest_stop) {
        for (std::size_t i = 1; i < rest_stop_centerline.size(); ++i)
            append(rest_stop_centerline[i], i + 1 == rest_stop_centerline.size());
    }
    return route;
}
}
