#include "async_actions.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
void Check(bool value) { if (!value) throw std::runtime_error("Async action regression"); }
int main() {
    std::promise<void> release;
    auto gate = release.get_future().share();
    interfayce::AsyncActions<int> actions;
    const auto ui = std::this_thread::get_id();
    std::vector<int> completed;
    bool onWorker = false;
    actions.Submit([&] { onWorker = std::this_thread::get_id() != ui; gate.wait(); return 1; },
        [&](int result) { Check(std::this_thread::get_id() == ui); completed.push_back(result); });
    actions.Submit([] { return 2; }, [&](int result) {
        completed.push_back(result);
        // A start completion can enqueue a previously requested release.
        actions.Submit([] { return 3; }, [&](int last) { completed.push_back(last); });
    });
    actions.Tick();
    const auto start = std::chrono::steady_clock::now();
    for (int i=0; i<100; ++i) actions.Tick();
    const bool responsive = std::chrono::steady_clock::now()-start < std::chrono::milliseconds(100);
    const bool pending = completed.empty() && actions.Busy();
    release.set_value();
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (actions.Busy() && std::chrono::steady_clock::now()<deadline) {
        actions.Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(responsive && pending && onWorker && !actions.Busy());
    Check(completed == std::vector<int>({1,2,3}));
    std::cout << "Nonblocking, FIFO and UI-thread completion checks passed.\n";
}
