#include "cam86/usb/IUsbTransport.hpp"

#ifdef _WIN32
#include <windows.h>

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace cam86 {
namespace {

// D2XX uses WINAPI and pointer-sized FT_HANDLE, including in 64-bit builds.
using FtHandle = void*;
using FtStatus = ULONG;

void check(const FtStatus status, const std::string& operation) {
    if (status != 0) {
        throw std::runtime_error(operation + ": D2XX status " + std::to_string(status));
    }
}

class D2xxFtdiTransport final : public IUsbTransport {
public:
    D2xxFtdiTransport() {
        // Prefer the DLL installed with the working FTDI driver. The legacy
        // application's bundled 32-bit DLL cannot be loaded by a 64-bit build.
        module_ = LoadLibraryExW(L"ftd2xx.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_) {
            module_ = LoadLibraryExW(L"ftd2xx.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
        }
        if (!module_) {
            throw std::runtime_error("Cannot load ftd2xx.dll (Windows error " +
                std::to_string(GetLastError()) +
                "). Install the FTDI D2XX driver or provide a DLL matching the application's architecture.");
        }
        try {
            createList_ = symbol<CreateList>("FT_CreateDeviceInfoList");
            getDetail_ = symbol<GetDetail>("FT_GetDeviceInfoDetail");
            openEx_ = symbol<OpenEx>("FT_OpenEx");
            close_ = symbol<HandleOperation>("FT_Close");
            reset_ = symbol<HandleOperation>("FT_ResetDevice");
            purge_ = symbol<DwordOperation>("FT_Purge");
            baud_ = symbol<DwordOperation>("FT_SetBaudRate");
            latency_ = symbol<Latency>("FT_SetLatencyTimer");
            bitMode_ = symbol<BitMode>("FT_SetBitMode");
            timeouts_ = symbol<Timeouts>("FT_SetTimeouts");
            read_ = symbol<Transfer>("FT_Read");
            write_ = symbol<Transfer>("FT_Write");
        } catch (...) {
            FreeLibrary(module_);
            throw;
        }
    }

    ~D2xxFtdiTransport() override {
        close();
        FreeLibrary(module_);
    }

    void open(const std::uint16_t vendorId, const std::uint16_t productId,
              const std::string& serialPrefix) override {
        close();
        DWORD count = 0;
        check(createList_(&count), "FT_CreateDeviceInfoList");
        std::vector<std::string> serials;
        std::string discovered;
        for (DWORD i = 0; i < count; ++i) {
            DWORD flags = 0, type = 0, id = 0, location = 0;
            std::array<char, 16> serial{};
            std::array<char, 64> description{};
            FtHandle unused = nullptr;
            check(getDetail_(i, &flags, &type, &id, &location,
                             serial.data(), description.data(), &unused), "FT_GetDeviceInfoDetail");
            serial.back() = '\0';
            discovered += " [" + std::string(serial.data()) + "]";
            if (id == ((static_cast<DWORD>(vendorId) << 16U) | productId)) {
                serials.emplace_back(serial.data());
            }
        }
        for (const auto& serial : serials) {
            if (!serial.starts_with(serialPrefix) || !serial.ends_with('A')) continue;
            auto partner = serial;
            partner.back() = 'B';
            if (std::find(serials.begin(), serials.end(), partner) == serials.end()) continue;
            try {
                check(openEx_(const_cast<char*>(serial.c_str()), 1, &handles_[0]),
                      "FT_OpenEx " + serial);
                check(openEx_(partner.data(), 1, &handles_[1]), "FT_OpenEx " + partner);
                return;
            } catch (...) {
                close();
                throw;
            }
        }
        throw std::runtime_error("No matching " + serialPrefix +
            " A/B pair found through D2XX. Enumerated serials:" + discovered +
            ". Close other camera applications and check the FTDI driver.");
    }

    void close() noexcept override {
        for (auto& handle : handles_) {
            if (handle) close_(handle);
            handle = nullptr;
        }
    }

    bool isOpen() const noexcept override { return handles_[0] && handles_[1]; }
    void reset(FtdiChannel channel) override { check(reset_(handle(channel)), "FT_ResetDevice"); }
    void purge(FtdiChannel channel, bool rx, bool tx) override {
        const DWORD mask = (rx ? 1U : 0U) | (tx ? 2U : 0U);
        if (mask) check(purge_(handle(channel), mask), "FT_Purge");
    }
    void setLatency(FtdiChannel channel, std::uint8_t milliseconds) override {
        check(latency_(handle(channel), milliseconds), "FT_SetLatencyTimer");
    }
    void setBaudRate(FtdiChannel channel, std::uint32_t rate) override {
        check(baud_(handle(channel), rate), "FT_SetBaudRate");
    }
    void setBitMode(FtdiChannel channel, std::uint8_t mask, std::uint8_t mode) override {
        check(bitMode_(handle(channel), mask, mode), "FT_SetBitMode");
    }
    void write(FtdiChannel channel, std::span<const std::uint8_t> bytes,
               std::chrono::milliseconds timeout) override {
        const auto device = handle(channel);
        setTimeout(device, timeout);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1U << 20U));
            DWORD transferred = 0;
            check(write_(device, const_cast<std::uint8_t*>(bytes.data() + offset), chunk, &transferred),
                  "FT_Write");
            if (transferred == 0 || transferred > chunk) throw std::runtime_error("FT_Write incomplete transfer");
            offset += transferred;
        }
    }
    std::vector<std::uint8_t> read(FtdiChannel channel, std::size_t payloadBytes,
                                 std::chrono::milliseconds timeout) override {
        const auto device = handle(channel);
        setTimeout(device, timeout);
        std::vector<std::uint8_t> bytes(payloadBytes);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1U << 20U));
            DWORD transferred = 0;
            check(read_(device, bytes.data() + offset, chunk, &transferred), "FT_Read");
            if (transferred == 0 || transferred > chunk) {
                throw std::runtime_error("FT_Read timed out after " + std::to_string(offset) +
                                         " of " + std::to_string(payloadBytes) + " bytes");
            }
            offset += transferred;
        }
        // D2XX already removes USB modem-status bytes.
        return bytes;
    }

