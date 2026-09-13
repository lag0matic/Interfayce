#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
namespace interfayce {
std::optional<std::string> LocalHttpRequest(std::string_view method, std::string_view path,
    std::chrono::milliseconds timeout, std::string_view body = {});
}
