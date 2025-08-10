/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#include "steroidslog/steroidslog.h"

#include <thread>

int main(int /*argc*/, char** /*argv*/) {
    STERLOG_INFO("Program start");
    std::thread t([&] {
        for (int i = 0; i < 100; ++i) {
            STERLOG_DEBUG("worker iteration {}", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    for (int i = 0; i < 50; ++i) {
        STERLOG_INFO("main loop {}", i);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    t.join();
    STERLOG_WARN("Shutting down...");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}
