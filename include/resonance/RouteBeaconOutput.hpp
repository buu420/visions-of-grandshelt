#pragma once
#include "resonance/AudioSettings.hpp"
#include "resonance/FieldProximity.hpp"
#include "resonance/Navigation.hpp"
#include <memory>

namespace resonance {
class RouteBeaconOutput {
public:
    RouteBeaconOutput();
    ~RouteBeaconOutput();
    bool ready() const;
    void set_settings(const AudioSettings&);
    void publish(NavigationMode mode,const std::optional<RouteBeaconCue>& cue);
    bool proximity_ready() const;
    void publish_proximity(FieldAudioFrame frame);
    void cancel_proximity();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
