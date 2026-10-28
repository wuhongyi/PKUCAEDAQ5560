#pragma once
#include "Config.hh"
#include <QTcpSocket>
#include <memory>
#include <functional>

namespace daq {
class DmaReceiver;
// R5560 board-management address, NOT a user FPGA Avalon word address.
constexpr quint32 BaseboardRunRegister = 0x00140001u; // (20 << 16) | sync_reg index 1
constexpr quint32 PsDmaEnableRegister = 0x001e0000u;
constexpr quint32 PsDmaControlRegister = 0x001f0000u; // Read status; write 1 only for explicit discard.
constexpr quint32 PsDmaStatusMagic = 0x50530000u;
constexpr quint32 PsDmaReady = 1u, PsDmaEnabled = 2u, PsDmaQuiesced = 4u,
                  PsDmaFault = 8u, PsDmaDirty = 16u;
constexpr quint32 RunInputHigh = 1u << 5;
// Initialization may repeat read-only requests. Fault reconnections only lower
// baseboard RUN / issue bounded STOP; FIFO reads are never replayed.
struct TransportError : Error {
    explicit TransportError(const QString &text, const QByteArray &reply = {}, bool isDmaPayload = false)
        : Error(text), partialReply(reply), dmaPayload(isDmaPayload) {}
    // Avalon: wire bytes may include the FIFO count header. DMA: raw data bytes
    // already received but not yet returned to the worker. Never parse as a count.
    QByteArray partialReply;
    const bool dmaPayload;
};
class DeviceHandle {
  public:
    virtual ~DeviceHandle() = default;
    virtual quint32 read(quint32 address) = 0;
    virtual void write(quint32 address, quint32 data) = 0;
    virtual void writeInternal(quint32 address, quint32 data) = 0;
    // Non-TCP/legacy implementations expose no PS handshake capability.
    virtual quint32 readInternal(quint32) { return 0; }
    virtual QVector<quint32> readBlock(quint32 address, int count);
    virtual QByteArray readFifo(int maxWords) = 0;
    virtual void selectEventTransport(quint32 api);
    virtual bool isDma() const { return false; }
    virtual void openEventStream() {}
    virtual void closeEventStream() {}
    virtual QByteArray readEventData(int maxWords) { return readFifo(maxWords); }
    virtual void verifyEventStreamEmpty() {}
    virtual QByteArray takePendingEventData() { return {}; }
    virtual void demoRun() {
        throw Error("External RUN is a physical input, not an Ethernet command");
    }
    void writeChecked(quint32 address, quint32 data);
    ModuleSettings readSettings();
    void writeSettings(const ModuleSettings &settings);
    void requireIdle();
};
// Uses the supplied SDK register/FIFO wire packets; DMA events use its ZMQ path.
// Qt sockets add deadlines, explicit little endian encoding and exact-length I/O.
// Construct and use only in its owning readout thread. No FELib / AXI bridge.
class TcpDevice final : public DeviceHandle {
  public:
    static constexpr int WarningTimeoutMs = 1500, OperationTimeoutMs = 5000,
                         InitializationTimeoutMs = 5000;
    explicit TcpDevice(const Endpoint &ep, int timeoutMs = OperationTimeoutMs);
    ~TcpDevice() override;
    void setTimeout(int timeoutMs);
    void setDiagnosticHandler(std::function<void(const QString &)> handler);
    quint32 read(quint32 address) override;
    void write(quint32 address, quint32 data) override;
    void writeInternal(quint32 address, quint32 data) override;
    quint32 readInternal(quint32 address) override;
    QVector<quint32> readBlock(quint32 address, int count) override;
    QByteArray readFifo(int maxWords) override;
    void selectEventTransport(quint32 api) override;
    bool isDma() const override { return dmaMode; }
    void openEventStream() override;
    void closeEventStream() override;
    QByteArray readEventData(int maxWords) override;
    void verifyEventStreamEmpty() override;
    QByteArray takePendingEventData() override;

  private:
    QTcpSocket socket;
    QString dmaEndpoint;
    bool dmaMode = false;
    std::unique_ptr<DmaReceiver> dmaReceiver;
    int timeout;
    QElapsedTimer deadline;
    QString peer, request;
    QString requestStartedUtc;
    QByteArray responseBytes;
    std::function<void(const QString &)> diagnostic;
    bool warned = false;
    int receivedBytes = 0, expectedBytes = 0;
    enum class WaitFor { Connection, Written, Readable };
    bool waitFor(WaitFor condition);
    void send(const QVector<quint32> &words);
    QByteArray receive(int bytes);
    [[noreturn]] void fail(const QString &why);
    int remaining() const;
    void warnIfSlow();
    void completeRequest();
};
class DemoDevice final : public DeviceHandle {
  public:
    explicit DemoDevice(int module);
    quint32 read(quint32 address) override;
    void write(quint32 address, quint32 data) override;
    void writeInternal(quint32 address, quint32 data) override;
    QByteArray readFifo(int maxWords) override;
    void demoRun() override;

  private:
    QHash<quint32, quint32> registers;
    QElapsedTimer clock;
    quint32 status = Idle | ClockGood | FifoReady;
    quint64 nextEvent = 0;
    int module;
    std::array<quint32, Channels> counts{};
    QElapsedTimer waveClock;
    QVector<quint32> waveMemory;
    bool waveForced = false;
};
} // namespace daq
