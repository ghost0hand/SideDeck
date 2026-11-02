#pragma once
#include <algorithm>
#include <cmath>

namespace touchscroll {
// Shared by native touch/pen pointers and streaming clients' mouse drags.
// Coordinates are client pixels; timestamps are monotonic seconds.
struct Gesture {
    bool active=false,moved=false,scrolling=false,eligible=false,stopTap=false;
    float startX=0,startY=0,lastY=0,velocity=0;
    double lastTime=0,lastMotion=0;
    void cancel() {active=moved=scrolling=eligible=stopTap=false;velocity=0;}
    void begin(float x,float y,double time,bool canScroll) {
        bool wasMoving=std::fabs(velocity)>15;
        cancel();active=true;eligible=canScroll;stopTap=canScroll&&wasMoving;
        startX=x;startY=lastY=y;lastTime=lastMotion=time;
    }
    float move(float x,float y,double time,float slop,float maximumSpeed) {
        if(!active)return 0;
        float dx=x-startX,dy=y-startY;
        if(dx * dx + dy * dy > slop * slop)moved=true;
        bool first=!scrolling;
        if(eligible && std::fabs(dy)>slop && std::fabs(dy)>=std::fabs(dx)*0.75f)scrolling=true;
        float delta=scrolling ? (first?startY-y:lastY-y) : 0;
        double elapsed=time-lastTime;
        if(scrolling && std::fabs(delta)>0.01f && elapsed>0) {
            float instantaneous=std::clamp(delta/(float)std::max(0.008,elapsed),-maximumSpeed,maximumSpeed);
            velocity=first?instantaneous:velocity*0.35f+instantaneous*0.65f;
            lastMotion=time;
        }
        lastY=y;lastTime=time;return delta;
    }
    bool end(double time) {
        bool consumed=moved||scrolling||stopTap;
        active=false;
        if(!scrolling || time-lastMotion>0.09)velocity=0;
        return consumed;
    }
    float coast(float seconds) {
        if(active || std::fabs(velocity)<15 || seconds<=0){if(!active)velocity=0;return 0;}
        float decay=std::exp(-8.0f*seconds);
        float distance=velocity*(1-decay)/8.0f;velocity*=decay;return distance;
    }
};
} // namespace touchscroll
