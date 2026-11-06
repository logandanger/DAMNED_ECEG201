// Minimal blocking serial port. RTS and DTR stay deasserted: on the ESP32-S3 USB-Serial-JTAG
// they drive the chip's reset logic, and the box must keep running when the mirror attaches.
#pragma once
#include <cstddef>
#include <cstdint>

namespace sim {

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort() { close(); }
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool open(const char* name);
    void close();
    int read(uint8_t* data, size_t size);  // waits up to 100 ms; 0 on timeout, -1 on error
    bool write(const uint8_t* data, size_t size);

private:
    void* handle_ = nullptr;  // HANDLE, or the file descriptor stored as an integer plus one
};

}  // namespace sim
