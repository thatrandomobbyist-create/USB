#include "UnoGPIO.h"

#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstring>
#include <utility>

namespace unogpio {
namespace {

constexpr std::uint8_t kStart = 0xA5;
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kWriteAll = 0x01;
constexpr std::uint8_t kReadAll = 0x02;
constexpr std::uint8_t kAck = 0x80;
constexpr std::uint8_t kReadResponse = 0x81;
constexpr std::uint8_t kErrorResponse = 0xE0;
constexpr std::size_t kMaxPayload = 8;

std::string win32Message(DWORD error) {
    LPSTR buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string result = size != 0 ? std::string(buffer, size) : "Windows error " + std::to_string(error);
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
        result.pop_back();
    }
    return result;
}

std::uint16_t crc16(const std::uint8_t* data, std::size_t length) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) != 0 ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                                      : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

void validatePin(unsigned int pin) {
    if (pin < 2 || pin > 13) {
        throw Error(ErrorCode::InvalidGpioPin, "GPIO pin must be between 2 and 13");
    }
}

std::uint16_t pinBit(unsigned int pin) {
    validatePin(pin);
    return static_cast<std::uint16_t>(1u << (pin - 2));
}

} // namespace

Error::Error(ErrorCode code, const std::string& message) : std::runtime_error(message), code_(code) {}
ErrorCode Error::code() const noexcept { return code_; }

struct UnoGPIO::Impl {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::uint8_t sequence = 0;
    std::uint16_t outputMask = 0;
    std::uint16_t outputValues = 0;

    [[noreturn]] void fail(ErrorCode code, const std::string& message) const {
        throw Error(code, message);
    }

    void close() noexcept {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    }

    void writeBytes(const std::uint8_t* bytes, DWORD length) {
        DWORD written = 0;
        if (!WriteFile(handle, bytes, length, &written, nullptr) || written != length) {
            const DWORD error = GetLastError();
            fail((error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_INVALID_HANDLE ||
                  error == ERROR_OPERATION_ABORTED)
                     ? ErrorCode::UsbDisconnected
                     : ErrorCode::IoError,
                 "Serial write failed: " + win32Message(error));
        }
    }

    std::uint8_t readByte() {
        std::uint8_t byte = 0;
        DWORD received = 0;
        if (!ReadFile(handle, &byte, 1, &received, nullptr)) {
            const DWORD error = GetLastError();
            fail((error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_INVALID_HANDLE ||
                  error == ERROR_OPERATION_ABORTED)
                     ? ErrorCode::UsbDisconnected
                     : ErrorCode::IoError,
                 "Serial read failed: " + win32Message(error));
        }
        if (received != 1) {
            fail(ErrorCode::Timeout, "Timed out waiting for Arduino response");
        }
        return byte;
    }

    std::array<std::uint8_t, 5 + kMaxPayload + 2> readFrame() {
        std::uint8_t byte = 0;
        do {
            byte = readByte();
        } while (byte != kStart);

        std::array<std::uint8_t, 5 + kMaxPayload + 2> frame{};
        frame[0] = byte;
        for (std::size_t i = 1; i < 5; ++i) {
            frame[i] = readByte();
        }
        if (frame[1] != kVersion || frame[3] > kMaxPayload) {
            fail(ErrorCode::InvalidPacket, "Invalid response header");
        }
        const std::size_t total = 5 + frame[3] + 2;
        for (std::size_t i = 5; i < total; ++i) {
            frame[i] = readByte();
        }
        const std::uint16_t expected = static_cast<std::uint16_t>(frame[total - 2]) |
                                       static_cast<std::uint16_t>(frame[total - 1] << 8);
        if (crc16(&frame[1], 4 + frame[3]) != expected) {
            fail(ErrorCode::ChecksumFailure, "Response checksum failed");
        }
        return frame;
    }

    std::array<std::uint8_t, 5 + kMaxPayload + 2> transact(
        std::uint8_t command, const std::uint8_t* payload, std::uint8_t length) {
        if (length > kMaxPayload) {
            fail(ErrorCode::InvalidPacket, "Payload is too large");
        }
        const std::uint8_t seq = ++sequence;
        std::array<std::uint8_t, 5 + kMaxPayload + 2> frame{};
        frame[0] = kStart;
        frame[1] = kVersion;
        frame[2] = command;
        frame[3] = length;
        frame[4] = seq;
        if (length != 0) {
            std::memcpy(&frame[5], payload, length);
        }
        const std::uint16_t checksum = crc16(&frame[1], 4 + length);
        frame[5 + length] = static_cast<std::uint8_t>(checksum & 0xFF);
        frame[6 + length] = static_cast<std::uint8_t>(checksum >> 8);
        writeBytes(frame.data(), static_cast<DWORD>(7 + length));

        const auto response = readFrame();
        const std::size_t responseLength = response[3];
        if (response[4] != seq) {
            fail(ErrorCode::InvalidPacket, "Response sequence does not match request");
        }
        if (response[2] == kErrorResponse) {
            fail(responseLength == 1 && response[5] == 1 ? ErrorCode::InvalidCommand
                                                          : ErrorCode::InvalidPacket,
                 "Arduino rejected the command");
        }
        return response;
    }
};

