#define NOMINMAX
#include "artwork_cache.h"
#include <Windows.h>
#include <fstream>
#include <utility>

namespace interfayce {
ArtworkCache::ArtworkCache(std::filesystem::path path) : path_(std::move(path)) {}

bool ArtworkCache::Update(std::string_view bytes) {
    if (initialized_ && bytes == bytes_) return false;
    std::error_code error;
    if (bytes.empty()) {
        std::filesystem::remove(path_, error);
        if (error) return false;
    } else {
        auto temporary = path_;
        temporary += L".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            output.close();
            if (!output) {
                std::filesystem::remove(temporary, error);
                return false;
            }
        }
        if (!MoveFileExW(temporary.c_str(), path_.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    bytes_ = bytes;
    initialized_ = true;
    return true;
}
}
