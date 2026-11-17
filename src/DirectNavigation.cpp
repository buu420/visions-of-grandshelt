#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/DirectNavigation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
namespace resonance::direct_nav {
namespace {
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
template<class T>std::optional<T> read(const void* p,std::size_t offset=0) {
    if(!p)return {};
    T value{};SIZE_T copied{};
    if(!ReadProcessMemory(GetCurrentProcess(),static_cast<const std::byte*>(p)+offset,&value,sizeof value,&copied)||copied!=sizeof value)return {};
    return value;
}
bool executable(const void* p) {
    MEMORY_BASIC_INFORMATION info{};
    if(!p||!VirtualQuery(p,&info,sizeof info)||info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    return (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
}
bool writable_counts(const void* p) {
    MEMORY_BASIC_INFORMATION info{};
    if(!p||!VirtualQuery(static_cast<const std::byte*>(p)+8,&info,sizeof info)||info.State!=MEM_COMMIT||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto end=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
    return reinterpret_cast<std::uintptr_t>(p)+16<=end&&
        (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
}
bool owned_controller(void* p) {
    const auto strong=read<LONG>(p,8),weak=read<LONG>(p,12);
    const auto table=read<void*>(p);
    if(!strong||!*strong||*strong<0||!weak||*weak<=0||!table||!writable_counts(p))return false;
    const auto destroy=read<void*>(*table),erase=read<void*>(*table,8);
    return destroy&&erase&&executable(*destroy)&&executable(*erase);
}
void release(Shared& value) noexcept {
    const auto controller=value.controller;value={};
    if(!controller||!owned_controller(controller))return;
    auto* bytes=static_cast<std::byte*>(controller);
    const auto table=*reinterpret_cast<void***>(controller);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(bytes+8))==0) {
        reinterpret_cast<void (__fastcall*)(void*)>(table[0])(controller);
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(bytes+12))==0)
            reinterpret_cast<void (__fastcall*)(void*,unsigned)>(table[1])(controller,1);
    }
}
struct OwnedResult {
    Result value{};
    ~OwnedResult(){release(value.path);}
};
struct OwnedQuery {
    // Native addressed layout is168 bytes/alignment8; overallocated scratch is
    //176 bytes/alignment16. Never copy a constructed native query.
    alignas(16) std::array<std::byte,176> bytes{};
    Api::Destroy destroy{};bool constructed{};
    ~OwnedQuery(){if(constructed)destroy(bytes.data());}
};
std::optional<Shared> default_filter(void* mesh) {
    const auto filter=read<Shared>(mesh,0x400);
    return filter&&filter->object&&owned_controller(filter->controller)?filter:std::nullopt;
}
struct NativeArray {const std::byte* data;int count,capacity;};
std::optional<NativeArray> path_points(const void* path) {
    const auto points=read<NativeArray>(path,0x28);
    if(!points||!points->data||points->count<2||points->count>4096||points->capacity<points->count)return {};
    return points;
}
bool copy_points(const NativeArray& points,std::vector<Vec3>& out) {
    out.reserve(static_cast<std::size_t>(points.count));
    for(int i=0;i<points.count;++i) {
        const auto point=read<Vec3>(points.data,static_cast<std::size_t>(i)*48);
        if(!point||!finite(*point)){out.clear();return false;}
        out.push_back(*point);
    }
    return true;
}
bool ordinary_points(const NativeArray& points,const std::vector<Vec3>& coords,double scale) {
    for(int i=0;i<points.count;++i) {
        const auto at=static_cast<std::size_t>(i);
        const auto flags=read<std::uint32_t>(points.data,at*48+0x20);
        const auto link=read<std::uint64_t>(points.data,at*48+0x28);
        if(!flags||(*flags&4)||!link||*link)return false; // DT_STRAIGHTPATH_OFFMESH_CONNECTION and FNavLinkId.
        if(at) {
            const auto a=coords[at-1],b=coords[at];
            if(std::hypot(b.x-a.x,b.y-a.y)<scale*.01&&std::abs(b.z-a.z)>scale*.025)return false;
        }
    }
    return true;
}
bool same_coordinates(Vec3 a,Vec3 b) {return a.x==b.x&&a.y==b.y&&a.z==b.z;}
void copy_corner_offset(const Api& api,void* path,void* mesh,const NativeArray& original,CornerOffset offset,Path& result) {
    if(!api.offset_corners||!api.recast_path_vtable||!std::isfinite(offset.distance_world)||offset.distance_world<=0||
       !std::isfinite(offset.world_to_metres)||offset.world_to_metres<=0||offset.distance_world>offset.world_to_metres||
       original.count>100)return;
    const auto table=read<const void*>(path);
    if(!table||*table!=api.recast_path_vtable||!executable(reinterpret_cast<void*>(api.offset_corners))||
       api.resolve_weak(static_cast<std::byte*>(path)+0x9c)!=mesh)return;
    const auto state=read<std::uint32_t>(path,0x178);
    const auto corridor=read<NativeArray>(path,0x128);
    const auto legacy_links=read<int>(path,0x150),links=read<int>(path,0x160);
    if(!state||!(*state&4)||!corridor||!corridor->data||corridor->count<2||corridor->count>4096||
       corridor->capacity<corridor->count||!legacy_links||*legacy_links||!links||*links||
       !ordinary_points(original,result.points,offset.world_to_metres))return;
    // The shared result owns this private FNavMeshPath. Copy both routes while
    // the original is still available and before releasing native ownership.
    api.offset_corners(path,offset.distance_world);
    const auto flags=read<std::uint32_t>(path,0x98);
    const auto adjusted=path_points(path);
    std::vector<Vec3> candidate;
    if(!flags||(*flags&0x37)!=3||!adjusted||!copy_points(*adjusted,candidate)||
       !ordinary_points(*adjusted,candidate,offset.world_to_metres)||
       !same_coordinates(candidate.front(),result.points.front())||
       !same_coordinates(candidate.back(),result.points.back()))return;
    if(!std::equal(candidate.begin(),candidate.end(),result.points.begin(),result.points.end(),same_coordinates))
        result.offset_points=std::move(candidate);
}
}
Api exact_build_api(Api::ResolveWeak resolve) {
    // Admission is the mod's exact executable hash and game-thread/world guards.
    // RVAs are independently retained in direct-nav-abi/DIRECT-NAV-ABI.json.
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    constexpr std::array<unsigned char,16> offset_prologue{
        0xf2,0x0f,0x11,0x4c,0x24,0x10,0x55,0x53,0x41,0x56,0x48,0x8d,0xac,0x24,0xf0,0xfd};
    auto offset=reinterpret_cast<Api::OffsetCorners>(base+0x4a5b630);
    const auto instructions=read<std::array<unsigned char,16>>(reinterpret_cast<const void*>(offset));
    if(!instructions||*instructions!=offset_prologue)offset=nullptr;
    return {reinterpret_cast<Api::Select>(base+0x35bfc40),reinterpret_cast<Api::Construct>(base+0x4a148f0),
        reinterpret_cast<Api::Destroy>(base+0x4a16a20),reinterpret_cast<Api::Find>(base+0x4a24610),resolve,
        offset,reinterpret_cast<const void*>(base+0x84c0280)};
}
void* select_mesh(const Api& api,void* system,const void* props,Vec3 start) {
    if(!system||!props||!finite(start)||!executable(reinterpret_cast<void*>(api.select)))return nullptr;
    const Vec3 extent{}; // Exact native V1 caller; V1 forwards only agent props.
    return api.select(system,props,&start,&extent);
}
Path find_path(const Api& api,const void* owner,void* system,void* mesh,Vec3 start,Vec3 end,CornerOffset offset) {
    Path result;
    const auto fail=[&](const char* reason){result.reason=reason;result.valid=false;return result;};
    if(!owner||!system||!mesh||!finite(start)||!finite(end)||!api.resolve_weak||
       !executable(reinterpret_cast<void*>(api.construct))||!executable(reinterpret_cast<void*>(api.destroy))||
       !executable(reinterpret_cast<void*>(api.find)))return fail("The direct walking-path query is unavailable.");
    const auto filter=default_filter(mesh);
    if(!filter)return fail("The selected walking mesh's query filter is not ready.");
    OwnedQuery query;query.destroy=api.destroy;
    Shared input_filter{},input_path{};
    if(api.construct(query.bytes.data(),owner,mesh,&start,&end,&input_filter,&input_path,
        std::numeric_limits<double>::max(),true)!=query.bytes.data())return fail("The native walking query could not be initialized.");
    query.constructed=true;
    const auto constructed_filter=read<Shared>(query.bytes.data(),0x38);
    if(api.resolve_weak(query.bytes.data()+0x58)!=mesh||!constructed_filter||
       constructed_filter->object!=filter->object||constructed_filter->controller!=filter->controller)
        return fail("The selected walking mesh changed before pathfinding.");
    OwnedResult native;
    query.constructed=false; // FindPathSync owns and destroys this one transfer.
    if(api.find(system,&native.value,query.bytes.data(),0)!=&native.value)return fail("The native walking result could not be read.");
    result.status=native.value.status;
    const bool owned_path=native.value.path.object&&owned_controller(native.value.path.controller);
    // Recast retains projection-error bits even on status Error. Copy only
    // under the result's ownership; no point array is needed for a failed query.
    const auto flags=owned_path?read<std::uint32_t>(native.value.path.object,0x98):std::nullopt;
    result.native_flags=flags;
    if(flags&&(*flags&0x10))return fail("The game could not place the player's position on the current walking surface.");
    if(flags&&(*flags&0x20))return fail("The game could not place this destination on the current walking surface.");
    if(result.status!=3||!owned_path)
        return fail("The game returned no complete walking path to this target.");
    const auto points=path_points(native.value.path.object);
    if(!points||!flags||(*flags&3)!=3)
        return fail("The game has not supplied a ready walking path.");
    result.partial=(*flags&4)!=0;
    if(result.partial)return fail("The game returned only a partial path. A complete walking route is unavailable.");
    if(!copy_points(*points,result.points))return fail("The native walking path points could not be read safely.");
    if(offset.distance_world>0&&read<LONG>(native.value.path.controller,8)==1)
        copy_corner_offset(api,native.value.path.object,mesh,*points,offset,result);
    result.valid=true;return result;
}
std::optional<ProjectedPoint> project_point(void* mesh,Vec3 point) {
    // The exact K2 wrapper calls this navigation-data interface and then
    // copies only FVector. Retain the native polygon ID for this query batch.
    using Project=bool (__fastcall*)(void*,const Vec3*,ProjectedPoint*,const Vec3*,Shared*,const void*);
    static_assert(sizeof(ProjectedPoint)==32);
    if(!finite(point))return {};
    const auto table=read<const void*>(mesh,0x2a8);
    const auto callback=table?read<Project>(*table,0x18):std::nullopt;
    const auto extent=read<Vec3>(mesh,0x300);
    if(!callback||!executable(reinterpret_cast<void*>(*callback))||!extent||!finite(*extent))return {};
    Shared default_filter{};
    ProjectedPoint result{};
    if(!(*callback)(static_cast<std::byte*>(mesh)+0x2a8,&point,&result,&*extent,&default_filter,nullptr))
        return ProjectedPoint{};
    return result.polygon&&finite(result.position)?std::optional{result}:std::nullopt;
}
std::optional<bool> raycast(void* mesh,Vec3 start,Vec3 end,Vec3* impact,SurfaceRayRetry* retry) {
    using Ray=bool (__fastcall*)(void*,const Vec3*,const Vec3*,Vec3*,void*,Shared*,const void*);
    if(!finite(start)||!finite(end))return {};
    struct NativeRay {Vec3 hit;bool blocked;};
    const auto query=[&](Vec3 a,Vec3 b,bool* end_in_corridor=nullptr)->std::optional<NativeRay> {
        const auto callback=read<Ray>(mesh,0x378);
        const auto filter=default_filter(mesh);
        if(!callback||!executable(reinterpret_cast<void*>(*callback))||!filter)return {};
        Shared transferred=*filter;
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<std::byte*>(transferred.controller)+8));
        const auto unwritten=std::numeric_limits<double>::quiet_NaN();
        Vec3 hit{unwritten,unwritten,unwritten};
        // The callback consumes this copy and optionally writes the native
        // FNavigationRaycastAdditionalResults endpoint-in-corridor byte.
        const bool blocked=(*callback)(mesh,&a,&b,&hit,end_in_corridor,&transferred,nullptr);
        if(!finite(hit))return {};
        return NativeRay{hit,blocked};
    };
    const auto forward=query(start,end);
    if(!forward)return {};
    const auto hit=forward->hit;
    if(impact)*impact=hit;
    const double remaining=std::hypot(hit.x-end.x,hit.y-end.y,hit.z-end.z);
    const double magnitude=std::max({1.,std::abs(end.x),std::abs(end.y),std::abs(end.z)});
    if(forward->blocked) {
        // The exact Recast wrapper writes Start + (End - Start) * HitTime.
        // A hit at t=1 reaches this segment's endpoint. The next segment
        // still needs its own proof. Reject hits before it and zero-length rays.
        const double rounding=std::max(.000001,magnitude*8*std::numeric_limits<double>::epsilon());
        const double length=std::hypot(start.x-end.x,start.y-end.y,start.z-end.z);
        if(length<=rounding*2)return true;
        if(remaining<=rounding)return false;
        if(retry&&retry->remaining&&std::isfinite(retry->max_length)&&length<=retry->max_length&&
           std::hypot(hit.x-start.x,hit.y-start.y,hit.z-start.z)<=rounding) {
            --retry->remaining;
            bool end_in_corridor=false;
            const auto reverse=query(end,start,&end_in_corridor);
            // A reverse hit at its end would only reach a boundary, and a
            // clear ray on a different floor would not prove this corridor.
            if(reverse&&!reverse->blocked&&end_in_corridor&&
               std::hypot(reverse->hit.x-start.x,reverse->hit.y-start.y,reverse->hit.z-start.z)<=rounding)return false;
        }
        return true;
    }
    // Reject a mismatched clear result. Allow float query roundoff.
    const double tolerance=std::clamp(magnitude*4*std::numeric_limits<float>::epsilon(),.001,.1);
    if(remaining>tolerance)return {};
    return false;
}
}
