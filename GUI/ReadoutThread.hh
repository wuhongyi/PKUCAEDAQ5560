#pragma once
#include "DeviceHandle.hh"
#include "EventDecoder.hh"
#include "Waveform.hh"
#include <QThread>
#include <functional>
#include <deque>
#include <atomic>

namespace daq {
struct ReadoutMetrics {
    int requestBytes = 65536;
    quint64 bytes = 0, fifoCalls = 0, emptyReads = 0, shortReads = 0;
    quint64 elapsedNs = 0, fifoNs = 0, writeNs = 0, decodeNs = 0;
    quint64 monitorNs = 0, countersNs = 0, waveformNs = 0, idleNs = 0;
    quint64 idleWaits = 0, counterCycles = 0;
    QJsonObject json() const;
};
struct Snapshot {
    bool connected = false;
    bool dma = false, dmaPending = false;
    // Separate diagnostic identity; it does not select or change the DMA API.
    quint32 dmaBuildId = 0xdeadbeefu;
    QString dmaBuildIdText() const;
    QString dmaBuildDescription() const;
    bool dmaBuildRecognized() const {
        return dma && (dmaBuildId == 0x26102202u || dmaBuildId == 0x26102301u);
    }
    quint32 diagnosticsVersion = 0;
    bool diagnosticsValid = false;
    quint32 diagnosticsSequence = 0, diagnosticsStartCount = 0;
    quint32 channelFifoDepth = 0, dmaFifoDepth = 0, dmaFifoPeak = 0;
    quint64 dmaStallMaxCycles = 0;
    qint64 diagnosticsReadMs = 0;
    std::array<quint32, Channels> rejectedProcessing{}, rejectedFifo{}, channelFifoPeak{};
    bool diagnosticsSupported() const { return diagnosticsVersion == R5560_DIAG_API; }
    bool diagnosticsAvailable() const {
        return diagnosticsSupported() && diagnosticsValid && diagnosticsStartCount == startCount;
    }
    QJsonObject diagnosticsJson() const;
    quint64 dmaTxWords = 0, dmaAckWords = 0;
    bool faultStopAttempted = false, faultStopConfirmed = false;
    bool baseboardRunLowConfirmed = false;
    QString faultUtc;
    quint32 status = 0, errors = 0, format = 0, api = 0, clockHz = 0, startCount = 0;
    quint32 startCountAtPrepare = 0;
    qint64 runStatusReadMs = 0; // Last complete status/errors/start-count sample, UTC ms.
    quint64 bytes = 0, events = 0, t0Ticks = 0;
    quint32 t0Count = 0;
    double megabytesPerSecond = 0;
    ReadoutMetrics readout;
    ModuleSettings settings;
    std::array<quint32, Channels> input{}, output{}, accepted{}, rejected{};
    std::array<quint64, Channels> received{}, underflow{}, overflow{};
    std::array<std::array<quint64, 512>, Channels> histogram{};
};
struct RunOptions {
    quint32 run = 0;
    int fifoKiB = 64;
    QString file;
    ModuleSettings expectedSettings;
};
class ReadoutThread final : public QThread {
    Q_OBJECT
  public:
    ReadoutThread(int index, const Endpoint &endpoint, bool demo, QObject *parent = nullptr);
    ~ReadoutThread() override;
    Snapshot snapshot() const;
    void configure(const ModuleSettings &settings);
    void refreshSettings();
    void clearErrors();
    void resetDmaEpoch(bool checkOnly = false);
    void prepare(const RunOptions &options);
    void arm();
    void enableBaseboardRunControl(); // Call before starting this worker.
    void setBaseboardRun(bool high, quint64 request);
    void cancelRunStart();
    void stopRun();
    void recover(const QString &directory);
    void cancelRecovery();
    void demoRun();
    void captureWaveform(const WaveformRequest &request);
    void cancelWaveform();
    void shutdown();
  signals:
    void connectionProgress(int index, const QString &message);
    void connected(int index);
    void configured(int index);
    void dmaResetReady(int index);
    void prepared(int index);
    void armed(int index);
    void completed(int index);
    void recoveryCompleted(int index);
    void failed(int index, const QString &message);
    void faultDetected(int index, const QString &message);
    void baseboardRunWritten(quint64 request, bool high, bool performed);
    void waveformReady(const daq::Waveform &waveform);
    void waveformFailed(int index, const QString &message);

  protected:
    void run() override;

  private:
    int index;
    Endpoint endpoint;
    bool demo;
    bool baseboardRunController = false, controllingBaseboardRun = false;
    std::atomic<bool> runCancelled{false};
    mutable QMutex stateMutex;
    Snapshot published, live;
    QMutex queueMutex;
    QWaitCondition wake;
    std::deque<std::function<void()>> queue;
    std::atomic<bool> quitting{false};
    std::unique_ptr<DeviceHandle> device;
    std::unique_ptr<EventDecoder> decoder;
    QFile outputFile;
    bool recovering = false;
    std::atomic<bool> recoveryCancelled{false};
    QString recoveryMetadataPath;
    QJsonObject recoveryMetadata;
    QElapsedTimer recoveryTimer;
    bool pending = false, active = false, readySent = false, seenRunning = false, stopping = false;
    QElapsedTimer armTimer, drainTimer, monitorTimer, rateTimer;
    QElapsedTimer counterTimer, readoutTimer, reportTimer;
    ReadoutMetrics lastReport;
    int counterStep = -1;
    quint32 counterT0Before = 0;
    std::array<QVector<quint32>, 4> pendingCounters;
    std::array<QVector<quint32>, 3> pendingDiagnostics;
    QVector<quint32> pendingDiagnosticHeader;
    quint64 lastBytes = 0;
    int fifoWords = 16384;
    quint64 recoveryDmaBase = 0;
    bool dmaPrepared = false;
    bool psDmaHandshake = false; // Once observed, never silently downgrade to legacy PS.
    QElapsedTimer dmaHealthTimer, dmaStallTimer;
    quint32 startBase = 0;
    bool wavePending = false, waveReading = false;
    WaveformRequest waveRequest;
    Waveform wave;
    QElapsedTimer waveTimer, wavePollTimer;
    quint32 wavePosition = 0, waveExpectedSequence = 0;
    bool initializeDevice();
    void pollRecovery();
    void writeRecoveryMetadata(const QString &state, const QString &reason = QString());
    void pollWaveform();
    void endWaveform(const QString &reason);
    void post(std::function<void()> operation, bool urgent = false);
    void publish();
    void readCounters();
    void readCounterStep();
    void probeDiagnostics();
    void readRunStatus();
    QByteArray readEventFifo();
    quint64 readDmaCounter(quint32 lowAddress);
    void updateDmaState();
    bool eventDataDrained(quint64 receivedBytes, quint64 baseWords = 0);
    void acknowledgeDma();
    void reportReadout(bool final = false);
    void poll();
    void finish();
    void writeBytes(const QByteArray &bytes);
    QString stopAfterFailure(bool freshConnection);
    QString savePartialReply(const TransportError &error,
                             const QString &suffix = ".partial-reply.raw");
};
} // namespace daq
