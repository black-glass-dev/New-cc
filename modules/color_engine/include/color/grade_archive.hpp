#pragma once
#include "color_pipeline_api.hpp"
#include <filesystem>
#include <mutex>
namespace color {
// Original bounded binary look format, not DRX/Resolve interchange. No executable payloads.
std::vector<std::byte> serialize_grade(const GradeProgram&);
GradeProgram deserialize_grade(std::span<const std::byte>);
void save_grade_atomic(const std::filesystem::path&,const GradeProgram&);
GradeProgram load_grade(const std::filesystem::path&);
class LookGallery {
public:
    explicit LookGallery(std::filesystem::path directory);
    void store(std::string_view key,const GradeProgram&);
    GradeProgram recall(std::string_view key) const;
    void erase(std::string_view key);
    std::vector<std::string> list() const;
private: std::filesystem::path directory_;mutable std::mutex mutex_;
};
} // namespace color
