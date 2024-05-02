#include <iostream>
#include <unordered_map>
#include <chrono>
#include <random>

// Stress test function
void stressTestHashMap(int numEntries) {
    // Start timing
    auto start = std::chrono::high_resolution_clock::now();

    // Create a hashmap
    std::unordered_map<int, int> hashmap;

    // Insert random key-value pairs
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(1, numEntries * 10); // Adjust the range according to the test
    for (int i = 0; i < numEntries; ++i) {
        int key = dist(gen);
        int value = dist(gen);
        hashmap[key] = value;
    }

    // Retrieve random values
    for (long long i = 0; i < numEntries; ++i) {
        int key = dist(gen);
        auto it = hashmap.find(key);
        if (it != hashmap.end()) {
            // Value found
            int value = it->second;
        }
    }

    // End timing
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    // Output elapsed time
    std::cout << "Stress test completed with " << numEntries << " entries in " << elapsed.count() << " seconds." << std::endl;
}

int main() {
    // Perform stress tests with different number of entries
    while (1) {
        stressTestHashMap(1024 * 1024 * 8);
    }
    return 0;
}