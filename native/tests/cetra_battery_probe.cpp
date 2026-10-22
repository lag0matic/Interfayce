#include "cetra_battery.h"
#include <iostream>
int main() {
    auto readings=interfayce::ReadCetraBatteries();
    const char* names[]{"Left", "Right", "Case"};
    for (int i=0;i<3;++i) std::cout << names[i] << ": " << readings[i].percent
        << "% charging=" << readings[i].charging << "\n";
}
