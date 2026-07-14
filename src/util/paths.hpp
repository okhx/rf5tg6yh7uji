#pragma once

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif
#include <Geode/Geode.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace silicate::paths {

inline const std::filesystem::path& gameRoot() {
    static const std::filesystem::path root = [] {
#ifdef GEODE_IS_WINDOWS
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0 || length == buffer.size()) {
            throw std::runtime_error("GetModuleFileNameW failed");
        }
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path();
#else
        return geode::Mod::get()->getConfigDir().parent_path().parent_path();
#endif
    }();
    return root;
}

inline const std::filesystem::path& dataRoot() {
    static const std::filesystem::path root = [] {
        auto path = gameRoot() / "Grape";
        auto legacy = gameRoot() / "Silicate";
        if (!std::filesystem::exists(path) && std::filesystem::exists(legacy)) {
            std::error_code error;
            std::filesystem::rename(legacy, path, error);
        }
        std::filesystem::create_directories(path);
        return path;
    }();
    return root;
}

inline std::filesystem::path directory(std::string_view name) {
    auto path = dataRoot() / name;
    std::filesystem::create_directories(path);
    return path;
}

inline std::filesystem::path file(std::string_view name) {
    return dataRoot() / name;
}

}  // namespace silicate::paths
