#pragma once

#include "resonance/Story.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <span>

namespace resonance::chapter_three {
struct WaterBounds {
    double min_x,max_x,min_y,max_y;
    bool contains(Vec3 p) const {
        return p.x>=min_x&&p.x<=max_x&&p.y>=min_y&&p.y<=max_y;
    }
};

struct Crossing {
    int map,actor,entry;
    const wchar_t* landing; // Native landing component name, or null for a passage.
    const char* label;
    Vec3 goal; // Candidate XY inside the live landing or passage.
    WaterBounds water;
    std::span<const Vec3> turns;
};

// The first pool winds around a central rock. The knots are taken from the
// exact build's cooked collision with the raft capsule. Live sweeps verify each leg.
inline constexpr std::array first_pool_turns{
    Vec3{770,10430,0},Vec3{890,9290,0},Vec3{1030,9170,0},Vec3{2170,9170,0},Vec3{2250,9250,0},
    Vec3{2250,10570,0},Vec3{2150,10670,0},Vec3{1930,10650,0},Vec3{1810,10490,0},Vec3{1730,10250,0},
    Vec3{1490,10190,0},Vec3{1350,10350,0},Vec3{1270,11410,0},Vec3{910,11670,0},Vec3{950,11970,0},
    Vec3{1210,12050,0},Vec3{1890,11990,0},Vec3{2030,11750,0}
};
inline constexpr std::array second_pool_turns{
    Vec3{1090,39750,0},Vec3{970,38650,0},Vec3{870,38230,0},Vec3{870,37550,0},
    Vec3{950,37470,0},Vec3{1170,37490,0},Vec3{1230,37550,0},Vec3{1250,37830,0},
    Vec3{1390,37910,0},Vec3{1590,37850,0},Vec3{1610,37790,0},Vec3{1630,37390,0},
    Vec3{1690,37330,0},Vec3{1910,37330,0},Vec3{2010,37430,0},Vec3{1970,38250,0},
    Vec3{1910,38310,0},Vec3{1570,38330,0},Vec3{1470,38430,0},Vec3{1530,38690,0}
};
inline constexpr std::array second_pool_return_turns{
    Vec3{2390,37870,0},Vec3{2450,38650,0},Vec3{2530,38730,0}
};
inline constexpr std::array north_channel_turns{
    Vec3{550,69150,0},Vec3{650,68510,0},Vec3{1070,68430,0},Vec3{1710,68490,0},
    Vec3{1790,68630,0},Vec3{1730,68830,0},Vec3{1070,68990,0},Vec3{1030,69110,0},
    Vec3{1050,69290,0},Vec3{1190,69370,0},Vec3{2250,69330,0},Vec3{2310,69190,0},
    Vec3{2290,68050,0},Vec3{2150,67970,0},Vec3{690,67610,0}
};
inline constexpr std::array shrine_outbound_turns{
    Vec3{950,43650,0},Vec3{1170,43530,0},Vec3{1610,42330,0},Vec3{1710,42310,0}
};
inline constexpr std::array shrine_home_turns{
    Vec3{1630,42310,0},Vec3{1350,42810,0},Vec3{1070,43630,0},Vec3{890,43590,0}
};
inline constexpr std::array shrine_tower_turns{Vec3{-1470,11990,0},Vec3{-1130,11990,0}};
inline constexpr std::array shrine_west_turns{Vec3{-2230,11810,0}};
inline std::optional<Crossing> crossing(StoryCrossing id) {
    constexpr WaterBounds first_cave{-490,2750,8600,12795},second_cave{525,2900,36550,40450},
        third_cave{-415,2510,65800,69950},shrine_entry{340,2130,41520,43850},
        shrine_basin{-2760,-880,11500,12680};
    switch(id) {
    case StoryCrossing::CaveFarBank:
        return Crossing{3080,11,-1,L"GetOffAreaBoxComponent2","Landing on the far bank",
            {2110,11510,0},first_cave,first_pool_turns};
    case StoryCrossing::CaveNextPool:
        return Crossing{3080,6,10002,nullptr,"Passage to the third pool",{2705,38725,0},second_cave,second_pool_turns};
    case StoryCrossing::CaveUpperPassage:
        return Crossing{3080,12,-1,L"CPP_MapGimmick_Raft_GetOff_Box","Landing below the upper passage",
            {630,67550,0},third_cave,north_channel_turns};
    case StoryCrossing::CaveHiddenReturn:
        return Crossing{3080,10,10005,nullptr,"Passage back to the second pool",
            {-318,67645,0},{-430,465,67430,68080},{}};
    case StoryCrossing::ShrinePedestalPassage:
        return Crossing{3110,3,7,nullptr,"Passage to the pedestal room",{1910,42310,0},shrine_entry,shrine_outbound_turns};
    case StoryCrossing::ShrinePedestalLanding:
        return Crossing{3110,7,-1,L"GetOffAreaBoxComponent2","Landing by the large crystal pedestal",
            {108,50450,0},{-620,240,50060,50840},{}};
    case StoryCrossing::ShrineReturnPassage:
        return Crossing{3110,0,10,nullptr,"Passage back to the first boat room",
            {-475,50450,0},{-620,240,50060,50840},{}};
    case StoryCrossing::ShrineSecondChannelReturn:
        return Crossing{3110,12,11,nullptr,"Passage back to the first boat room",
            {-475,51450,0},{-620,240,51060,51770},{}};
    case StoryCrossing::ShrineThirdChannelReturn:
        return Crossing{3110,13,12,nullptr,"Passage back to the first boat room",
            {-475,52100,0},{-620,240,51800,52440},{}};
    case StoryCrossing::ShrineHomeLanding:
        return Crossing{3110,7,-1,L"GetOffAreaBoxComponent","Landing by the central hall passage",
            {750,43545,0},shrine_entry,shrine_home_turns};
    case StoryCrossing::ShrineTowerLanding:
        return Crossing{3110,8,-1,L"CPP_MapGimmick_Raft_GetOff_Box","Landing by the tower ladder",
            {-1050,12100,0},shrine_basin,shrine_tower_turns};
    case StoryCrossing::ShrineWestLanding:
        return Crossing{3110,8,-1,L"GetOffAreaBoxComponent","Landing below the water pedestal",
            {-2365,11748,0},shrine_basin,shrine_west_turns};
    }
    return {};
}

template<class Clear>
std::vector<Vec3> rowing_path(Vec3 start,Vec3 goal,const Crossing& crossing,Clear&& clear) {
    const auto& bounds=crossing.water;
    const auto open=[&](Vec3 a,Vec3 b) {
        return bounds.contains(a)&&bounds.contains(b)&&clear(a,b);
    };
    if(!bounds.contains(start)||!bounds.contains(goal))return {};
    if(open(start,goal))return {start,goal};
    std::vector<Vec3> surveyed;
    auto turns=crossing.turns;
    if(crossing.map==3080&&crossing.actor==6&&start.x>=2300&&start.y<38700)turns=second_pool_return_turns;
    for(auto point:turns)surveyed.push_back({point.x,point.y,start.z});
    surveyed.push_back(goal);
    for(std::size_t join=surveyed.size()-1;join>0;--join) {
        const auto i=join-1;
        if(!open(start,surveyed[i]))continue;
        bool complete=true;
        for(std::size_t j=i+1;j<surveyed.size();++j) {
            if(open(surveyed[j-1],surveyed[j]))continue;
            complete=false;break;
        }
        if(complete) {
            surveyed.erase(surveyed.begin(),surveyed.begin()+i);
            surveyed.insert(surveyed.begin(),start);
            return surveyed;
        }
    }

    // The live route can start away from the surveyed crossing. Search the
    // current pool with the same support and collision checks as the direct leg.
    constexpr double spacing=58.;
    const auto width=static_cast<int>(std::floor((bounds.max_x-bounds.min_x)/spacing))+1;
    const auto height=static_cast<int>(std::floor((bounds.max_y-bounds.min_y)/spacing))+1;
    if(width<=0||height<=0||width*height>5000)return {};
    const auto point=[&](int id) {
        return Vec3{bounds.min_x+(id%width)*spacing,bounds.min_y+(id/width)*spacing,start.z};
    };
    const auto distance=[](Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);};
    struct Node { double distance=std::numeric_limits<double>::infinity();int parent=-1;bool visited{}; };
    std::vector<Node> nodes(width*height);
    using Candidate=std::pair<double,int>;
    std::priority_queue<Candidate,std::vector<Candidate>,std::greater<>> pending;
    const auto add=[&](int id,int parent,double travel) {
        nodes[id].distance=travel;
        nodes[id].parent=parent;
        pending.emplace(travel+distance(point(id),goal),id);
    };
    for(int i=0;i<width*height;++i) {
        const auto p=point(i);
        const auto offset=distance(start,p);
        if(offset<=spacing*2.&&open(start,p))add(i,-1,offset);
    }
    constexpr std::array offsets{std::pair{-1,0},std::pair{1,0},std::pair{0,-1},std::pair{0,1},
        std::pair{-1,-1},std::pair{-1,1},std::pair{1,-1},std::pair{1,1}};
    for(std::size_t budget=2200;budget&&!pending.empty();) {
        const auto index=pending.top().second;pending.pop();
        auto& node=nodes[index];
        if(node.visited)continue;
        node.visited=true;
        const auto from=point(index);
        if(distance(from,goal)<=spacing*3.&&open(from,goal)) {
            std::vector<Vec3> dense{goal};
            for(auto i=index;i!=-1;i=nodes[i].parent)dense.push_back(point(i));
            dense.push_back(start);
            std::reverse(dense.begin(),dense.end());
            std::vector<Vec3> result{start};
            for(std::size_t i=0;i+1<dense.size();) {
                auto next=std::min(i+18,dense.size()-1);
                while(next>i+1&&!open(dense[i],dense[next]))--next;
                result.push_back(dense[next]);
                i=next;
            }
            return result;
        }
        for(auto [dx,dy]:offsets) {
            const auto x=index%width+dx,y=index/width+dy;
            if(x<0||x>=width||y<0||y>=height||!budget)continue;
            const auto next=y*width+x;
            const auto to=point(next);
            const auto length=node.distance+distance(from,to);
            if(nodes[next].visited||length>=nodes[next].distance)continue;
            --budget;
            if(open(from,to))add(next,index,length);
        }
    }
    return {};
}
}
