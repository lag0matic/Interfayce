#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace interfayce {
// Called on the host thread after background retrieval. Unchanged artwork
// never rewrites the file, preserving the renderer's GPU bitmap cache.
class ArtworkCache {
public:
    explicit ArtworkCache(std::filesystem::path path);
    bool Update(std::string_view bytes);
private:
    std::filesystem::path path_;
    std::string bytes_;
    bool initialized_{};
};
}
