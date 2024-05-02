#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <cstdlib>
#include <ctime>
#include <chrono>

#define GB (1024 * 1024 * 1024)
#define MB (1024 * 1024)

const std::string DATA_FILE = "data.dat"; // File to store data

// Custom key-value store class
class KeyValueStore {
private:
    std::unordered_map<std::string, std::string> data; // In-memory key-value store

public:
    // Constructor
    KeyValueStore() {
        // Load data from file to memory (Optane)
        loadDataFromFile();
    }

    // Destructor
    ~KeyValueStore() {
        // Save data from memory (Optane) to file
        saveDataToFile();
    }

    // Get value for a given key
    std::string getValue(const std::string& key) {
        auto it = data.find(key);
        if (it != data.end()) {
            return it->second;
        }
        return ""; // Return empty string if key not found
    }

    // Set value for a given key
    void setValue(const std::string& key, const std::string& value) {
        data[key] = value;
    }

private:
    // Load data from file to memory (Optane)
    void loadDataFromFile() {
        std::ifstream file(DATA_FILE);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                size_t pos = line.find('=');
                if (pos != std::string::npos) {
                    std::string key = line.substr(0, pos);
                    std::string value = line.substr(pos + 1);
                    data[key] = value;
                }
            }
            file.close();
        }
    }

    // Save data from memory (Optane) to file
    void saveDataToFile() {
        std::ofstream file(DATA_FILE);
        if (file.is_open()) {
            for (const auto& pair : data) {
                file << pair.first << '=' << pair.second << '\n';
            }
            file.close();
        }
    }
};

int main() {
    // Seed for randomization
    std::srand(std::time(nullptr));

    // Create key-value store object
    KeyValueStore kvStore;

    while (1) {
        auto start = std::chrono::high_resolution_clock::now();
        for (long long i = 0; i < 10 * MB; ++i) {
            std::string key = "key" + std::to_string(std::rand() % MB);
            std::string value = "value" + std::to_string(std::rand() % MB);
            kvStore.setValue(key, value);
        }
        for (long long i = 0; i < 10 * MB; ++i) {
            std::string randomKey = "key" + std::to_string(std::rand() % MB);
            kvStore.getValue(randomKey);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = end - start;
        std::cout << "Time taken: " << duration.count() << " seconds" << std::endl;
    }

    return 0;
}
