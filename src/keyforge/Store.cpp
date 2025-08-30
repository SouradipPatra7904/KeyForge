#include "keyforge/Store.hpp"

namespace keyforge {

bool Store::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mtx_);
    data_[key] = value;
    return true;
}

std::optional<std::string> Store::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (auto it = data_.find(key); it != data_.end())
        return it->second;
    return std::nullopt;
}

bool Store::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx_);
    return data_.erase(key) > 0;
}

bool Store::update(const std::string& key, const std::string& new_value) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    it->second = new_value;
    return true;
}

} // namespace keyforge
