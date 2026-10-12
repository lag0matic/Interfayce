#include "drag_acceleration.h"
#include <iostream>
#include <stdexcept>
#include <limits>
void Check(bool ok) { if (!ok) throw std::runtime_error("Drag acceleration regression"); }
float Travel(int hz) {
    interfayce::DragAcceleration curve;
    float distance = 0;
    for (int i = 0; i < hz; ++i) distance += curve.Gain(1.0F/hz, 1.0F/hz, true, 4) / hz;
    return distance;
}
int main() {
    try {
        interfayce::DragAcceleration curve;
        for (int i=0; i<200; ++i) Check(curve.Gain(.001F,.01F,true,4) == 1);
        for (int i=0; i<200; ++i) Check(curve.Gain(.02F,.01F,false,4) == 1);
        for (int i=0; i<200; ++i) curve.Gain(.015F,.01F,true,4);
        Check(curve.Gain(.015F,.01F,true,4) > 3.99F);
        Check(curve.Gain(.001F,.01F,true,4) == 1); // Precision returns immediately.
        Check(curve.Gain(0,.01F,true,4) == 1); // No momentum.
        Check(curve.Gain(1,.01F,true,4) == 0);
        Check(curve.Gain(.01F,.5F,true,4) == 1);
        Check(curve.Gain(.01F,.5F,false,4) == 1);
        Check(curve.Gain(.06F,.001F,false,4) == 1);
        Check(curve.Gain(std::numeric_limits<float>::quiet_NaN(),.01F,true,4) == 0);
        Check(curve.Gain(.01F,0,true,4) == 0);
        // Identical physical motion with a cached pose every other iteration.
        interfayce::DragAcceleration regular, cached;
        float regularTravel = 0, cachedTravel = 0;
        for (int i=0; i<100; ++i) {
            regularTravel += .01F * regular.Gain(.01F,.02F,true,4);
            cached.Gain(0,.019F,true,4);
            cachedTravel += .01F * cached.Gain(.01F,.001F,true,4);
        }
        Check(std::abs(regularTravel - cachedTravel) < .001F);
        curve.Reset();
        Check(curve.Gain(.001F,.01F,true,4) == 1);
        Check(std::abs(Travel(72)-Travel(144)) < .035F);
        Check(Travel(90)>3.5F && Travel(90)<=4);
        std::cout << "Precision, boost, stop, reset, tracking faults and frame-rate checks passed.\n";
    } catch(const std::exception& error) { std::cerr<<error.what(); return 1; }
}
