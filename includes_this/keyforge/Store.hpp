#pragma once
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>

namespace keyforge {

class Store {
public:
    bool put(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    bool remove(const std::string& key);
    bool update(const std::string& key, const std::string& new_value); // NEW

private:
    std::unordered_map<std::string, std::string> data_;
    std::mutex mtx_;
};

} // namespace keyforge
