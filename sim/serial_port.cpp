#include "sim/serial_port.hpp"

#include <cstdio>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace sim {

bool SerialPort::open(const char* name) {
    const std::string path = std::string("\\\\.\\") + name;  // required for COM10 and above
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = 115200;  // ignored by USB CDC
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    SetCommState(h, &dcb);

    COMMTIMEOUTS t{};
    t.ReadIntervalTimeout = MAXDWORD;
    t.ReadTotalTimeoutMultiplier = MAXDWORD;
    t.ReadTotalTimeoutConstant = 100;  // return as soon as any byte arrives, or after 100 ms
    t.WriteTotalTimeoutConstant = 200;
    SetCommTimeouts(h, &t);

    handle_ = h;
    return true;
}

void SerialPort::close() {
    if (handle_) CloseHandle(handle_);
    handle_ = nullptr;
}

int SerialPort::read(uint8_t* data, size_t size) {
    DWORD n = 0;
    if (!ReadFile(handle_, data, DWORD(size), &n, nullptr)) return -1;
    return int(n);
}

bool SerialPort::write(const uint8_t* data, size_t size) {
    DWORD n = 0;
    return WriteFile(handle_, data, DWORD(size), &n, nullptr) && n == size;
}

}  // namespace sim

#else
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace sim {
namespace {
int fd_of(void* h) { return int(reinterpret_cast<intptr_t>(h)) - 1; }
}

bool SerialPort::open(const char* name) {
    const int fd = ::open(name, O_RDWR | O_NOCTTY);
    if (fd < 0) return false;
    termios t{};
    tcgetattr(fd, &t);
    cfmakeraw(&t);
    t.c_cflag &= ~tcflag_t(HUPCL);
    t.c_cflag |= CLOCAL;
    tcsetattr(fd, TCSANOW, &t);
    int lines = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIC, &lines);
    handle_ = reinterpret_cast<void*>(intptr_t(fd) + 1);
    return true;
}

void SerialPort::close() {
    if (handle_) ::close(fd_of(handle_));
    handle_ = nullptr;
}

int SerialPort::read(uint8_t* data, size_t size) {
    pollfd p{fd_of(handle_), POLLIN, 0};
    const int r = poll(&p, 1, 100);
    if (r < 0) return -1;
    if (r == 0) return 0;
    const ssize_t n = ::read(fd_of(handle_), data, size);
    return n <= 0 ? -1 : int(n);
}

bool SerialPort::write(const uint8_t* data, size_t size) {
    return ::write(fd_of(handle_), data, size) == ssize_t(size);
}

}  // namespace sim
#endif
