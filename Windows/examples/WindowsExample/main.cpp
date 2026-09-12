#include "UnoGPIO.h"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    try {
        unogpio::UnoGPIO gpio("COM3");
        gpio.setOutput(13);
        gpio.setInput(7);

        for (int i = 0; i < 10; ++i) {
            gpio.write(13, (i % 2) == 0);
            std::cout << "pin 7: " << (gpio.read(7) ? "HIGH" : "LOW") << '\n';
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        // This demonstrates traffic pacing only; Windows is not hard real-time.
        const auto period = std::chrono::microseconds(2000);
        auto next = std::chrono::steady_clock::now();
        for (int i = 0; i < 500; ++i) {
            gpio.write(13, (i & 1) != 0);
            next += period;
            std::this_thread::sleep_until(next);
        }
    } catch (const unogpio::Error& error) {
        std::cerr << "UnoGPIO error (" << static_cast<int>(error.code()) << "): "
                  << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
