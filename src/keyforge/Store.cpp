#include "keyforge/Store.hpp"

namespace keyforge {

void Store::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mtx_);

    // If key exists, remove old reverse mapping
    auto it = kv_store_.find(key);
    if (it != kv_store_.end()) {
        value_to_keys_[it->second].erase(key);
        if (value_to_keys_[it->second].empty()) {
            value_to_keys_.erase(it->second);
        }
    }

    kv_store_[key] = value;
    value_to_keys_[value].insert(key);
}

std::optional<std::string> Store::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = kv_store_.find(key);
    if (it != kv_store_.end()) return it->second;
    return std::nullopt;
}

bool Store::update(const std::string& key, const std::string& new_value) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = kv_store_.find(key);
    if (it == kv_store_.end()) return false;

    // Update reverse map
    value_to_keys_[it->second].erase(key);
    if (value_to_keys_[it->second].empty()) {
        value_to_keys_.erase(it->second);
    }

    it->second = new_value;
    value_to_keys_[new_value].insert(key);
    return true;
}

bool Store::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = kv_store_.find(key);
    if (it == kv_store_.end()) return false;

    // Remove from reverse map
    value_to_keys_[it->second].erase(key);
    if (value_to_keys_[it->second].empty()) {
        value_to_keys_.erase(it->second);
    }

    kv_store_.erase(it);
    return true;
}

std::optional<std::string> Store::getKeyByValue(const std::string& value) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = value_to_keys_.find(value);
    if (it != value_to_keys_.end() && !it->second.empty()) {
        return *(it->second.begin()); // return one key
    }
    return std::nullopt;
}

} // namespace keyforge
