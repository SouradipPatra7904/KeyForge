#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <optional>
#include <atomic>

namespace keyforge {

class Store {
public:
    Store() = default;

    // Add a key-value pair
    void put(const std::string& key, const std::string& value);

    // Get value for a key
    std::optional<std::string> get(const std::string& key);

    // Update existing key
    bool update(const std::string& key, const std::string& new_value);

    // Remove key
    bool remove(const std::string& key);

    // Optional: get a key by value (reverse lookup)
    std::optional<std::string> getKeyByValue(const std::string& value);

    // Methods for persistence
    bool saveToFile(const std::string& filename);
    bool loadFromFile(const std::string& filename);


    // Size of Store :
    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return kv_store_.size();
    }

    // Statistics & metrics variables :
    std::atomic<size_t> get_count{0};
    std::atomic<size_t> put_count{0};
    std::atomic<size_t> update_count{0};
    std::atomic<size_t> delete_count{0};
    std::atomic<size_t> get_miss_count{0};


private:
    std::unordered_map<std::string, std::string> kv_store_;
    std::unordered_map<std::string, std::unordered_set<std::string>> value_to_keys_;
    mutable std::mutex mtx_;
};

} // namespace keyforge
