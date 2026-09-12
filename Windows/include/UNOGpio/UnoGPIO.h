#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace unogpio {

enum class ErrorCode {
    ComPortUnavailable,
    UsbDisconnected,
    Timeout,
    InvalidPacket,
    ChecksumFailure,
    InvalidCommand,
    InvalidGpioPin,
    ArduinoNotResponding,
    IoError,
};

class Error final : public std::runtime_error {
public:
    Error(ErrorCode code, const std::string& message);
    ErrorCode code() const noexcept;

private:
    ErrorCode code_;
};

class UnoGPIO final {
public:
    explicit UnoGPIO(const std::string& comPort, unsigned int baudRate = 115200);
    ~UnoGPIO();

    UnoGPIO(const UnoGPIO&) = delete;
    UnoGPIO& operator=(const UnoGPIO&) = delete;
    UnoGPIO(UnoGPIO&& other) noexcept;
    UnoGPIO& operator=(UnoGPIO&& other) noexcept;

    void setOutput(unsigned int pin);
    void setInput(unsigned int pin);
    void write(unsigned int pin, bool high);
    bool read(unsigned int pin);

    void writeAll(std::uint16_t outputMask, std::uint16_t valueMask);
    std::uint16_t readAll();

    std::uint16_t outputMask() const noexcept;
    std::uint16_t outputValueMask() const noexcept;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace unogpio

