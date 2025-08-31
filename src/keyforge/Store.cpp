#include "keyforge/Store.hpp"
#include <fstream>
#include <sstream>

namespace keyforge {

void Store::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mtx_);

    // Increment PUT counter
    put_count++;

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
    if (it != kv_store_.end()) {
        get_count++;
        return it->second;
    } else {
        get_miss_count++;
        return std::nullopt;
    }
}

bool Store::update(const std::string& key, const std::string& new_value) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = kv_store_.find(key);
    if (it == kv_store_.end()) return false;

    // Increment UPDATE counter
    update_count++;

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

    // Increment DELETE counter
    delete_count++;

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

// Persistence
bool Store::saveToFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::ofstream ofs(filename, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;

    for (const auto& [key, value] : kv_store_) {
        std::string escaped_value = value;
        size_t pos = 0;
        while ((pos = escaped_value.find('\n', pos)) != std::string::npos) {
            escaped_value.replace(pos, 1, "\\n");
            pos += 2;
        }
        pos = 0;
        while ((pos = escaped_value.find('=', pos)) != std::string::npos) {
            escaped_value.replace(pos, 1, "\\=");
            pos += 2;
        }
        ofs << key << "=" << escaped_value << "\n";
    }
    return true;
}

bool Store::loadFromFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::ifstream ifs(filename, std::ios::in);
    if (!ifs.is_open()) return false;

    kv_store_.clear();
    value_to_keys_.clear();

    std::string line;
    while (std::getline(ifs, line)) {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // unescape
        size_t pos = 0;
        while ((pos = value.find("\\n", pos)) != std::string::npos) {
            value.replace(pos, 2, "\n");
            pos += 1;
        }
        pos = 0;
        while ((pos = value.find("\\=", pos)) != std::string::npos) {
            value.replace(pos, 2, "=");
            pos += 1;
        }

        kv_store_[key] = value;
        value_to_keys_[value].insert(key);
    }
    return true;
}

} // namespace keyforge
