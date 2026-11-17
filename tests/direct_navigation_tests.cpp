#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/DirectNavigation.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace resonance;
using namespace resonance::direct_nav;
namespace {
void require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
template<class T,std::size_t N>void put(std::array<std::byte,N>& b,std::size_t at,T value){std::memcpy(b.data()+at,&value,sizeof value);}
template<class T>T get(const void* p,std::size_t at){T t{};std::memcpy(&t,static_cast<const std::byte*>(p)+at,sizeof t);return t;}
struct Controller {void** vtable{};volatile long strong{1},weak{1};int destroyed{},deleted{};};
void __fastcall destroy(void* p){++static_cast<Controller*>(p)->destroyed;}
void __fastcall erase(void* p,unsigned){++static_cast<Controller*>(p)->deleted;}
std::array<void*,2> controller_vtable{reinterpret_cast<void*>(&destroy),reinterpret_cast<void*>(&erase)};
std::array<std::byte,0x420> mesh{};
std::array<std::byte,0x180> native_path{};
std::array<std::byte,144> native_points{};
std::array<std::uint64_t,3> corridor{1,2,3};
Controller filter{},path_control{};
int constructions{},destructions{},finds{},rays{};
int ray_mode{};
int offsets{},offset_mode{};
bool bad_weak{};int response_status{3};unsigned char flags{3};
void* __fastcall select(void* system,const void*,const Vec3*,const Vec3*) {
    require(get<void*>(system,0)==nullptr,"fixture keeps native MainNavData null");return mesh.data();
}
void* __fastcall construct(void* q,const void*,void* selected,const Vec3* start,const Vec3* end,Shared* source,Shared* path,double cost,bool require_end){
    ++constructions;require(!source->object&&!source->controller&&!path->object&&!path->controller,"constructor arguments must be distinct empty owned temporaries");
    require(cost==std::numeric_limits<double>::max()&&require_end,"native constructor flags and DBL_MAX must be retained");
    require(start->x==1&&end->y==4,"query must retain live endpoints");
    std::memcpy(static_cast<std::byte*>(q)+0x58,&selected,8);
    const Shared owned_filter{&filter,&filter};InterlockedIncrement(&filter.strong);
    std::memcpy(static_cast<std::byte*>(q)+0x38,&owned_filter,sizeof owned_filter);return q;
}
void __fastcall destroy_query(void*){++destructions;InterlockedDecrement(&filter.strong);}
Result* __fastcall find(void*,Result* out,void* q,int mode){
    ++finds;require(mode==0,"regular native path mode");destroy_query(q);
    out->status=response_status;out->path={native_path.data(),&path_control};return out;
}
void* weak(const void* p){return bad_weak?nullptr:get<void*>(p,0);}
void __fastcall offset(void* path,double distance) {
    require(path==native_path.data()&&path_control.strong==1&&distance==18.666666,
        "the optional offset must operate on an owned Recast result with the requested distance");
    ++offsets;
    put(native_points,48,Vec3{2,3.25,0});
    if(offset_mode==1)put(native_points,96,Vec3{3,4.1,0});
    if(offset_mode==2)put(native_points,48+0x20,std::uint32_t{4});
}
bool __fastcall ray(void*,const Vec3* start,const Vec3* end,Vec3* hit,void* additional,Shared* filter_copy,const void* querier){
    ++rays;require(!querier&&filter_copy->object,"ray uses selected mesh and its owned filter copy");
    require(filter.strong==2,"ray owns exactly one additional strong reference");
    InterlockedDecrement(&filter.strong);
    if(ray_mode==0)*hit=*end;
    if(ray_mode==2)*hit={end->x+10,end->y,end->z};
    if(ray_mode==3)*hit=*start;
    if(ray_mode==4)*hit=*end;
    if(ray_mode==5)*hit={(start->x+end->x)/2,(start->y+end->y)/2,(start->z+end->z)/2};
    // Castle stair seam: the forward query selects the polygon on the other
    // side of the start corner; the reverse query reaches the corner.
    if(ray_mode>=6) {
        if(end->y<start->y){*hit=*start;return true;}
        if(ray_mode==7)return false; // Native callback did not write a result.
        *hit=*end;
        if(additional&&ray_mode!=9)*static_cast<bool*>(additional)=true;
        return ray_mode==8; // A reverse endpoint hit is still an obstruction.
    }
    return ray_mode>=3;
}
void reset(){
    mesh={};native_path={};native_points={};filter={controller_vtable.data(),1,1,0,0};path_control={controller_vtable.data(),1,1,0,0};
    constructions=destructions=finds=rays=offsets=0;ray_mode=offset_mode=0;bad_weak=false;response_status=3;flags=3;
    put(mesh,0x400,static_cast<void*>(&filter));put(mesh,0x408,static_cast<void*>(&filter));put(mesh,0x378,reinterpret_cast<void*>(&ray));
    put(native_points,0,Vec3{1,2,0});put(native_points,48,Vec3{3,4,0});
    put(native_path,0x28,native_points.data());put(native_path,0x30,2);put(native_path,0x34,2);put(native_path,0x98,flags);
}
void offset_route() {
    put(native_path,0,static_cast<const void*>(controller_vtable.data()));
    put(native_path,0x9c,static_cast<void*>(mesh.data()));
    put(native_path,0x128,corridor.data());put(native_path,0x130,3);put(native_path,0x134,3);
    put(native_path,0x178,std::uint32_t{4});
    put(native_path,0x30,3);put(native_path,0x34,3);
    put(native_points,48,Vec3{2,3,0});put(native_points,96,Vec3{3,4,0});
}
}
int main(){try{
    Api api{select,construct,destroy_query,find,weak};std::array<void*,1> system{nullptr};std::array<std::byte,56> props{};
    reset();require(select_mesh(api,system.data(),props.data(),{1,2,0})==mesh.data(),"registered native agent selection must work with MainNavData null");
    auto path=find_path(api,system.data(),system.data(),mesh.data(),{1,2,0},{3,4,0});
    require(path.valid&&path.points.size()==2&&path.points[1].y==4,"direct native query must return complete finite path without a Main mesh");
    require(constructions==1&&finds==1&&destructions==1,"FindSync consumes the constructed query exactly once");
    require(path_control.strong==0&&path_control.weak==0&&path_control.destroyed==1&&path_control.deleted==1,"result ownership must release after copying points");
    reset();bad_weak=true;path=find_path(api,system.data(),system.data(),mesh.data(),{1,2,0},{3,4,0});
    require(!path.valid&&finds==0&&destructions==1,"revoked mesh weak reference must stop before transfer and clean the owned query");
    reset();response_status=2;path=find_path(api,system.data(),system.data(),mesh.data(),{1,2,0},{3,4,0});
    require(!path.valid&&path_control.destroyed==1&&destructions==1,"failed native results still release query and result exactly once");
    reset();response_status=1;put(native_path,0x98,std::uint32_t{0x10});path=find_path(api,system.data(),system.data(),mesh.data(),{1,2,0},{3,4,0});
    require(!path.valid&&path.reason.find("player's position")!=std::string::npos,"native start projection errors must identify the player's position");
    require(path_control.destroyed==1&&path_control.deleted==1&&destructions==1,"failed start projection still releases all native ownership");
    reset();response_status=1;put(native_path,0x98,std::uint32_t{0x20});path=find_path(api,system.data(),system.data(),mesh.data(),{1,2,0},{3,4,0});
    require(!path.valid&&path.reason.find("destination")!=std::string::npos,"native end projection errors must identify the destination");
    require(path_control.destroyed==1&&path_control.deleted==1&&destructions==1,"failed end projection still releases all native ownership");
    reset();put(native_path,0x98,std::uint32_t{0x23});path=find_path(api,system.data(),system.data(),mesh.data(),{1,2,0},{3,4,0});
    require(!path.valid,"ready flags cannot override a native destination projection error");
    reset();put(native_path,0x98,static_cast<unsigned char>(7));path=find_path(api,system.data(),system.data(),mesh.data(),{1,2,0},{3,4,0});
    require(!path.valid&&path.partial&&path_control.destroyed==1,"partial paths remain unavailable and release result ownership");
    reset();put(native_points,48,Vec3{std::numeric_limits<double>::quiet_NaN(),4,0});path=find_path(api,system.data(),system.data(),mesh.data(),{1,2,0},{3,4,0});
    require(!path.valid&&path_control.destroyed==1,"invalid path coordinates must be rejected before leaving owned storage");
    reset();put(native_path,0x30,4097);put(native_path,0x34,4097);path=find_path(api,system.data(),system.data(),mesh.data(),{1,2,0},{3,4,0});
    require(!path.valid&&path_control.destroyed==1,"unbounded path arrays must fail safely");
    api.offset_corners=offset;api.recast_path_vtable=controller_vtable.data();
    const auto adjusted=[&] {
        return find_path(api,system.data(),system.data(),mesh.data(),{1,2,0},{3,4,0},{18.666666,100});
    };
    reset();offset_route();path=adjusted();
    require(path.valid&&path.points[1].y==3&&path.offset_points.size()==3&&path.offset_points[1].y==3.25&&offsets==1&&
        path_control.destroyed==1,"copy original and alternate geometry before releasing the native path");
    reset();offset_route();put(native_path,0,static_cast<const void*>(nullptr));path=adjusted();
    require(path.valid&&path.offset_points.empty()&&offsets==0,"a foreign path type cannot call the offset method");
    reset();offset_route();put(native_path,0x9c,static_cast<void*>(nullptr));path=adjusted();
    require(path.valid&&path.offset_points.empty()&&offsets==0,"a changed mesh leaves the original route available");
    reset();offset_route();put(native_points,48+0x20,std::uint32_t{4});path=adjusted();
    require(path.valid&&path.offset_points.empty()&&offsets==0,"an original off-mesh link keeps its native route");
    reset();offset_route();put(native_points,48,Vec3{1,2,3});path=adjusted();
    require(path.valid&&path.offset_points.empty()&&offsets==0,"an unflagged native vertical link also keeps its route");
    for(const auto mode:{1,2}) {
        reset();offset_route();offset_mode=mode;path=adjusted();
        require(path.valid&&path.points.back().y==4&&path.offset_points.empty()&&offsets==1,
            "a changed endpoint or a new native link invalidates only the optional candidate");
    }
    reset();const auto clear=raycast(mesh.data(),{1,2,0},{3,4,0});
    require(clear&&!*clear&&rays==1&&filter.strong==1&&!filter.destroyed,"direct mesh ray must consume only its retained filter copy");
    ray_mode=1;require(!raycast(mesh.data(),{1,2,0},{3,4,0})&&filter.strong==1,"an unwritten clear-ray hit must remain unavailable");
    ray_mode=2;require(!raycast(mesh.data(),{1,2,0},{3,4,0})&&filter.strong==1,"a clear-ray hit that misses the endpoint must remain unavailable");
    ray_mode=1;require(!raycast(mesh.data(),{1000000000.,2,0},{1000000004.,4,0})&&filter.strong==1,
        "large world coordinates must not turn an unwritten ray into corridor clearance");
    ray_mode=3;const auto blocked=raycast(mesh.data(),{1,2,0},{3,4,0});require(blocked&&*blocked&&filter.strong==1,"native obstruction remains blocked with balanced filter ownership");
    ray_mode=4;const auto reached_edge=raycast(mesh.data(),{1,2,0},{3,4,0});
    require(reached_edge&&!*reached_edge,"a native hit at the requested corner still reached the whole ray segment");
    ray_mode=5;const auto hit_before_end=raycast(mesh.data(),{1,2,0},{3,4,0});
    require(hit_before_end&&*hit_before_end,"an obstruction before the endpoint is still blocked");
    ray_mode=3;const auto no_progress=raycast(mesh.data(),{1,2,0},{1.0000001,2,0});
    require(no_progress&&*no_progress,"a hit at the start of a tiny ray cannot prove progress");
    const Vec3 corner{-13140,390,800},surface{-13140.948820,388.671653,800};
    auto before=rays;ray_mode=6;const auto forward_only=raycast(mesh.data(),corner,surface);
    require(forward_only&&*forward_only&&rays==before+1,"non-surface rays must not reverse a start hit");
    before=rays;SurfaceRayRetry retry{2.5,1};
    const auto seam=raycast(mesh.data(),corner,surface,nullptr,&retry);
    require(seam&&!*seam&&rays==before+2&&!retry.remaining&&filter.strong==1,
        "the exact short corner segment needs native proof from the opposite side");
    before=rays;const auto exhausted=raycast(mesh.data(),corner,surface,nullptr,&retry);
    require(exhausted&&*exhausted&&rays==before+1,"an exhausted retry budget must leave the forward obstruction intact");
    retry.remaining=1;before=rays;ray_mode=7;Vec3 hit;
    const auto unreadable=raycast(mesh.data(),corner,surface,&hit,&retry);
    require(unreadable&&*unreadable&&rays==before+2&&hit.x==corner.x&&hit.y==corner.y,
        "an unreadable reverse ray must leave the forward obstruction and its location intact");
    retry.remaining=1;before=rays;ray_mode=8;const auto reverse_hit=raycast(mesh.data(),corner,surface,nullptr,&retry);
    require(reverse_hit&&*reverse_hit&&rays==before+2,"touching the source boundary in reverse does not prove a crossing");
    retry.remaining=1;before=rays;ray_mode=9;const auto wrong_corridor=raycast(mesh.data(),corner,surface,nullptr,&retry);
    require(wrong_corridor&&*wrong_corridor&&rays==before+2,"a clear reverse ray without endpoint corridor continuity is insufficient");
    retry.remaining=1;before=rays;ray_mode=6;
    const auto long_ray=raycast(mesh.data(),corner,{surface.x,380,surface.z},nullptr,&retry);
    require(long_ray&&*long_ray&&rays==before+1&&retry.remaining==1,"a longer ray is outside the surface retry");
    ray_mode=5;before=rays;const auto mid_ray=raycast(mesh.data(),corner,surface,nullptr,&retry);
    require(mid_ray&&*mid_ray&&rays==before+1&&retry.remaining==1,"a hit between endpoints must remain blocked");
    const auto prior_rays=rays;
    put(mesh,0x400,static_cast<void*>(nullptr));require(!raycast(mesh.data(),{1,2,0},{3,4,0})&&rays==prior_rays,"unready filter must not call a native ray");
    std::cout<<"PASS direct navigation: Main-null query, selection, ownership, weak revocation, partial/invalid results and ray filter lifecycle\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
