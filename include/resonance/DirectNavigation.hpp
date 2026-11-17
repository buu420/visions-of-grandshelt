#pragma once
#include "resonance/Core.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace resonance::direct_nav {
// Exact Steam 24250053 ABI; value observations never retain engine objects.
struct Shared {void* object{};void* controller{};};
struct Result {Shared path;int status{};int padding{};};
static_assert(sizeof(Shared)==16&&sizeof(Result)==24);
struct Api {
    using Select=void* (__fastcall*)(void*,const void*,const Vec3*,const Vec3*);
    using Construct=void* (__fastcall*)(void*,const void*,void*,const Vec3*,const Vec3*,Shared*,Shared*,double,bool);
    using Destroy=void (__fastcall*)(void*);
    using Find=Result* (__fastcall*)(void*,Result*,void*,int);
    using ResolveWeak=void* (*)(const void*);
    using OffsetCorners=void (__fastcall*)(void*,double);
    Select select{};Construct construct{};Destroy destroy{};Find find{};ResolveWeak resolve_weak{};
    OffsetCorners offset_corners{};const void* recast_path_vtable{};
};
struct Path {
    std::vector<Vec3> points;
    bool valid{},partial{};
    int status{};
    std::string reason;
    std::optional<std::uint32_t> native_flags;
    std::vector<Vec3> offset_points;
};
struct CornerOffset {double distance_world{},world_to_metres{};};
Api exact_build_api(Api::ResolveWeak);
void* select_mesh(const Api&,void* system,const void* agent_properties,Vec3 start);
Path find_path(const Api&,const void* owner,void* system,void* mesh,Vec3 start,Vec3 end,CornerOffset={});
struct ProjectedPoint {Vec3 position;std::uint64_t polygon{};};
// An engaged result with no polygon means the native query found no surface.
std::optional<ProjectedPoint> project_point(void* mesh,Vec3 point);
// For short, already projected ground segments only. A hit at the start can
// select the polygon on the wrong side of a shared corner. Confirm from the
// other side with the same native mesh/filter and a bounded number of retries.
struct SurfaceRayRetry {double max_length{};std::size_t remaining{};};
// True means a hit before the requested end. On a readable result, hit reports
// the forward native impact position, including when the segment reaches its end.
std::optional<bool> raycast(void* mesh,Vec3 start,Vec3 end,Vec3* hit=nullptr,SurfaceRayRetry* retry=nullptr);
}
