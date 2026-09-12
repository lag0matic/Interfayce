#include "desktop_click_filter.h"
#include <stdexcept>
#include <iostream>

void Check(POINT point, LONG x, LONG y) {
    if (point.x != x || point.y != y) throw std::runtime_error("Unexpected click position");
}
int main() {
    using namespace std::chrono;
    interfayce::DesktopClickFilter filter;
    const auto start = interfayce::DesktopClickFilter::Clock::now();
    filter.Begin({300, 2100}, start);
    Check(filter.Move({310, 2093}, start + milliseconds(200)), 300, 2100);
    Check(filter.End({310, 2093}), 300, 2100);
    filter.Begin({300, 2100}, start);
    Check(filter.Move({350, 2050}, start + milliseconds(80)), 300, 2100);
    Check(filter.End({350, 2050}), 300, 2100);
    filter.Begin({-1800, 120}, start);
    Check(filter.Move({-1700, 220}, start + milliseconds(180)), -1700, 220);
    Check(filter.Move({-1800, 120}, start + milliseconds(200)), -1800, 120);
    Check(filter.End({-1600, 220}), -1600, 220);
    Check(filter.Move({30, 40}), 30, 40);
    std::cout << "Click jitter, quick squeeze, deliberate drag, and negative coordinates passed.\n";
}
