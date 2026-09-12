#include "artwork_cache.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

void Check(bool value) { if (!value) throw std::runtime_error("Artwork cache assertion failed"); }
std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int main() {
    const auto path = std::filesystem::temp_directory_path()
        / ("interfayce-art-test-" + std::to_string(GetCurrentProcessId()) + ".jpg");
    try {
        { std::ofstream stale(path); stale << "previous session"; }
        interfayce::ArtworkCache cache(path);
        Check(cache.Update({}));
        Check(!std::filesystem::exists(path));
        Check(cache.Update("old album"));
        const auto timestamp = std::filesystem::last_write_time(path);
        Check(!cache.Update("old album"));
        Check(std::filesystem::last_write_time(path) == timestamp);
        Check(cache.Update("late thumbnail"));
        Check(Read(path) == "late thumbnail");
        Check(cache.Update({}));
        Check(!std::filesystem::exists(path));
        Check(cache.Update("recovered thumbnail"));
        Check(Read(path) == "recovered thumbnail");
        std::filesystem::remove(path);
        std::cout << "Artwork late refresh, unchanged caching, clearing, and recovery passed.\n";
    } catch (...) {
        std::filesystem::remove(path);
        throw;
    }
}
