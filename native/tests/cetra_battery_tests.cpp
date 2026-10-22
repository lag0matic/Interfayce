#include "cetra_battery.h"
#include <stdexcept>
#include <iostream>
void Check(bool ok) { if (!ok) throw std::runtime_error("Cetra regression"); }
int main() {
    using interfayce::DecodeCetraBatteries;
    const std::array<unsigned char,3> battery{93,66,13};
    const std::array<unsigned char,2> charge{2,1};
    auto values=DecodeCetraBatteries(3,battery,charge);
    Check(values[0].percent==93 && !values[0].charging);
    Check(values[1].percent==66 && values[1].charging);
    Check(values[2].percent==13 && values[2].charging);
    Check(DecodeCetraBatteries(0,battery,charge)==interfayce::CetraBatteries{});
    Check(DecodeCetraBatteries(255,battery,charge)==interfayce::CetraBatteries{});
    Check(DecodeCetraBatteries(3,std::span(battery).first(2),charge)==interfayce::CetraBatteries{});
    values=DecodeCetraBatteries(1,battery,charge);
    Check(values[0].percent==93 && values[1].percent==-1 && !values[1].charging);
    const std::array<unsigned char,3> unknown{255,0,100};
    values=DecodeCetraBatteries(3,unknown,charge);
    Check(values[0].percent==-1 && values[1].percent==0 && values[2].percent==100 && !values[2].charging);
    values=DecodeCetraBatteries(3,battery,{});
    Check(values[0].percent==93 && !values[1].charging && !values[2].charging);
    std::cout << "Cetra charging, disconnect, partial and invalid-report checks passed.\n";
}
