/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */
#define STEROIDSLOG_MIN_LEVEL Info
#include <steroidslog/steroidslog.h>

#include <thread>

int main() {
    STERLOG_INFO("Program start");
    std::thread t([&] {
        for (int i = 0; i < 100; ++i) {
            STERLOG(Warning, "worker iteration {}", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    for (int i = 0; i < 50; ++i) {
        STERLOG_INFO("main loop {}", i);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    t.join();
    STERLOG_ERROR("Shutting down...");
    STERLOG_DEBUG("I will not be logged!");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}
