#pragma once
#include <QtCore>
#include <memory>

namespace daq {
// One receiver belongs to one readout worker. Opening/closing it never changes
// FPGA or board-management registers: the worker owns the DMA engine lifetime.
class DmaReceiver final {
  public:
    // The SDK's DMA example permits a 32 MiB message. A bounded ZMQ queue applies
    // backpressure; complete messages are retained before slicing for the caller.
    static constexpr int MaxMessageBytes = 32 * 1024 * 1024;
    static constexpr int ReceiveHighWaterMark = 2;
    DmaReceiver();
    ~DmaReceiver();
    DmaReceiver(const DmaReceiver &) = delete;
    DmaReceiver &operator=(const DmaReceiver &) = delete;
    void open(const QString &endpoint, int timeoutMs);
    void close() noexcept;
    bool isOpen() const;
    QByteArray read(int maxBytes); // Nonblocking; empty means no message yet.
    // A matching FPGA byte count alone is insufficient if the last ZMQ message
    // contained additional data. Preserve any extra bytes as a transport fault.
    void verifyEmpty();
    QByteArray takePending(); // Local bytes only; no network I/O or close.

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace daq