UnoGPIO::UnoGPIO(const std::string& comPort, unsigned int baudRate) : impl_(new Impl()) {
    std::string device = comPort.rfind("\\\\.\\", 0) == 0 ? comPort : "\\\\.\\" + comPort;
    impl_->handle = CreateFileA(device.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (impl_->handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        delete impl_;
        impl_ = nullptr;
        throw Error(ErrorCode::ComPortUnavailable, "Cannot open " + comPort + ": " + win32Message(error));
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(impl_->handle, &dcb)) {
        const DWORD error = GetLastError();
        impl_->close();
        delete impl_;
        impl_ = nullptr;
        throw Error(ErrorCode::ComPortUnavailable, "Cannot configure serial port: " + win32Message(error));
    }
    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(impl_->handle, &dcb)) {
        const DWORD error = GetLastError();
        impl_->close();
        delete impl_;
        impl_ = nullptr;
        throw Error(ErrorCode::ComPortUnavailable, "Cannot configure serial port: " + win32Message(error));
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutConstant = 150;
    timeouts.WriteTotalTimeoutConstant = 150;
    if (!SetCommTimeouts(impl_->handle, &timeouts)) {
        const DWORD error = GetLastError();
        impl_->close();
        delete impl_;
        impl_ = nullptr;
        throw Error(ErrorCode::ComPortUnavailable, "Cannot configure serial timeouts: " + win32Message(error));
    }
    // Opening an Uno commonly toggles DTR and resets its bootloader.
    Sleep(2000);
    PurgeComm(impl_->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

UnoGPIO::~UnoGPIO() {
    if (impl_ != nullptr) {
        impl_->close();
        delete impl_;
    }
}

UnoGPIO::UnoGPIO(UnoGPIO&& other) noexcept : impl_(std::exchange(other.impl_, nullptr)) {}
UnoGPIO& UnoGPIO::operator=(UnoGPIO&& other) noexcept {
    if (this != &other) {
        if (impl_ != nullptr) {
            impl_->close();
            delete impl_;
        }
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

void UnoGPIO::writeAll(std::uint16_t outputMask, std::uint16_t valueMask) {
    if ((outputMask | valueMask) & 0xF000) {
        throw Error(ErrorCode::InvalidPacket, "GPIO masks may only use bits 0 through 11");
    }
    const std::uint8_t payload[4] = {
        static_cast<std::uint8_t>(outputMask), static_cast<std::uint8_t>(outputMask >> 8),
        static_cast<std::uint8_t>(valueMask), static_cast<std::uint8_t>(valueMask >> 8)};
    const auto response = impl_->transact(kWriteAll, payload, 4);
    if (response[2] != kAck || response[3] != 0) {
        throw Error(ErrorCode::InvalidPacket, "Invalid write acknowledgement");
    }
    impl_->outputMask = outputMask;
    impl_->outputValues = valueMask & outputMask;
}

void UnoGPIO::setOutput(unsigned int pin) {
    writeAll(static_cast<std::uint16_t>(impl_->outputMask | pinBit(pin)), impl_->outputValues);
}

void UnoGPIO::setInput(unsigned int pin) {
    writeAll(static_cast<std::uint16_t>(impl_->outputMask & ~pinBit(pin)), impl_->outputValues);
}

void UnoGPIO::write(unsigned int pin, bool high) {
    const std::uint16_t bit = pinBit(pin);
    if ((impl_->outputMask & bit) == 0) {
        throw Error(ErrorCode::InvalidGpioPin, "Pin must be configured as output before writing");
    }
    const std::uint16_t values = high ? static_cast<std::uint16_t>(impl_->outputValues | bit)
                                      : static_cast<std::uint16_t>(impl_->outputValues & ~bit);
    writeAll(impl_->outputMask, values);
}

bool UnoGPIO::read(unsigned int pin) {
    const std::uint16_t bit = pinBit(pin);
    return (readAll() & bit) != 0;
}

std::uint16_t UnoGPIO::readAll() {
    const auto response = impl_->transact(kReadAll, nullptr, 0);
    if (response[2] != kReadResponse || response[3] != 2) {
        throw Error(ErrorCode::InvalidPacket, "Invalid read response");
    }
    return static_cast<std::uint16_t>(response[5]) | static_cast<std::uint16_t>(response[6] << 8);
}

std::uint16_t UnoGPIO::outputMask() const noexcept { return impl_->outputMask; }
std::uint16_t UnoGPIO::outputValueMask() const noexcept { return impl_->outputValues; }

} // namespace unogpio
