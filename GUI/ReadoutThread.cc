#include "ReadoutThread.hh"
#include <limits>
#include <algorithm>
#include <mutex>

namespace daq {
namespace {
// Account for time even when an operation throws, without changing its deadline.
class MeasureTime {
    quint64 &total;
    QElapsedTimer timer;
  public:
    explicit MeasureTime(quint64 &value, bool enabled = true) : total(value) {
        if (enabled) timer.start();
    }
    ~MeasureTime() { if (timer.isValid()) total += quint64(timer.nsecsElapsed()); }
};
// Opcode 6 is available in the legacy bridge too; unmapped internal register 31
// returns zero there. A recognized PS must keep reporting its capability, so a
// vanished/failed reset response cannot silently fall back to legacy behavior.
bool checkPsDmaState(DeviceHandle &device, const QString &stage, quint32 required,
                     quint32 forbidden, bool requireSupported = false) {
    const auto status = device.readInternal(PsDmaControlRegister);
    if (!status && !requireSupported) return false;
    if ((status & 0xffff0000u) != PsDmaStatusMagic ||
        (status & required) != required || (status & forbidden))
        throw Error(QString("PS DMA %1 failed: status=0x%2; required flags=0x%3, forbidden flags=0x%4. "
                            "No FPGA epoch clear is permitted until PS reset is confirmed.")
                        .arg(stage).arg(status, 8, 16, QChar('0'))
                        .arg(required, 2, 16, QChar('0')).arg(forbidden, 2, 16, QChar('0')));
    return true;
}
}
QString Snapshot::dmaBuildIdText() const {
    if (!dma || dmaBuildId == 0xdeadbeefu || dmaBuildId == 0 || dmaBuildId == 0xffffffffu)
        return "unknown";
    return QString("0x%1").arg(dmaBuildId, 8, 16, QChar('0'));
}
QString Snapshot::dmaBuildDescription() const {
    if (dma && dmaBuildId == 0x26102301u)
        return dmaBuildIdText() + " (ACK-to-READY repair + FIFO diagnostics)";
    if (dmaBuildRecognized())
        return dmaBuildIdText() + " (ACK-to-READY repair)";
    return dmaBuildIdText() == "unknown"
               ? QString("unknown (build ID unavailable; compatible legacy firmware)")
               : dmaBuildIdText() + " (unrecognized build; compatibility follows DMA interface version)";
}
QJsonObject Snapshot::diagnosticsJson() const {
    const bool valid = diagnosticsAvailable();
    return {{"version_u32", double(diagnosticsVersion)},
            {"supported", diagnosticsSupported()}, {"valid", valid},
            {"snapshot_sequence_u32", valid ? QJsonValue(double(diagnosticsSequence)) : QJsonValue()},
            {"start_count_u32", valid ? QJsonValue(double(diagnosticsStartCount)) : QJsonValue()},
            {"read_completed_utc", valid ? QJsonValue(QDateTime::fromMSecsSinceEpoch(diagnosticsReadMs, Qt::UTC)
                                                  .toString(Qt::ISODateWithMs)) : QJsonValue()},
            {"channel_fifo_depth_events", valid ? QJsonValue(double(channelFifoDepth)) : QJsonValue()},
            {"dma_fifo_depth_events", valid ? QJsonValue(double(dmaFifoDepth)) : QJsonValue()},
            {"dma_fifo_peak_events", valid ? QJsonValue(double(dmaFifoPeak)) : QJsonValue()},
            {"dma_stall_max_cycles_u64", valid ? QJsonValue(QString::number(dmaStallMaxCycles)) : QJsonValue()},
            {"dma_stall_max_ns", valid && clockHz ? QJsonValue(double(dmaStallMaxCycles) * 1e9 / clockHz) : QJsonValue()},
            {"clock_hz", double(clockHz)}};
}
QJsonObject ReadoutMetrics::json() const {
    return {{"request_bytes", requestBytes},
            {"bytes", QString::number(bytes)}, {"fifo_calls", QString::number(fifoCalls)},
            {"empty_reads", QString::number(emptyReads)}, {"short_reads", QString::number(shortReads)},
            {"elapsed_ms", elapsedNs / 1e6}, {"fifo_ms", fifoNs / 1e6},
            {"write_ms", writeNs / 1e6}, {"decode_ms", decodeNs / 1e6},
            {"monitor_ms", monitorNs / 1e6}, {"counters_ms", countersNs / 1e6},
            {"waveform_ms", waveformNs / 1e6}, {"idle_ms", idleNs / 1e6},
            {"idle_waits", QString::number(idleWaits)},
            {"counter_cycles", QString::number(counterCycles)}};
}
ReadoutThread::ReadoutThread(int i, const Endpoint &ep, bool simulated, QObject *p)
    : QThread(p), index(i), endpoint(ep), demo(simulated) {
    // Qt 5.15.2 lazily registers the Gregorian calendar without locking.
    // Initialize it before any DAQ worker formats its first timestamp. Keep
    // this safe even when different callers construct workers concurrently.
    static std::once_flag calendarReady;
    std::call_once(calendarReady, [] { (void)QDate(2000, 1, 1).toString(Qt::ISODate); });
}
ReadoutThread::~ReadoutThread() {
    shutdown();
    wait();
}
Snapshot ReadoutThread::snapshot() const {
    QMutexLocker lock(&stateMutex);
    return published;
}
void ReadoutThread::publish() {
    if (readoutTimer.isValid())
        live.readout.elapsedNs = quint64(readoutTimer.nsecsElapsed());
    QMutexLocker lock(&stateMutex);
    published = live;
}
void ReadoutThread::post(std::function<void()> op, bool urgent) {
    QMutexLocker lock(&queueMutex);
    if (urgent)
        queue.push_front(std::move(op));
    else
        queue.push_back(std::move(op));
    wake.wakeOne();
}
void ReadoutThread::shutdown() {
    quitting = true;
    wake.wakeAll();
}
void ReadoutThread::configure(const ModuleSettings &s) {
    post([this, s] {
        if (pending)
            throw Error("Cannot configure during a run");
        device->writeSettings(s);
        live.settings = device->readSettings();
        publish();
        emit configured(index);
    });
}
void ReadoutThread::refreshSettings() {
    post([this] {
        if (pending)
            throw Error("Cannot read settings during a run");
        device->requireIdle();
        live.settings = device->readSettings();
        publish();
        emit configured(index);
    });
}
void ReadoutThread::clearErrors() {
    post([this] {
        device->requireIdle();
        device->write(R5560_SYNC_COMMAND, R5560_CMD_CLEAR_ERRORS);
        live.errors = device->read(R5560_SYNC_ERRORS);
        if (live.errors)
            throw Error("Hardware errors remain active");
        publish();
        emit configured(index);
    });
}
void ReadoutThread::resetDmaEpoch(bool checkOnly) {
    post([this, checkOnly] {
        if (pending || active || recovering)
            throw Error("Cannot discard DMA data during a host acquisition or recovery");
        const auto state = device->read(R5560_SYNC_STATUS);
        if (state & (Running | RunInputHigh | Preparing))
            throw Error("DMA reset requires stopped devices and physical RUN LOW");
        if (!device->isDma() && !(state & Idle))
            throw Error("Legacy Avalon devices must be IDLE before a crate DMA reset");
        if (checkOnly) { emit dmaResetReady(index); return; }
        if (device->isDma()) {
            // Reached only through the explicit GUI discard confirmation. This
            // intentionally destroys unread DDR/FIFO data; never used for STOP.
            device->write(R5560_LIST_BUS_CONFIG, 0);
            device->write(R5560_SYNC_COMMAND, R5560_CMD_STOP);
            device->closeEventStream();
            dmaPrepared = false;
            device->writeInternal(PsDmaEnableRegister, 0);
            psDmaHandshake = checkPsDmaState(*device, "quiesce before explicit discard",
                                PsDmaReady | PsDmaQuiesced, PsDmaEnabled, psDmaHandshake);
            if (psDmaHandshake) {
                device->writeInternal(PsDmaControlRegister, 1);
                checkPsDmaState(*device, "explicit discard", PsDmaReady | PsDmaQuiesced,
                                PsDmaEnabled | PsDmaFault | PsDmaDirty, true);
                emit connectionProgress(index, "PS DMA explicit discard confirmed; clearing FPGA epoch next.");
            } else {
                emit connectionProgress(index, "Legacy PS: explicit discard cannot confirm that PS/ZeroMQ "
                                                "queues are empty; only the legacy DMA/FPGA reset is available.");
            }
            device->write(R5560_SYNC_COMMAND, R5560_CMD_CLEAR);
            QElapsedTimer timer; timer.start();
            do {
                live.status = device->read(R5560_SYNC_STATUS);
                if (live.status & Idle) break;
                if (timer.elapsed() > 3000) throw Error("DMA epoch reset did not reach IDLE");
                QThread::msleep(2);
            } while (true);
            updateDmaState();
            if (live.dmaPending || live.dmaTxWords || live.dmaAckWords)
                throw Error("DMA epoch reset failed counter/empty verification");
        }
        live.errors = device->read(R5560_SYNC_ERRORS);
        publish();
        emit configured(index);
    });
}
void ReadoutThread::prepare(const RunOptions &options) {
    runCancelled = false;
    post([this, options] {
        if (runCancelled) {
            emit completed(index);
            return;
        }
        if (pending)
            throw Error("Run already prepared");
        if (options.fifoKiB != 64 && options.fifoKiB != 128 && options.fifoKiB != 256)
            throw Error("Read block must be 64, 128 or 256 KiB");
        fifoWords = options.fifoKiB * 256;
        device->requireIdle();
        auto settings = device->readSettings();
        settings.validate();
        if (settings.json() != options.expectedSettings.json())
            throw Error(
                "Device settings changed since the last read. Refresh settings before Start.");
        if (device->read(R5560_API_VERSION) != live.api ||
            device->read(R5560_CLOCK_HZ) != ClockHz)
            throw Error("Firmware changed since connection; reconnect before Start.");
        live.status = device->read(R5560_SYNC_STATUS);
        live.errors = device->read(R5560_SYNC_ERRORS);
        if (!(live.status & ClockGood) || live.errors)
            throw Error("Clock is not locked or SYNC_ERRORS is nonzero");
        if ((settings.syncConfig & 1) && (live.status & (1u << 5)))
            throw Error("External RUN must be LOW before ARM");
        if (!(device->read(R5560_LIST_BUS_STATUS) & 1))
            throw Error("Residual FIFO data: cannot start a new epoch");
        updateDmaState();
        if (live.dmaPending)
            throw Error("Unacknowledged DMA data remains. Use Stop / Recover before a new run; "
                        "ARM would erase the previous transfer counters.");
        if (device->isDma()) {
            device->openEventStream();
            device->verifyEventStreamEmpty();
            // All words of the previous epoch were acknowledged above. Only in
            // this clean state may the vendor engine be reset for a new epoch.
            device->writeInternal(PsDmaEnableRegister, 0);
            psDmaHandshake = checkPsDmaState(*device, "prepare quiesce",
                PsDmaReady | PsDmaQuiesced, PsDmaEnabled | PsDmaFault | PsDmaDirty, psDmaHandshake);
            // Enable the vendor DDR-to-network service only after a receiver is ready.
            // This does not replace the physical shared RUN or the FPGA ARM command.
            device->writeInternal(PsDmaEnableRegister, 1);
            checkPsDmaState(*device, "prepare enable", PsDmaReady | PsDmaEnabled,
                            PsDmaQuiesced | PsDmaFault | PsDmaDirty, psDmaHandshake);
            dmaPrepared = true;
        }
        device->write(R5560_LIST_BUS_CONFIG,
                      0); // Disable the legacy run level; never assert CLEAR.
        device->writeChecked(R5560_DAQ_ID, endpoint.id);
        device->writeChecked(R5560_RUN_ID, options.run);
        startBase = device->read(
            R5560_START_COUNT); // START_COUNT is lifetime cumulative, not reset by ARM.
        live = Snapshot();
        live.connected = true;
        live.dma = device->isDma();
        live.startCount = live.startCountAtPrepare = startBase;
        live.settings = settings;
        live.api = device->read(R5560_API_VERSION);
        live.clockHz = device->read(R5560_CLOCK_HZ);
        probeDiagnostics();
        if (live.dma) {
            live.dmaBuildId = device->read(R5560_DMA_BUILD_ID);
            emit connectionProgress(index, QString("Run %1 DMA build: %2")
                                               .arg(options.run).arg(live.dmaBuildDescription()));
        }
        live.format = device->read(R5560_EVENT_FORMAT);
        if (live.format != 2)
            throw Error("Select external RUN or trigger timestamp mode (bit 6); event format 2 is "
                        "required");
        decoder = std::make_unique<EventDecoder>(settings);
        lastBytes = 0;
        rateTimer.restart();
        if (!options.file.isEmpty()) {
            outputFile.setFileName(options.file);
            if (!outputFile.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                throw Error("Cannot create data file: " + outputFile.errorString());
            QJsonObject header{
                {"schema", "r5560-list-v1"},
                {"module_index", index},
                {"daq_id", double(endpoint.id)},
                {"ip", endpoint.ip},
                {"run_id", double(options.run)},
                {"api_version", double(live.api)},
                {"dma_build_id", live.dmaBuildIdText()},
                {"dma_build_id_raw", live.dma ? QJsonValue(double(live.dmaBuildId)) : QJsonValue()},
                {"dma_build_id_recognized", live.dmaBuildRecognized()},
                {"event_format", int(live.format)},
                {"clock_hz", int(live.clockHz)},
                {"diagnostics_version", double(live.diagnosticsVersion)},
                {"event_bytes", 32},
                {"byte_order", "little"},
                {"simulated", demo},
                {"transport", live.dma ? "dma-zmq" : "avalon-fifo"},
                {"settings", settings.json()},
                {"created_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
            QByteArray json = QJsonDocument(header).toJson(QJsonDocument::Compact),
                       prefix("R5560D1\n", 8);
            appendWord(prefix, quint32(json.size()));
            writeBytes(prefix);
            writeBytes(json);
        }
        pending = true;
        active = false;
        readySent = false;
        seenRunning = false;
        stopping = false;
        counterStep = -1;
        readoutTimer.invalidate();
        publish();
        emit prepared(index);
    });
}
void ReadoutThread::arm() {
    post([this] {
        if (runCancelled)
            return;
        if (!pending || active)
            throw Error("Run not prepared");
        if (device->isDma()) device->verifyEventStreamEmpty();
        // DMA TX and ACK are reset atomically by the FPGA only when ARM is
        // accepted. Do not erase the previous epoch's ACK before sending ARM.
        device->write(R5560_SYNC_COMMAND, R5560_CMD_ARM);
        if (!(live.settings.syncConfig & 1))
            device->write(R5560_LIST_BUS_CONFIG, 1);
        active = true;
        live.readout = ReadoutMetrics();
        live.readout.requestBytes = fifoWords * 4;
        lastReport = ReadoutMetrics();
        readoutTimer.start();
        reportTimer.start();
        rateTimer.restart();
        counterTimer.start();
        counterStep = -1;
        armTimer.restart();
        monitorTimer.invalidate();
        dmaHealthTimer.start();
        dmaStallTimer.invalidate();
    });
}
void ReadoutThread::enableBaseboardRunControl() {
    Q_ASSERT(!isRunning());
    baseboardRunController = true;
}
void ReadoutThread::cancelRunStart() {
    runCancelled = true;
}
void ReadoutThread::setBaseboardRun(bool high, quint64 request) {
    post([this, high, request] {
        if (high && (runCancelled || quitting)) {
            emit baseboardRunWritten(request, high, false);
            return;
        }
        if (!baseboardRunController)
            throw Error("Only the first configured DAQ controls baseboard RUN");
        controllingBaseboardRun = true;
        live.baseboardRunLowConfirmed = false;
        if (high && (!pending || !active || !readySent))
            throw Error("Baseboard RUN rise requires an ARMED acquisition");
        device->writeInternal(BaseboardRunRegister, high ? 1 : 0);
        if (!high) {
            // Command 5 is unacknowledged. Confirm the routed signal at the FPGA
            // input instead of treating TCP send completion as a board ACK.
            QElapsedTimer settling;
            settling.start();
            do {
                live.status = device->read(R5560_SYNC_STATUS);
                if (!(live.status & RunInputHigh)) {
                    live.baseboardRunLowConfirmed = true;
                    break;
                }
                if (settling.elapsed() > 1000)
                    throw Error("Baseboard sync_reg.1=0 sent, but Mod 0 RUN input remains HIGH; "
                                "check baseboard routing");
                QMutexLocker lock(&queueMutex);
                wake.wait(&queueMutex, 10);
            } while (true);
        }
        controllingBaseboardRun = false;
        publish();
        emit baseboardRunWritten(request, high, true);
    }, !high);
}
void ReadoutThread::stopRun() {
    cancelRunStart();
    post([this] {
        if (pending) {
            device->write(R5560_LIST_BUS_CONFIG, 0);
            device->write(R5560_SYNC_COMMAND, R5560_CMD_STOP);
            if (!stopping)
                drainTimer.restart();
            stopping = true;
            if (!active)
                finish();
        }
    });
}
void ReadoutThread::recover(const QString &directory) {
    recoveryCancelled = false;
    post([this, directory] {
        if (pending || recovering)
            throw Error("Stop the current host operation before recovery");
        recoveryMetadataPath = QDir(directory).filePath(QString("M%1.json").arg(index));
        outputFile.setFileName(QDir(directory).filePath(QString("M%1.fifo.raw").arg(index)));
        live.bytes = live.events = 0;
        live.megabytesPerSecond = 0;
        live.readout = ReadoutMetrics();
        readoutTimer.invalidate();
        counterStep = -1;
        recoveryMetadata = {{"schema", "r5560-fifo-recovery-v1"},
                            {"module_index", index}, {"ip", endpoint.ip},
                            {"configured_daq_id", double(endpoint.id)},
                            {"dma_build_id", live.dmaBuildIdText()},
                            {"dma_build_id_raw", live.dma ? QJsonValue(double(live.dmaBuildId)) : QJsonValue()},
                            {"dma_build_id_recognized", live.dmaBuildRecognized()},
                            {"file", QFileInfo(outputFile.fileName()).fileName()},
                            {"byte_order", "little"}, {"event_bytes", 32},
                            {"event_alignment_known", false}, {"original_run_complete", false},
                            {"settings", live.settings.json()},
                            {"started_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
        recovering = true;
        live.status = device->read(R5560_SYNC_STATUS);
        live.errors = device->read(R5560_SYNC_ERRORS);
        recoveryMetadata["initial_status"] = double(live.status);
        recoveryMetadata["initial_errors"] = double(live.errors);
        recoveryMetadata["hardware_daq_id"] = double(device->read(R5560_DAQ_ID));
        recoveryMetadata["original_run_id"] = double(device->read(R5560_RUN_ID));
        recoveryMetadata["event_format"] = double(device->read(R5560_EVENT_FORMAT));
        if (device->isDma()) {
            updateDmaState();
            recoveryDmaBase = live.dmaAckWords;
            recoveryMetadata["transport"] = "dma-zmq";
            recoveryMetadata["initial_dma_ack_words"] = QString::number(recoveryDmaBase);
            device->openEventStream();
            // Never disable/reset the DMA engine during recovery: run=0 clears
            // the vendor mover's partial block and DDR buffer metadata.
            psDmaHandshake = checkPsDmaState(*device, "recovery readiness",
                PsDmaReady, PsDmaFault | PsDmaDirty, psDmaHandshake);
            device->writeInternal(PsDmaEnableRegister, 1);
            checkPsDmaState(*device, "recovery enable", PsDmaReady | PsDmaEnabled,
                            PsDmaQuiesced | PsDmaFault | PsDmaDirty, psDmaHandshake);
            dmaPrepared = true;
        }
        // STOP works in RUNNING/ARMED/DRAINING and does not clear the FIFO.
        // Stop even if opening the recovery file subsequently fails.
        device->write(R5560_LIST_BUS_CONFIG, 0);
        device->write(R5560_SYNC_COMMAND, R5560_CMD_STOP);
        live.status = device->read(R5560_SYNC_STATUS);
        if (!outputFile.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            throw Error("Cannot create recovery data file: " + outputFile.errorString());
        writeRecoveryMetadata("recovering");
        recoveryTimer.start();
        monitorTimer.invalidate();
        publish();
    }, true);
}
void ReadoutThread::cancelRecovery() {
    // Also covers cancellation before the queued recover operation has started.
    recoveryCancelled = true;
    wake.wakeAll();
}
void ReadoutThread::writeRecoveryMetadata(const QString &state, const QString &reason) {
    recoveryMetadata["status"] = state;
    recoveryMetadata["bytes_saved"] = QString::number(live.bytes);
    recoveryMetadata["last_status"] = double(live.status);
    recoveryMetadata["last_errors"] = double(live.errors);
    recoveryMetadata["fault_utc"] = live.faultUtc;
    recoveryMetadata["fault_stop_attempted"] = live.faultStopAttempted;
    recoveryMetadata["fault_stop_confirmed"] = live.faultStopConfirmed;
    if (state != "recovering")
        recoveryMetadata["finished_utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (!reason.isEmpty())
        recoveryMetadata["error"] = reason;
    writeObject(recoveryMetadataPath, recoveryMetadata);
}
void ReadoutThread::pollRecovery() {
    if (recoveryCancelled)
        throw Error("Recovery cancelled; saved bytes are preserved and FIFO was not cleared");
    // A previous transport failure may have consumed part of an event. Save raw
    // FIFO bytes separately; never decode or append them to an earlier run.
    const auto bytes = device->readEventData(16384);
    writeBytes(bytes);
    live.bytes += quint64(bytes.size());
    if (!monitorTimer.isValid() || monitorTimer.elapsed() >= 250) {
        monitorTimer.restart();
        live.status = device->read(R5560_SYNC_STATUS);
        live.errors = device->read(R5560_SYNC_ERRORS);
        live.startCount = device->read(R5560_START_COUNT);
        publish();
        if ((live.status & Idle) && eventDataDrained(live.bytes, recoveryDmaBase)) {
            if (!outputFile.flush())
                throw Error("Recovery file flush failed: " + outputFile.errorString());
            acknowledgeDma();
            outputFile.close();
            writeRecoveryMetadata("drained");
            recovering = false;
            lastBytes = live.bytes;
            rateTimer.restart();
            publish(); // Publish the acknowledged counters before the crate writes its manifest.
            emit recoveryCompleted(index);
            return;
        }
    }
    if (recoveryTimer.elapsed() > 15000)
        throw Error(device->isDma()
                        ? "DMA recovery could not account for every transmitted byte. Residual data "
                          "is preserved; data consumed by a previous process cannot be replayed. "
                          "The interrupted run remains incomplete; counters were not acknowledged."
                        : "Recovery STOP/drain timeout; saved bytes preserved, FIFO was not cleared");
    if (bytes.isEmpty()) {
        QMutexLocker lock(&queueMutex);
        if (queue.empty() && !quitting)
            wake.wait(&queueMutex, 5);
    }
}
void ReadoutThread::demoRun() {
    post([this] { device->demoRun(); });
}
void ReadoutThread::captureWaveform(const WaveformRequest &request) {
    request.validate();
    post([this, request] {
        if (wavePending) {
            emit waveformFailed(index, "A waveform capture is already pending");
            return;
        }
        if (device->read(R5560_WAVE_VERSION) != R5560_WAVE_API) {
            emit waveformFailed(index, "This FPGA firmware has no compatible waveform capture. "
                                       "Load the waveform firmware first.");
            return;
        }
        device->write(R5560_WAVE_COMMAND, R5560_WAVE_ABORT);
        device->writeChecked(R5560_WAVE_CHANNEL, quint32(request.channel));
        device->writeChecked(R5560_WAVE_LENGTH, quint32(request.length));
        device->writeChecked(R5560_WAVE_PRETRIGGER, quint32(request.pretrigger));
        waveExpectedSequence = device->read(R5560_WAVE_SEQUENCE) + 1;
        waveRequest = request;
        wave = Waveform();
        wave.module = index;
        wave.channel = request.channel;
        wave.pretrigger = request.pretrigger;
        wave.forced = request.force;
        wave.simulated = demo;
        wavePending = true;
        waveReading = false;
        waveTimer.start();
        wavePollTimer.invalidate();
        device->write(R5560_WAVE_COMMAND, R5560_WAVE_ARM);
        if (request.force)
            device->write(R5560_WAVE_COMMAND, R5560_WAVE_FORCE);
    });
}
void ReadoutThread::endWaveform(const QString &reason) {
    device->write(R5560_WAVE_COMMAND, R5560_WAVE_ABORT);
    device->read(R5560_WAVE_STATUS); // Complete the posted command before another request.
    wavePending = false;
    waveReading = false;
    emit waveformFailed(index, reason);
}
void ReadoutThread::cancelWaveform() {
    post([this] {
        if (wavePending)
            endWaveform("Waveform capture cancelled");
    });
}
void ReadoutThread::pollWaveform() {
    if (!wavePending)
        return;
    if (!waveReading) {
        if (wavePollTimer.isValid() && wavePollTimer.elapsed() < 20)
            return;
        wavePollTimer.restart();
        auto status = device->read(R5560_WAVE_STATUS);
        if (status & 8) {
            endWaveform("FPGA rejected the waveform request");
            return;
        }
        if (!(status & 1)) {
            if (waveTimer.elapsed() > waveRequest.timeoutMs)
                endWaveform("No channel trigger before timeout. Check the selected channel and "
                            "Basic threshold, or use Force for a baseline capture.");
            return;
        }
        wavePosition = device->read(R5560_WAVE_POSITION);
        wave.sequence = device->read(R5560_WAVE_SEQUENCE);
        if (wavePosition >= R5560_WAVE_CAPACITY || wave.sequence != waveExpectedSequence) {
            endWaveform("Waveform metadata changed or is invalid; capture discarded");
            return;
        }
        waveReading = true;
    }
    // One small memory burst per poll leaves STOP and list readout responsive.
    auto position = (wavePosition + quint32(wave.samples.size())) % R5560_WAVE_CAPACITY;
    int n = std::min(
        {1024, waveRequest.length - int(wave.samples.size()), int(R5560_WAVE_CAPACITY - position)});
    auto data = device->readBlock(R5560_WAVE_DATA_START + position, n);
    try {
        if (data.size() != n)
            throw Error("Short waveform memory read");
        wave.append(data);
    } catch (const std::exception &e) {
        endWaveform(QString::fromUtf8(e.what()));
        return;
    }
    if (wave.samples.size() == waveRequest.length) {
        if (!(device->read(R5560_WAVE_STATUS) & 1) ||
            device->read(R5560_WAVE_SEQUENCE) != wave.sequence) {
            endWaveform("Waveform changed during transfer; capture discarded");
            return;
        }
        wavePending = false;
        waveReading = false;
        // A channel trigger may win the race with the FORCE command. The stored
        // marker at the trigger position identifies that case honestly.
        wave.forced = waveRequest.force && !wave.triggers.value(wave.pretrigger);
        emit waveformReady(wave);
    }
}
void ReadoutThread::writeBytes(const QByteArray &bytes) {
    qint64 offset = 0;
    while (offset < bytes.size()) {
        qint64 n = outputFile.write(bytes.constData() + offset, bytes.size() - offset);
        if (n <= 0)
            throw Error("Disk write failed: " + outputFile.errorString());
        offset += n;
    }
}
void ReadoutThread::finish() {
    if (decoder && decoder->pendingBytes())
        throw Error("Truncated event at end of run; data file marked incomplete");
    if (outputFile.isOpen()) {
        MeasureTime measured(live.readout.writeNs, readoutTimer.isValid());
        if (!outputFile.flush())
            throw Error("Disk flush failed: " + outputFile.errorString());
    }
    live.status = device->read(R5560_SYNC_STATUS);
    live.startCount = device->read(R5560_START_COUNT);
    readCounters();
    if (device->isDma() && active) {
        if (!(live.status & Idle) || !eventDataDrained(live.bytes))
            throw Error("DMA completion attempted before all FPGA/DDR data was received");
        for (int ch = 0; ch < Channels; ++ch)
            if (quint32(live.received[ch]) != live.accepted[ch])
                throw Error(QString("DMA event count mismatch on Ch %1: received %2, accepted %3")
                                .arg(ch).arg(live.received[ch]).arg(live.accepted[ch]));
        acknowledgeDma();
    } else if (device->isDma() && dmaPrepared) {
        // Prepared but never ARMed: no new epoch or event data was generated.
        device->writeInternal(PsDmaEnableRegister, 0);
        checkPsDmaState(*device, "finish quiesce", PsDmaReady | PsDmaQuiesced,
                        PsDmaEnabled | PsDmaFault | PsDmaDirty, psDmaHandshake);
        device->closeEventStream();
        dmaPrepared = false;
    }
    // Keep the file context alive for raw fault attachments until final counter
    // checks and DMA acknowledgement have succeeded.
    outputFile.close();
    active = false;
    pending = false;
    stopping = false;
    reportReadout(true);
    publish();
    emit completed(index);
}
quint64 ReadoutThread::readDmaCounter(quint32 lowAddress) {
    // Counter and bus share the acquisition clock, but three bus reads are not atomic.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto hi = device->read(lowAddress + 1);
        const auto lo = device->read(lowAddress);
        if (device->read(lowAddress + 1) == hi)
            return (quint64(hi) << 32) | lo;
    }
    throw Error("DMA counter changed too quickly to read consistently");
}
void ReadoutThread::updateDmaState() {
    live.dma = device->isDma();
    if (!live.dma) return;
    live.dmaTxWords = readDmaCounter(R5560_DMA_TX_WORDS_LO);
    live.dmaAckWords = readDmaCounter(R5560_DMA_ACK_WORDS_LO);
    live.dmaPending = live.dmaTxWords != live.dmaAckWords ||
                      !(device->read(R5560_DMA_STATUS) & 1);
}
bool ReadoutThread::eventDataDrained(quint64 receivedBytes, quint64 baseWords) {
    if (!device->isDma()) return device->read(R5560_LIST_BUS_STATUS) & 1;
    const auto dmaStatus = device->read(R5560_DMA_STATUS);
    if (!(dmaStatus & 1)) return false;
    live.dmaTxWords = readDmaCounter(R5560_DMA_TX_WORDS_LO);
    if (live.dmaTxWords < baseWords)
        throw Error("DMA counter reset unexpectedly; cannot join different acquisition epochs");
    const auto words = live.dmaTxWords - baseWords;
    if (words > std::numeric_limits<quint64>::max() / 8)
        throw Error("DMA byte counter overflow");
    const auto expected = words * 8;
    live.dmaPending = expected != receivedBytes;
    if (receivedBytes > expected)
        throw Error(QString("DMA byte-count mismatch: received_bytes=%1, expected_bytes=%2, "
                            "excess_bytes=%3, tx_words64=%4, base_words64=%5, dma_status=0x%6; "
                            "cause unconfirmed: check FPGA counter/reset and DMA message "
                            "length, padding or duplicate data")
                        .arg(receivedBytes).arg(expected).arg(receivedBytes - expected)
                        .arg(live.dmaTxWords).arg(baseWords)
                        .arg(dmaStatus, 8, 16, QChar('0')));
    if (receivedBytes != expected) return false;
    // A complete network message can exceed one local processing slice. Never
    // discard its unread suffix just because this slice matches the FPGA count.
    device->verifyEventStreamEmpty();
    return true;
}
void ReadoutThread::acknowledgeDma() {
    if (!device->isDma()) return;
    // Only commit after all bytes have been checked and any output file flushed.
    device->writeChecked(R5560_DMA_ACK_WORDS_LO, quint32(live.dmaTxWords));
    device->writeChecked(R5560_DMA_ACK_WORDS_HI, quint32(live.dmaTxWords >> 32));
    device->writeInternal(PsDmaEnableRegister, 0);
    checkPsDmaState(*device, "acknowledge quiesce", PsDmaReady | PsDmaQuiesced,
                    PsDmaEnabled | PsDmaFault | PsDmaDirty, psDmaHandshake);
    device->closeEventStream();
    dmaPrepared = false;
    live.dmaAckWords = live.dmaTxWords;
    live.dmaPending = false;
}
void ReadoutThread::readCounters() {
    // Full refresh is used only while idle or after the event FIFO is drained.
    counterStep = 0;
    do { readCounterStep(); } while (counterStep >= 0);
}
void ReadoutThread::probeDiagnostics() {
    // Unknown registers in older firmware return 0/DEADBEEF. Never interpret
    // that as a valid zero measurement, or read the extension arrays blindly.
    live.diagnosticsVersion = device->read(R5560_DIAG_VERSION);
    live.diagnosticsValid = false;
    live.diagnosticsReadMs = 0;
    if (live.diagnosticsSupported())
        live.diagnosticsSequence = device->read(R5560_DIAG_SEQUENCE);
}
void ReadoutThread::readCounterStep() {
    MeasureTime measured(live.readout.countersNs, readoutTimer.isValid());
    switch (counterStep) {
    case 0:
        counterT0Before = device->read(R5560_T0_COUNT);
        break;
    case 1:
        device->write(R5560_SYNC_COMMAND, R5560_CMD_SNAPSHOT);
        break;
    case 2: {
        const auto after = device->readBlock(R5560_T0_COUNT, 3);
        // The count must bracket SNAPSHOT, not just the two timestamp reads.
        if (counterT0Before == after[0]) {
            live.t0Count = after[0];
            live.t0Ticks = (quint64(after[2] & 0xffff) << 32) | after[1];
        }
        break;
    }
    case 3: case 4: case 5: case 6: {
        static const quint32 addresses[] = {R5560_TRIGGERIN, R5560_TRIGGEROUT,
                                            R5560_ACCEPTED, R5560_REJECTED};
        pendingCounters[size_t(counterStep - 3)] = device->readBlock(addresses[counterStep - 3], Channels);
        break;
    }
    case 7:
        pendingDiagnosticHeader = device->readBlock(R5560_DIAG_SEQUENCE,
                                                    R5560_DIAG_START_COUNT - R5560_DIAG_SEQUENCE + 1);
        break;
    case 8: case 9: case 10: {
        static const quint32 addresses[] = {R5560_REJECTED_PROCESSING, R5560_REJECTED_FIFO,
                                            R5560_CHANNEL_FIFO_PEAK};
        pendingDiagnostics[size_t(counterStep - 8)] = device->readBlock(addresses[counterStep - 8], Channels);
        break;
    }
    case 11: {
        const auto sequence = device->read(R5560_DIAG_SEQUENCE);
        const auto &h = pendingDiagnosticHeader;
        // The FPGA latched every diagnostic on CMD_SNAPSHOT. A second client
        // taking a snapshot, or an epoch change, must not publish a torn set.
        if (sequence == h[0] && sequence != live.diagnosticsSequence && h[6] == live.startCount) {
            live.diagnosticsSequence = sequence;
            live.channelFifoDepth = h[1];
            live.dmaFifoDepth = h[2];
            live.dmaFifoPeak = h[3];
            live.dmaStallMaxCycles = (quint64(h[5]) << 32) | h[4];
            live.diagnosticsStartCount = h[6];
            for (int ch = 0; ch < Channels; ++ch) {
                live.rejectedProcessing[ch] = pendingDiagnostics[0][ch];
                live.rejectedFifo[ch] = pendingDiagnostics[1][ch];
                live.channelFifoPeak[ch] = pendingDiagnostics[2][ch];
            }
            live.diagnosticsValid = true;
            live.diagnosticsReadMs = QDateTime::currentMSecsSinceEpoch();
        }
        break;
    }
    }
    if (++counterStep == (live.diagnosticsSupported() ? 12 : 7)) {
        // Publish a complete counter set, never a half-refreshed group.
        for (int ch = 0; ch < Channels; ++ch) {
            live.input[ch] = pendingCounters[0][ch];
            live.output[ch] = pendingCounters[1][ch];
            live.accepted[ch] = pendingCounters[2][ch];
            live.rejected[ch] = pendingCounters[3][ch];
            // Both reasons share one hardware snapshot and are mutually exclusive.
            // Keep the displayed total from the same sample (u32 wrap preserved).
            if (live.diagnosticsAvailable())
                live.rejected[ch] = live.rejectedProcessing[ch] + live.rejectedFifo[ch];
        }
        if (readoutTimer.isValid()) ++live.readout.counterCycles;
        counterStep = -1;
        counterTimer.restart();
    }
}
void ReadoutThread::readRunStatus() {
    // Reads in this range have no side effects; in particular it excludes
    // SYNC_COMMAND and the consuming FIFO data endpoint. This is not atomic.
    MeasureTime measured(live.readout.monitorNs, readoutTimer.isValid());
    const auto values = device->readBlock(R5560_SYNC_STATUS,
                                          R5560_START_COUNT - R5560_SYNC_STATUS + 1);
    live.status = values[0];
    live.errors = values[R5560_SYNC_ERRORS - R5560_SYNC_STATUS];
    live.startCount = values[R5560_START_COUNT - R5560_SYNC_STATUS];
    if (live.diagnosticsValid && live.diagnosticsStartCount != live.startCount)
        live.diagnosticsValid = false;
    live.runStatusReadMs = QDateTime::currentMSecsSinceEpoch();
}
QByteArray ReadoutThread::readEventFifo() {
    MeasureTime measured(live.readout.fifoNs);
    auto bytes = device->readEventData(fifoWords);
    ++live.readout.fifoCalls;
    live.readout.bytes += quint64(bytes.size());
    if (bytes.isEmpty()) ++live.readout.emptyReads;
    else if (bytes.size() < fifoWords * 4) ++live.readout.shortReads;
    return bytes;
}
void ReadoutThread::reportReadout(bool final) {
    if (!readoutTimer.isValid()) return;
    live.readout.elapsedNs = quint64(readoutTimer.nsecsElapsed());
    const auto &m = live.readout;
    const double elapsed = double(m.elapsedNs - lastReport.elapsedNs);
    const auto calls = m.fifoCalls - lastReport.fifoCalls;
    if (elapsed > 0 && (final || reportTimer.elapsed() >= 10000)) {
        auto percent = [&](quint64 value, quint64 previous) { return 100.0 * (value - previous) / elapsed; };
        emit connectionProgress(index, QString("Readout %1: %2 MB/s, %3 calls/s, avg %4 KiB/call, "
            "empty %5, short %6 | time: receive %7%, disk %8%, decode %9%, status %10%, "
            "counters %11%, waveform %12%, idle %13% | interval %14 s | block %15 KiB | %16")
            .arg(final ? "final interval" : "interval")
            .arg((m.bytes - lastReport.bytes) * 1000.0 / elapsed, 0, 'f', 2)
            .arg(calls * 1e9 / elapsed, 0, 'f', 1)
            .arg(calls ? double(m.bytes - lastReport.bytes) / calls / 1024 : 0, 0, 'f', 1)
            .arg(m.emptyReads - lastReport.emptyReads).arg(m.shortReads - lastReport.shortReads)
            .arg(percent(m.fifoNs, lastReport.fifoNs), 0, 'f', 1)
            .arg(percent(m.writeNs, lastReport.writeNs), 0, 'f', 1)
            .arg(percent(m.decodeNs, lastReport.decodeNs), 0, 'f', 1)
            .arg(percent(m.monitorNs, lastReport.monitorNs), 0, 'f', 1)
            .arg(percent(m.countersNs, lastReport.countersNs), 0, 'f', 1)
            .arg(percent(m.waveformNs, lastReport.waveformNs), 0, 'f', 1)
            .arg(percent(m.idleNs, lastReport.idleNs), 0, 'f', 1)
            .arg(elapsed / 1e9, 0, 'f', 2).arg(m.requestBytes / 1024)
            .arg(live.dma ? "DMA local slice" : "Avalon FIFO request"));
        lastReport = m;
        reportTimer.restart();
    }
    if (final) readoutTimer.invalidate();
}
void ReadoutThread::poll() {
    if (recovering) {
        pollRecovery();
        return;
    }
    bool receivedData = false;
    if (active) {
        const auto bytes = readEventFifo();
        receivedData = !bytes.isEmpty();
        if (receivedData) dmaStallTimer.invalidate();
        if (outputFile.isOpen()) {
            MeasureTime measured(live.readout.writeNs);
            writeBytes(bytes);
        }
        live.bytes += quint64(bytes.size());
        MeasureTime measured(live.readout.decodeNs);
        auto events = decoder->feed(bytes);
        live.events += events.size();
        for (const auto &event : events) {
            const auto ch = event.channel;
            ++live.received[ch];
            if (event.energy < 0)
                ++live.underflow[ch];
            else if (event.energy >= 16384)
                ++live.overflow[ch];
            else
                ++live.histogram[ch][size_t(event.energy / 32)];
        }
    }
    if (!monitorTimer.isValid() || monitorTimer.elapsed() >= 250) {
        monitorTimer.restart();
        readRunStatus();
        if (active && (!(live.status & ClockGood) || live.errors))
            throw Error(QString("Acquisition fault: status=0x%1 errors=0x%2")
                            .arg(live.status, 0, 16)
                            .arg(live.errors, 0, 16));
        if (active && quint32(live.startCount - startBase) > 1)
            throw Error(
                "A second RUN edge started a new timestamp epoch; stop and start a new run");
        if (active && !readySent) {
            if (live.status & Armed) {
                readySent = true;
                publish();
                emit armed(index);
            } else if (live.status & Running) {
                if (live.settings.syncConfig & 1)
                    throw Error("External RUN rose before this module reported ARMED");
                readySent = true;
                publish();
                emit armed(index);
            } else if (armTimer.elapsed() > 3000 && !stopping)
                throw Error("ARM timeout: check RUN low, clock and FIFO readiness");
        }
        if (live.status & Running)
            seenRunning = true;
        if (active && device->isDma() && !stopping && dmaHealthTimer.elapsed() >= 1000) {
            dmaHealthTimer.restart();
            live.dmaTxWords = readDmaCounter(R5560_DMA_TX_WORDS_LO);
            const bool outstanding = live.dmaTxWords > live.bytes / 8 ||
                                     !(device->read(R5560_DMA_STATUS) & 1);
            if (!outstanding) dmaStallTimer.invalidate();
            else if (!dmaStallTimer.isValid()) dmaStallTimer.start();
            else if (dmaStallTimer.elapsed() >= 10000)
                throw TransportError("DMA stream made no progress for 10 s with pending FPGA/DDR "
                                     "data; acquisition cannot continue on an uncertain stream");
        }
        if (active && ((live.status & Draining) || (seenRunning && (live.status & Idle))) &&
            !stopping) {
            stopping = true;
            drainTimer.restart();
        }
        if (rateTimer.isValid() && rateTimer.elapsed() >= 1000) {
            const double sec = rateTimer.restart() / 1000.0;
            live.megabytesPerSecond = (live.bytes - lastBytes) / sec / 1e6;
            lastBytes = live.bytes;
            if (!active && !pending)
                readCounters();
        }
        reportReadout();
        publish();
        if (active && stopping && (live.status & Idle) && eventDataDrained(live.bytes))
            finish();
        if (active && stopping && drainTimer.elapsed() > 15000)
            throw Error(device->isDma()
                            ? QString("DMA drain timeout: FPGA transmitted %1 bytes, host received %2; "
                                      "DDR tail is not confirmed, run remains incomplete")
                                  .arg(live.dmaTxWords * 8).arg(live.bytes)
                            : QString("STOP drain timeout; preserve the incomplete run and check hardware"));
    }
    // At most one statistics transaction per FIFO iteration. Command handling
    // and the 250 ms safety poll always get their turn, even under full load.
    if (active && !stopping && (live.status & Running)) {
        if (counterStep < 0 && counterTimer.elapsed() >= 1000) counterStep = 0;
        if (counterStep >= 0) readCounterStep();
    }
    {
        MeasureTime measured(live.readout.waveformNs, readoutTimer.isValid());
        pollWaveform();
    }
    if (!receivedData) {
        QMutexLocker lock(&queueMutex);
        if (queue.empty() && !quitting) {
            MeasureTime measured(live.readout.idleNs, readoutTimer.isValid());
            if (readoutTimer.isValid()) ++live.readout.idleWaits;
            wake.wait(&queueMutex, active || waveReading ? 2 : wavePending ? 20 : 40);
        }
    }
}
bool ReadoutThread::initializeDevice() {
    constexpr int attempts = 3;
    for (int attempt = 1; attempt <= attempts && !quitting; ++attempt) {
        try {
            if (demo)
                device = std::make_unique<DemoDevice>(index);
            else {
                emit connectionProgress(index, QString("Connecting to %1:%2 (attempt %3/%4; "
                                                       "initialization timeout %5 ms)")
                    .arg(endpoint.ip).arg(endpoint.port).arg(attempt).arg(attempts)
                    .arg(TcpDevice::InitializationTimeoutMs));
                device = std::make_unique<TcpDevice>(endpoint, TcpDevice::InitializationTimeoutMs);
            }
            // Re-read everything on a fresh connection. No writes or FIFO reads
            // are allowed in this retryable handshake.
            if (auto *tcp = dynamic_cast<TcpDevice *>(device.get()))
                tcp->setDiagnosticHandler([this](const QString &message) {
                    emit connectionProgress(index, message);
                });
            live = Snapshot();
            live.api = device->read(R5560_API_VERSION);
            live.clockHz = device->read(R5560_CLOCK_HZ);
            probeDiagnostics();
            if (!supportedApi(live.api) || live.clockHz != ClockHz)
                throw Error("Firmware API / clock mismatch: requires API 0x00020000 (Avalon) "
                            "or 0x00030000 (DMA), 125 MHz");
            device->selectEventTransport(live.api);
            if (device->isDma() && device->read(R5560_DMA_VERSION) != DmaInterfaceVersion)
                throw Error("Unsupported DMA interface version");
            // Old Avalon banks return DEADBEEF for this optional, read-only ID.
            // Keep those DMA firmwares compatible and record their identity as unknown.
            live.dma = device->isDma();
            if (live.dma)
                live.dmaBuildId = device->read(R5560_DMA_BUILD_ID);
            live.settings = device->readSettings();
            live.format = device->read(R5560_EVENT_FORMAT);
            live.status = device->read(R5560_SYNC_STATUS);
            live.errors = device->read(R5560_SYNC_ERRORS);
            updateDmaState();
            // A live non-IDLE board must remain reachable for explicit STOP and
            // raw FIFO recovery. Configuration/Start retain their IDLE guards.
            if (auto *tcp = dynamic_cast<TcpDevice *>(device.get()))
                tcp->setTimeout(TcpDevice::OperationTimeoutMs);
            return !quitting;
        } catch (const TransportError &e) {
            device.reset();
            if (quitting)
                return false;
            if (attempt == attempts)
                throw Error(QString("Connection initialization failed after %1 attempts: %2. "
                                    "Use File > Disconnect, then Connect to device to retry.")
                                .arg(attempts).arg(QString::fromUtf8(e.what())));
            const int delayMs = 500 * attempt;
            emit connectionProgress(index,
                QString("Initialization attempt %1/%2 failed: %3. Retrying with a new "
                        "connection in %4 ms")
                    .arg(attempt).arg(attempts).arg(QString::fromUtf8(e.what())).arg(delayMs));
            QMutexLocker lock(&queueMutex);
            if (!quitting)
                wake.wait(&queueMutex, delayMs);
        }
    }
    return false;
}
QString ReadoutThread::savePartialReply(const TransportError &error, const QString &suffix) {
    if (error.partialReply.isEmpty())
        return "";
    if (!outputFile.isOpen())
        return QString("; %1 partial reply bytes were received; no output file was enabled "
                       "to preserve them").arg(error.partialReply.size());
    // These bytes may include a FIFO count header and an incomplete event. They
    // must not be appended to the event file or replayed through the decoder.
    const auto path = outputFile.fileName() + suffix;
    QFile partial(path);
    if (!partial.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        throw Error("Cannot preserve partial reply: " + partial.errorString());
    qint64 offset = 0;
    while (offset < error.partialReply.size()) {
        auto count = partial.write(error.partialReply.constData() + offset,
                                   error.partialReply.size() - offset);
        if (count <= 0)
            throw Error("Cannot write partial reply: " + partial.errorString());
        offset += count;
    }
    if (!partial.flush())
        throw Error("Cannot flush partial reply: " + partial.errorString());
    partial.close();
    writeObject(path + ".json", {{"schema", "r5560-partial-reply-v1"},
                                {"module_index", index}, {"ip", endpoint.ip},
                                {"bytes", error.partialReply.size()}, {"fault_utc", live.faultUtc},
                                {"wire_reply_including_protocol_header", !error.dmaPayload},
                                {"transport", error.dmaPayload ? "dma-zmq" : "avalon-tcp"},
                                {"data_kind", error.dmaPayload ? "event_stream" : "tcp_wire_reply"},
                                {"event_alignment_known", false},
                                {"error", QString::fromUtf8(error.what())}});
    return "; partial wire reply saved separately: " + path;
}
QString ReadoutThread::stopAfterFailure(bool freshConnection) {
    // Bound the entire STOP recovery, not just each individual register read.
    // At most two attempts; a fresh connection never reads FIFO or writes ARM,
    // CLEAR, settings, or run IDs, and never resumes the interrupted run.
    constexpr int totalBudgetMs = 5000, requestLimitMs = 1500;
    QElapsedTimer budget;
    budget.start();
    live.faultStopAttempted = true;
    QStringList failures;
    for (int attempt = 1; attempt <= 2; ++attempt) {
        auto limit = [&] {
            const auto remaining = qint64(totalBudgetMs) - budget.elapsed();
            if (remaining <= 0)
                throw Error("STOP recovery total deadline exceeded");
            return int(std::min<qint64>(requestLimitMs, remaining));
        };
        auto bounded = [&] {
            const int timeout = limit();
            if (auto *tcp = dynamic_cast<TcpDevice *>(device.get()))
                tcp->setTimeout(timeout);
        };
        try {
            if (freshConnection && !demo) {
                device.reset();
                emit connectionProgress(index,
                    QString("Fault STOP: new control connection, attempt %1/2; total budget "
                            "5000 ms, no FIFO replay | worker_utc=%2")
                        .arg(attempt).arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
                device = std::make_unique<TcpDevice>(endpoint, limit());
                bounded();
                if (!supportedApi(device->read(R5560_API_VERSION)))
                    throw Error("STOP recovery refused: firmware API mismatch");
                bounded();
                if (device->read(R5560_CLOCK_HZ) != ClockHz)
                    throw Error("STOP recovery refused: firmware clock mismatch");
            }
            if (!device)
                throw Error("No control connection for STOP");
            if (baseboardRunController) {
                bounded();
                emit connectionProgress(index, "Fault STOP: lowering baseboard sync_reg.1 "
                                                "through internal address 0x00140001");
                device->writeInternal(BaseboardRunRegister, 0);
                live.baseboardRunLowConfirmed = false;
            }
            bounded();
            device->write(R5560_LIST_BUS_CONFIG, 0);
            bounded();
            device->write(R5560_SYNC_COMMAND, R5560_CMD_STOP);
            while (true) {
                bounded();
                live.status = device->read(R5560_SYNC_STATUS);
                if (baseboardRunController)
                    live.baseboardRunLowConfirmed = !(live.status & RunInputHigh);
                if ((live.status & (Idle | Draining)) &&
                    !(live.status & (Running | Armed | Preparing | (1u << 11))) &&
                    (!baseboardRunController || live.baseboardRunLowConfirmed)) {
                    live.faultStopConfirmed = true;
                    return QString("; STOP confirmed: %1, status=0x%2; FIFO was not cleared "
                                   "or read on the control connection | worker_utc=%3")
                        .arg(statusText(live.status)).arg(live.status, 8, 16, QChar('0'))
                        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                }
                QMutexLocker lock(&queueMutex);
                wake.wait(&queueMutex, 20);
            }
        } catch (const std::exception &e) {
            failures.append(QString("attempt %1: %2").arg(attempt).arg(QString::fromUtf8(e.what())));
        }
        freshConnection = true;
        if (budget.elapsed() >= totalBudgetMs || quitting)
            break;
        if (attempt < 2) {
            QMutexLocker lock(&queueMutex);
            wake.wait(&queueMutex, std::max(1, std::min(250, totalBudgetMs - int(budget.elapsed()))));
        }
    }
    return "; STOP NOT confirmed: " + failures.join("; ") +
           ". Device may still be acquiring. Lower shared RUN in external mode; "
           "reconnect and use Stop / Recover. Software mode requires a reachable control "
           "connection to stop.";
}
void ReadoutThread::run() {
    try {
        if (!initializeDevice()) {
            device.reset();
            return;
        }
        live.connected = true;
        publish();
        emit connected(index);
        rateTimer.start();
        while (!quitting) {
            std::function<void()> command;
            {
                QMutexLocker lock(&queueMutex);
                if (!queue.empty()) {
                    command = std::move(queue.front());
                    queue.pop_front();
                }
            }
            if (command)
                command();
            poll();
        }
        if (wavePending)
            endWaveform("Waveform capture cancelled on disconnect");
        if (pending || recovering || controllingBaseboardRun) {
            if (baseboardRunController)
                device->writeInternal(BaseboardRunRegister, 0);
            device->write(R5560_SYNC_COMMAND, R5560_CMD_STOP);
            throw Error("Shutdown interrupted acquisition/recovery; data is incomplete");
        }
    } catch (const std::exception &e) {
        reportReadout(true);
        QString failure = QString::fromUtf8(e.what());
        live.faultUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        failure += " | fault_worker_utc=" + live.faultUtc;
        const auto *transport = dynamic_cast<const TransportError *>(&e);
        // A control-channel error or decoder/hardware fault can occur after ZMQ
        // received a full message but before all local slices were processed.
        // Detach that suffix before STOP recovery replaces/destroys the device.
        QByteArray localDmaTail;
        if (device) {
            try { localDmaTail = device->takePendingEventData(); }
            catch (const std::exception &tailError) {
                failure += "; cannot retain local DMA tail: " + QString::fromUtf8(tailError.what());
            }
        }
        if (pending || recovering || controllingBaseboardRun) {
            // Notify peers before spending time re-establishing this connection.
            emit faultDetected(index, failure);
            failure += stopAfterFailure(transport != nullptr);
        }
        if (transport) {
            try {
                failure += savePartialReply(*transport);
            } catch (const std::exception &saveError) {
                failure += "; " + QString::fromUtf8(saveError.what());
            }
        }
        if (!localDmaTail.isEmpty()) {
            try {
                failure += savePartialReply(TransportError(
                    "Unconsumed local DMA message suffix after: " + QString::fromUtf8(e.what()),
                    localDmaTail, true), ".dma-pending.raw");
            } catch (const std::exception &saveError) {
                failure += "; " + QString::fromUtf8(saveError.what());
            }
        }
        if (recovering) {
            if (outputFile.isOpen() && !outputFile.flush())
                failure += "; recovery file flush failed: " + outputFile.errorString();
            outputFile.close();
            try {
                writeRecoveryMetadata("incomplete", failure);
            } catch (const std::exception &metadataError) {
                failure += "; recovery metadata could not be saved: " +
                           QString::fromUtf8(metadataError.what());
            }
        } else if (outputFile.isOpen() && !outputFile.flush()) {
            failure += "; incomplete run file flush failed: " + outputFile.errorString();
        }
        live.connected = false;
        publish();
        if (!quitting || pending || recovering)
            emit failed(index, failure);
    }
    outputFile.close();
    device.reset();
}
} // namespace daq