private:
    using CreateList = FtStatus (WINAPI*)(DWORD*);
    using GetDetail = FtStatus (WINAPI*)(DWORD, DWORD*, DWORD*, DWORD*, DWORD*, void*, void*, FtHandle*);
    using OpenEx = FtStatus (WINAPI*)(void*, DWORD, FtHandle*);
    using HandleOperation = FtStatus (WINAPI*)(FtHandle);
    using DwordOperation = FtStatus (WINAPI*)(FtHandle, DWORD);
    using Latency = FtStatus (WINAPI*)(FtHandle, UCHAR);
    using BitMode = FtStatus (WINAPI*)(FtHandle, UCHAR, UCHAR);
    using Timeouts = FtStatus (WINAPI*)(FtHandle, DWORD, DWORD);
    using Transfer = FtStatus (WINAPI*)(FtHandle, void*, DWORD, DWORD*);

    template<class T> T symbol(const char* name) {
        auto address = GetProcAddress(module_, name);
        if (!address) throw std::runtime_error(std::string("ftd2xx.dll is missing ") + name);
        return reinterpret_cast<T>(address);
    }
    FtHandle handle(FtdiChannel channel) const {
        auto result = handles_[static_cast<std::size_t>(channel)];
        if (!result) throw std::runtime_error("D2XX transport is not open");
        return result;
    }
    void setTimeout(FtHandle device, std::chrono::milliseconds timeout) {
        if (timeout.count() <= 0 || timeout.count() > std::numeric_limits<DWORD>::max()) {
            throw std::invalid_argument("D2XX timeout is out of range");
        }
        const auto value = static_cast<DWORD>(timeout.count());
        check(timeouts_(device, value, value), "FT_SetTimeouts");
    }

    HMODULE module_ = nullptr;
    std::array<FtHandle, 2> handles_{};
    CreateList createList_ = nullptr;
    GetDetail getDetail_ = nullptr;
    OpenEx openEx_ = nullptr;
    HandleOperation close_ = nullptr, reset_ = nullptr;
    DwordOperation purge_ = nullptr, baud_ = nullptr;
    Latency latency_ = nullptr;
    BitMode bitMode_ = nullptr;
    Timeouts timeouts_ = nullptr;
    Transfer read_ = nullptr, write_ = nullptr;
};
} // namespace

std::unique_ptr<IUsbTransport> makeD2xxFtdiTransport() {
    return std::make_unique<D2xxFtdiTransport>();
}
} // namespace cam86
#endif
