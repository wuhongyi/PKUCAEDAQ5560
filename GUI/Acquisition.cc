#include "Acquisition.hh"

namespace daq {
Acquisition::Acquisition(QObject *parent) : QObject(parent) {
    qRegisterMetaType<daq::Waveform>("daq::Waveform");
    connect(&monitor, &QTimer::timeout, this, &Acquisition::checkState);
    monitor.start(250);
}
Acquisition::~Acquisition() {
    for (auto *w : workers)
        w->shutdown();
    for (auto *w : workers) {
        w->wait();
        delete w;
    }
}
void Acquisition::setState(const QString &s) {
    phase = s;
    emit stateChanged(s);
}
QVector<ModuleSettings> Acquisition::settings() const {
    QVector<ModuleSettings> s;
    for (auto *w : workers)
        s.append(w->snapshot().settings);
    return s;
}
void Acquisition::connectCrate(const Config &c, bool simulated) {
    if (phase != "Disconnected")
        throw Error("Disconnect the current crate first");
    if (c.endpoints.isEmpty())
        throw Error("No modules in configuration");
    crate = c;
    demo = simulated;
    controllerFault = false;
    runControlRequests.clear();
    waiting.clear();
    manifestPath.clear();
    setState("Connecting");
    for (int i = 0; i < c.endpoints.size(); ++i) {
        auto *w = new ReadoutThread(i, c.endpoints[i], demo, this);
        if (i == 0 && c.endpoints.size() > 1) {
            w->enableBaseboardRunControl();
            connect(w, &ReadoutThread::baseboardRunWritten,
                    this, &Acquisition::onBaseboardRunWritten);
        }
        workers.append(w);
        waiting.insert(i);
        connect(w, &ReadoutThread::connectionProgress, this, [this](int n, const QString &text) {
            emit logMessage(QString("Mod %1: %2").arg(n).arg(text));
        });
        connect(w, &ReadoutThread::connected, this, [this](int n) {
            waiting.remove(n);
            const auto s = snapshot(n);
            emit logMessage(QString("Mod %1 connected: 32 channels, %2, "
                                    "status=0x%3 errors=0x%4; API 0x%5, transport %6%7")
                                .arg(n).arg(statusText(s.status))
                                .arg(s.status, 8, 16, QChar('0')).arg(s.errors, 8, 16, QChar('0'))
                                 .arg(s.api, 8, 16, QChar('0')).arg(s.dma ? "DMA/ZMQ" : "Avalon FIFO")
                                 .arg(s.dma ? "; DMA build " + s.dmaBuildDescription() : QString()));
            if (waiting.isEmpty() && phase == "Connecting") {
                bool allIdle = true;
                for (auto *v : workers)
                    allIdle &= bool(v->snapshot().status & Idle) && !v->snapshot().dmaPending;
                setState(allIdle ? "Idle" : "RecoveryRequired");
                emit settingsChanged();
                if (!allIdle)
                    emit logMessage("Device is not IDLE or has unacknowledged DMA data. Stop / Recover saves residual data "
                                    "in the Output folder before returning to IDLE. No FIFO clear "
                                    "or parameter writes are performed on connection.");
            }
        });
        connect(w, &ReadoutThread::configured, this, [this](int n) {
            waiting.remove(n);
            if (waiting.isEmpty() && phase == "Configuring") {
                setState("Idle");
                if (configurationModule >= 0)
                    emit moduleSettingsChanged(configurationModule);
                else
                    emit settingsChanged();
                emit logMessage(configurationResult);
            }
        });
        connect(w, &ReadoutThread::dmaResetReady, this, [this](int n) {
            if (phase != "CheckingReset") return;
            waiting.remove(n);
            if (waiting.isEmpty()) {
                configurationModule = -1;
                configurationResult = "DMA epoch reset by explicit user request; unread data "
                                      "discarded. Earlier runs remain incomplete. Inspect/clear "
                                      "hardware error flags before starting again.";
                for (int i = 0; i < workers.size(); ++i) waiting.insert(i);
                setState("Configuring");
                for (auto *v : workers) v->resetDmaEpoch();
            }
        });
        connect(w, &ReadoutThread::prepared, this, [this](int n) {
            waiting.remove(n);
            if (waiting.isEmpty() && phase == "Preparing") {
                waiting = members;
                setState("Arming");
                if (phase != "Arming")
                    return;
                for (auto *v : workers)
                    v->arm();
            }
        });
        connect(w, &ReadoutThread::armed, this, [this](int n) {
            waiting.remove(n);
            if (waiting.isEmpty() && phase == "Arming") {
                setState("Armed");
                if (phase != "Armed")
                    return;
                if (controlsBaseboardRun()) {
                    recordRunDiagnostics("all-ARMED-before-RUN");
                    emit logMessage("All modules ARMED. Raising baseboard RUN through Mod 0 "
                                    "(sync_reg.1, internal address 0x00140001).");
                    requestBaseboardRun(true, RunControlAction::Start);
                } else if (settings()[0].syncConfig & 1)
                    emit logMessage(demo
                                        ? "Module ARMED. Use Simulate RUN in Logic Settings."
                                        : "Module ARMED. Apply the external RUN edge now.");
            }
        });
        connect(w, &ReadoutThread::completed, this, &Acquisition::onCompleted);
        connect(w, &ReadoutThread::recoveryCompleted, this, &Acquisition::onCompleted);
        connect(w, &ReadoutThread::faultDetected, this, [this](int n, const QString &message) {
            if (n == 0)
                controllerFault = true;
            runFailed = true;
            setState("Fault");
            const auto text = QString("Mod %1: %2; attempting bounded STOP recovery")
                                  .arg(n).arg(message);
            emit logMessage(text);
            emit fault(text);
            // Keep the failed worker in members until its STOP attempt and file
            // cleanup finish. Other DAQs can stop/drain immediately.
            if (session && !recoverySession)
                stopRun();
            else if (session && recoverySession && !recoveryDispatched)
                recoverWorkers();
        });
        connect(w, &ReadoutThread::failed, this, &Acquisition::onError);
        connect(w, &ReadoutThread::waveformReady, this, [this](const Waveform &wave) {
            if (wave.module != waveModule)
                return;
            waveModule = -1;
            emit waveformBusyChanged(false);
            emit waveformReady(wave);
        });
        connect(w, &ReadoutThread::waveformFailed, this, [this](int index, const QString &message) {
            if (index != waveModule)
                return;
            waveModule = -1;
            emit waveformBusyChanged(false);
            emit waveformFailed(message);
        });
    }
    for (auto *w : workers)
        w->start();
}
void Acquisition::disconnectCrate() {
    if (session || phase == "Preparing" || phase == "Arming")
        throw Error("Stop and drain acquisition before disconnecting");
    if (waveModule >= 0) {
        waveModule = -1;
        emit waveformBusyChanged(false);
        emit waveformFailed("Waveform capture cancelled on disconnect");
    }
    for (auto *w : workers) {
        QObject::disconnect(w, nullptr, this, nullptr);
        w->shutdown();
    }
    for (auto *w : workers) {
        w->wait();
        delete w;
    }
    workers.clear();
    waiting.clear();
    runControlRequests.clear();
    setState("Disconnected");
    emit settingsChanged();
}
void Acquisition::apply(const QVector<ModuleSettings> &s) {
    if (waveformBusy())
        throw Error("Stop waveform capture before changing channel parameters");
    if (!idle() || s.size() != workers.size())
        throw Error("Settings require an idle, fully connected crate");
    try {
        validateSettings(s);
    } catch (const Error &e) {
        throw Error(QString::fromUtf8(e.what()) +
                    "\nNo registers were written. Write All checks every channel of every module.");
    }
    configurationModule = -1;
    configurationResult = "All modules: settings written and readback verified";
    waiting.clear();
    setState("Configuring");
    for (int i = 0; i < workers.size(); ++i) {
        waiting.insert(i);
        workers[i]->configure(s[i]);
    }
}
void Acquisition::applyModule(int module, const ModuleSettings &s) {
    if (waveformBusy())
        throw Error("Stop waveform capture before changing channel parameters");
    if (!idle() || module < 0 || module >= workers.size())
        throw Error("Select a connected module while IDLE");
    validateModuleSettings(s, module);
    configurationModule = module;
    configurationResult = QString("Mod %1: settings written and readback verified").arg(module);
    waiting = {module};
    setState("Configuring");
    workers[module]->configure(s);
}
void Acquisition::refresh() {
    if (!idle())
        throw Error("Settings require IDLE");
    configurationModule = -1;
    configurationResult = "All modules: settings read from device";
    waiting.clear();
    setState("Configuring");
    for (int i = 0; i < workers.size(); ++i) {
        waiting.insert(i);
        workers[i]->refreshSettings();
    }
}
void Acquisition::refreshModule(int module) {
    if (!idle() || module < 0 || module >= workers.size())
        throw Error("Select a connected module while IDLE");
    configurationModule = module;
    configurationResult = QString("Mod %1: settings read from device").arg(module);
    waiting = {module};
    setState("Configuring");
    workers[module]->refreshSettings();
}
void Acquisition::clearErrors() {
    if (!idle())
        throw Error("Error reset requires IDLE");
    configurationModule = -1;
    configurationResult = "Hardware error flags cleared";
    waiting.clear();
    setState("Configuring");
    for (int i = 0; i < workers.size(); ++i) {
        waiting.insert(i);
        workers[i]->clearErrors();
    }
}
void Acquisition::setReadoutBlockKiB(int kib) {
    if (kib != 64 && kib != 128 && kib != 256)
        throw Error("FIFO request must be 64, 128 or 256 KiB");
    if (session || (phase != "Idle" && phase != "Disconnected"))
        throw Error("Change FIFO request size only while IDLE or disconnected");
    readoutBlockKiB = kib;
}
void Acquisition::resetDmaEpoch() {
    if (session || phase != "RecoveryRequired" || waveformBusy())
        throw Error("DMA epoch reset requires RecoveryRequired with no host operation in progress");
    bool anyDma = false;
    for (auto *w : workers) {
        const auto s = w->snapshot();
        anyDma |= s.dma;
        if (!s.connected || !w->isRunning() || (s.status & (Running | RunInputHigh | Preparing)))
            throw Error("Reconnect and STOP every device; physical RUN must be LOW before DMA reset");
    }
    if (!anyDma) throw Error("No DMA module is connected");
    waiting.clear();
    for (int i = 0; i < workers.size(); ++i) waiting.insert(i);
    setState("CheckingReset");
    // Complete read-only checks on the whole crate before any destructive write.
    for (auto *w : workers) w->resetDmaEpoch(true);
}
void Acquisition::startRun(quint32 run, const QString &folder, const QString &prefix, bool save) {
    if (waveformBusy())
        throw Error("Stop waveform capture before starting a new acquisition");
    if (!idle())
        throw Error("All devices must be connected and IDLE");
    auto s = settings();
    validateSettings(s);
    for (int i = 0; i < s.size(); ++i) {
        if (s.size() > 1 && !(s[i].syncConfig & 1))
            throw Error("Multiple modules require shared external RUN mode. Configure Logic "
                        "Settings first.");
        if (s.size() > 1 && ((s[i].syncConfig >> 1) & 3) != 1)
            throw Error(QString("Mod %1: baseboard sync_reg.1 drives SYNC 1. Select RUN input "
                                "SYNC 1 in Logic Settings before Start; disable T0 or select "
                                "a separately wired T0 input.").arg(i));
        if ((s[i].syncConfig & 0x7f) != (s[0].syncConfig & 0x7f))
            throw Error("All modules must use the same RUN/T0 configuration");
        if (!(s[i].syncConfig & 1) && !(s[i].syncConfig & 64))
            throw Error("Enable trigger timestamps in Logic Settings");
    }
    if (!QRegularExpression("^[A-Za-z0-9_-]+$").match(prefix).hasMatch())
        throw Error("File prefix may contain letters, digits, _ and - only");
    manifestPath.clear();
    QString runDir;
    if (save) {
        QDir dir(folder);
        if (!dir.exists())
            throw Error("Output directory does not exist");
        runDir = dir.filePath(QString::number(run));
        if (!dir.mkdir(QString::number(run)))
            throw Error(
                "Run directory already exists or cannot be created; choose a new run number");
        manifestPath = QDir(runDir).filePath("run.json");
    }
    QJsonArray params;
    for (auto &v : s)
        params.append(v.json());
    manifest = {{"schema", "r5560-run-v1"},
                {"run_id", double(run)},
                {"fifo_request_bytes", readoutBlockKiB * 1024},
                {"simulated", demo},
                {"crate", crate.json()},
                {"settings", params},
                {"started_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"errors", QJsonArray()},
                {"synchronization",
                 s[0].syncConfig & 1 ? "shared-external-RUN" : "single-module-software"}};
    if (controlsBaseboardRun())
        manifest["run_control"] = QJsonObject{{"source", "baseboard-sync-reg-1"},
                                              {"module_index", 0},
                                              {"ip", crate.endpoints[0].ip},
                                              {"internal_address", "0x00140001"}};
    writeManifest("preparing");
    members.clear();
    waiting.clear();
    runFailed = false;
    session = true;
    stopRequested = stopDispatched = false;
    prepareDispatched = false;
    runControlRequests.clear();
    runOptions.clear();
    partialStart.invalidate();
    sharedStart.invalidate();
    runInputReported = false;
    for (int i = 0; i < workers.size(); ++i) {
        members.insert(i);
        waiting.insert(i);
        RunOptions o;
        o.run = run;
        o.fifoKiB = readoutBlockKiB;
        o.expectedSettings = s[i];
        if (save)
            o.file = QDir(runDir).filePath(QString("%1_R%2_M%3.bin").arg(prefix).arg(run).arg(i));
        runOptions.append(o);
    }
    emit logMessage(QString("Readout block: %1 KiB per module (Avalon: request maximum; "
                            "DMA: local slice of a complete ZMQ message)")
                        .arg(readoutBlockKiB));
    setState("Preparing");
    if (!session || stopRequested)
        return;
    if (controlsBaseboardRun())
        requestBaseboardRun(false, RunControlAction::Prepare);
    else
        prepareWorkers();
}
void Acquisition::prepareWorkers() {
    if (!session || phase != "Preparing" || stopRequested)
        return;
    prepareDispatched = true;
    for (int i : members)
        workers[i]->prepare(runOptions[i]);
}
void Acquisition::requestBaseboardRun(bool high, RunControlAction next) {
    const auto request = ++runControlSerial;
    runControlRequests.insert(request, next);
    workers[0]->setBaseboardRun(high, request);
}
void Acquisition::onBaseboardRunWritten(quint64 request, bool high, bool performed) {
    if (!runControlRequests.contains(request))
        return;
    const auto next = runControlRequests.take(request);
    if (!session)
        return;
    if (performed) {
        emit logMessage(high
            ? "Mod 0: sync_reg.1=1 sent; waiting for every module to report RUNNING."
            : "Mod 0: sync_reg.1=0 sent; Mod 0 FPGA RUN input is LOW "
              "(a LOW level alone does not verify baseboard routing).");
        auto control = manifest["run_control"].toObject();
        control[high ? "high_sent_utc" : "low_confirmed_utc"] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        manifest["run_control"] = control;
    }
    if (next == RunControlAction::Prepare)
        prepareWorkers();
    else if (next == RunControlAction::Start && performed && !stopRequested && phase != "Fault") {
        sharedStart.start();
        if (demo)
            for (auto *w : workers)
                w->demoRun();
    } else if (next == RunControlAction::Stop)
        stopWorkers();
    else if (next == RunControlAction::Recover)
        recoverWorkers();
    finishIfReady();
}
void Acquisition::stopRun() {
    cancelWaveform();
    if (!session)
        return;
    if (recoverySession) {
        cancelRecovery();
        return;
    }
    if (phase != "Fault")
        setState("Stopping");
    for (auto *w : workers)
        w->cancelRunStart();
    if (!stopRequested) {
        stopRequested = true;
        if (controlsBaseboardRun() && !controllerFault && workers[0]->isRunning()) {
            requestBaseboardRun(false, RunControlAction::Stop);
            return;
        }
    }
    if (!controlsBaseboardRun() || controllerFault)
        stopWorkers();
}
void Acquisition::stopWorkers() {
    if (stopDispatched)
        return;
    stopDispatched = true;
    if (!prepareDispatched) {
        const bool controllerCleaningUp = controllerFault && members.contains(0);
        members.clear();
        if (controllerCleaningUp)
            members.insert(0);
        finishIfReady();
        return;
    }
    for (int i : members)
        if (workers[i]->isRunning() && !(i == 0 && controllerFault))
            workers[i]->stopRun();
}
void Acquisition::recoverDevices(const QString &folder) {
    if (session || phase != "RecoveryRequired")
        throw Error("Recovery requires connected devices in RecoveryRequired state");
    for (auto *w : workers)
        if (!w->snapshot().connected || !w->isRunning())
            throw Error("Reconnect every module before recovery");
    QDir output(folder);
    if (folder.isEmpty() || !output.exists())
        throw Error("Choose an existing Output folder for recovery data");
    const auto name = "recovery-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") +
                      "-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    if (!output.mkdir(name))
        throw Error("Cannot create recovery directory in " + output.absolutePath());
    const auto directory = output.absoluteFilePath(name);
    recoveryDirectory = directory;
    manifestPath = QDir(directory).filePath("recovery.json");
    manifest = {{"schema", "r5560-recovery-v1"}, {"crate", crate.json()},
                {"original_run_complete", false}, {"event_alignment_known", false},
                {"started_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"errors", QJsonArray()}};
    writeManifest("recovering");
    members.clear();
    waiting.clear();
    runFailed = false;
    session = recoverySession = true;
    recoveryDispatched = false;
    runControlRequests.clear();
    for (int i = 0; i < workers.size(); ++i)
        members.insert(i);
    if (controlsBaseboardRun())
        requestBaseboardRun(false, RunControlAction::Recover);
    setState("Recovering");
    emit logMessage("Stopping devices and saving residual FIFO bytes to " + directory +
                    ". Recovery always saves raw data, regardless of Save Data. "
                    "The interrupted run remains incomplete; event alignment is unknown.");
    if (!controlsBaseboardRun())
        recoverWorkers();
}
void Acquisition::recoverWorkers() {
    if (recoveryDispatched)
        return;
    recoveryDispatched = true;
    for (int i : members)
        if (!(i == 0 && controllerFault))
            workers[i]->recover(recoveryDirectory);
}
void Acquisition::cancelRecovery() {
    if (session && recoverySession) {
        // Queue recovery before cancellation so its atomic flag is not reset by
        // a later low-RUN callback. The pending controller operation stays first.
        recoverWorkers();
        for (int i : members)
            workers[i]->cancelRecovery();
    }
}
void Acquisition::simulateRunEdge() {
    if (!demo || controlsBaseboardRun() || phase != "Armed")
        throw Error("Simulation RUN requires all demo modules ARMED");
    for (auto *w : workers)
        w->demoRun();
}
void Acquisition::captureWaveform(int module, const WaveformRequest &request) {
    request.validate();
    if (phase != "Idle" && phase != "Running")
        throw Error("Waveform capture requires IDLE or RUNNING");
    if (module < 0 || module >= workers.size() || waveformBusy())
        throw Error("Select one connected module and wait for the previous waveform");
    waveModule = module;
    emit waveformBusyChanged(true);
    workers[module]->captureWaveform(request);
}
void Acquisition::cancelWaveform() {
    if (waveModule >= 0 && waveModule < workers.size())
        workers[waveModule]->cancelWaveform();
}
void Acquisition::onError(int index, const QString &message) {
    if (index == 0) {
        controllerFault = true;
        runControlRequests.clear(); // This worker has exited; no callbacks can follow.
    }
    const QString text =
        index < 0 ? "Crate: " + message : QString("Mod %1: %2").arg(index).arg(message);
    runFailed = true;
    if (waveModule >= 0) {
        cancelWaveform();
        waveModule = -1;
        emit waveformBusyChanged(false);
        emit waveformFailed(text);
    }
    setState("Fault");
    auto errors = manifest["errors"].toArray();
    errors.append(text);
    manifest["errors"] = errors;
    emit logMessage(text);
    emit fault(text);
    members.remove(index);
    waiting.remove(index);
    if (session) {
        // Other DAQs should finish preserving their residual data even if one
        // recovery fails. Normal acquisition faults still STOP the whole group.
        if (!recoverySession)
            stopRun();
        else
            recoverWorkers();
        finishIfReady();
    }
}
void Acquisition::onCompleted(int index) {
    if (!members.contains(index))
        return;
    members.remove(index);
    waiting.remove(index);
    if (session && !recoverySession && !stopRequested &&
        (controlsBaseboardRun() || !members.isEmpty())) {
        // A physical RUN fall on any module ends this host run; drain every peer.
        stopRun();
    }
    finishIfReady();
}
void Acquisition::finishIfReady() {
    if (session && members.isEmpty() && runControlRequests.isEmpty())
        finishRun();
}
void Acquisition::writeManifest(const QString &status) {
    manifest["status"] = status;
    QJsonArray results;
    for (int i = 0; i < workers.size(); ++i) {
        const auto s = snapshot(i);
        const bool diagnosticsValid = s.diagnosticsAvailable();
        QJsonArray channelCounts;
        for (int ch = 0; ch < Channels; ++ch)
            channelCounts.append(QJsonObject{{"channel", ch},
                                             {"accepted_u32", double(s.accepted[ch])},
                                             {"rejected_u32", double(s.rejected[ch])},
                                             {"rejected_processing_u32", diagnosticsValid ? QJsonValue(double(s.rejectedProcessing[ch])) : QJsonValue()},
                                             {"rejected_fifo_u32", diagnosticsValid ? QJsonValue(double(s.rejectedFifo[ch])) : QJsonValue()},
                                             {"channel_fifo_peak_events", diagnosticsValid ? QJsonValue(double(s.channelFifoPeak[ch])) : QJsonValue()},
                                             {"received_u64", QString::number(s.received[ch])}});
        results.append(QJsonObject{{"module", i},
                                   {"transport", s.dma ? "dma-zmq" : "avalon-fifo"},
                                   {"dma_build_id", s.dmaBuildIdText()},
                                   {"dma_build_id_raw", s.dma ? QJsonValue(double(s.dmaBuildId)) : QJsonValue()},
                                   {"dma_build_id_recognized", s.dmaBuildRecognized()},
                                   {"dma_tx_words64", QString::number(s.dmaTxWords)},
                                   {"dma_ack_words64", QString::number(s.dmaAckWords)},
                                   {"dma_pending", s.dmaPending},
                                   {"bytes", QString::number(s.bytes)},
                                   {"events", QString::number(s.events)},
                                   {"readout", s.readout.json()},
                                   {"diagnostics", s.diagnosticsJson()},
                                   {"sync_errors", double(s.errors)},
                                   {"start_count", double(s.startCount)},
                                   {"last_status", double(s.status)},
                                   {"fault_utc", s.faultUtc},
                                   {"fault_stop_attempted", s.faultStopAttempted},
                                   {"fault_stop_confirmed", s.faultStopConfirmed},
                                   {"baseboard_run_low_confirmed", s.baseboardRunLowConfirmed},
                                   {"channel_counts", channelCounts},
                                   {"last_t0_ticks", QString::number(s.t0Ticks)},
                                   {"t0_count", double(s.t0Count)}});
    }
    manifest["results"] = results;
    if (!manifestPath.isEmpty())
        writeObject(manifestPath, manifest);
}
void Acquisition::finishRun() {
    if (controlsBaseboardRun() && !runFailed) {
        for (int i = 0; i < workers.size(); ++i)
            if (snapshot(i).status & RunInputHigh) {
                runFailed = true;
                const auto text = QString("Mod %1 RUN input remains HIGH after STOP; "
                                          "check baseboard sync_reg.1 routing").arg(i);
                auto errors = manifest["errors"].toArray();
                errors.append(text);
                manifest["errors"] = errors;
                emit logMessage(text);
                emit fault(text);
            }
    }
    session = false;
    manifest["finished_utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    try {
        writeManifest(recoverySession ? (runFailed ? "recovery_incomplete" : "recovery_complete")
                                      : (runFailed ? "incomplete" : "complete"));
    } catch (const std::exception &e) {
        runFailed = true;
        emit fault(QString::fromUtf8(e.what()));
    }
    setState(runFailed ? "Fault" : "Idle");
    if (recoverySession) {
        recoverySession = false;
        emit settingsChanged();
        emit recoveryFinished(!runFailed);
        emit logMessage(runFailed ? "Recovery incomplete; saved bytes preserved. Disconnect and "
                                    "reconnect to retry. FIFO was not cleared."
                                  : "Recovery complete: devices IDLE, residual data saved separately. "
                                    "Inspect hardware error flags before a new run; the interrupted "
                                    "run has not been repaired.");
        return;
    }
    emit runFinished(!runFailed);
    if (runFailed) {
        emit logMessage(manifestPath.isEmpty()
            ? QString("Run incomplete. Save Data was off; no run.json or event file was saved. "
                      "Keep the error log; enable Save Data before another diagnostic run.")
            : QString("Run incomplete; inspect %1 and preserved data")
                  .arg(QFileInfo(manifestPath).absoluteFilePath()));
    } else {
        emit logMessage("Run complete; all FIFOs drained and files closed");
    }
}
void Acquisition::recordRunDiagnostics(const QString &stage) {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    QJsonArray modules;
    for (int i = 0; i < workers.size(); ++i) {
        const auto s = snapshot(i);
        const auto sampleUtc = s.runStatusReadMs
            ? QDateTime::fromMSecsSinceEpoch(s.runStatusReadMs, Qt::UTC).toString(Qt::ISODateWithMs)
            : QString("unavailable");
        const qint64 age = s.runStatusReadMs ? qMax(qint64(0), now - s.runStatusReadMs) : -1;
        const auto delta = quint32(s.startCount - s.startCountAtPrepare);
        const bool high = s.status & RunInputHigh;
        modules.append(QJsonObject{{"module", i}, {"ip", crate.endpoints[i].ip},
            {"status", double(s.status)}, {"errors", double(s.errors)}, {"run_high", high},
            {"armed", bool(s.status & Armed)}, {"running", bool(s.status & Running)},
            {"clock_good", bool(s.status & ClockGood)},
            {"sync_config_at_prepare", double(s.settings.syncConfig)},
            {"start_count", double(s.startCount)}, {"start_delta", double(delta)},
            {"status_sample_utc", sampleUtc}, {"sample_age_ms", double(age)}});
        emit logMessage(QString("Mod %1 RUN diagnostics [%2]: %3 | status=0x%4 errors=0x%5 "
                                "RUN=%6 ARMED=%7 RUNNING=%8 CLOCK_GOOD=%9 start_delta=%10 "
                                "sync_config_at_prepare=0x%11 status_sample_utc=%12 sample_age_ms=%13")
            .arg(i).arg(stage).arg(crate.endpoints[i].ip)
            .arg(s.status, 8, 16, QChar('0')).arg(s.errors, 8, 16, QChar('0'))
            .arg(int(high)).arg(int(bool(s.status & Armed))).arg(int(bool(s.status & Running)))
            .arg(int(bool(s.status & ClockGood))).arg(delta)
            .arg(s.settings.syncConfig, 8, 16, QChar('0')).arg(sampleUtc).arg(age));
    }
    auto history = manifest["run_diagnostics"].toArray();
    history.append(QJsonObject{{"stage", stage},
        {"utc", QDateTime::fromMSecsSinceEpoch(now, Qt::UTC).toString(Qt::ISODateWithMs)},
        {"modules", modules}});
    manifest["run_diagnostics"] = history;
}
void Acquisition::checkState() {
    if (!session || (phase != "Armed" && phase != "Running"))
        return;
    int running = 0;
    for (auto *w : workers)
        if (w->snapshot().status & Running)
            ++running;
    if (running == workers.size()) {
        if (phase != "Running") {
            if (controlsBaseboardRun())
                recordRunDiagnostics("all-RUNNING");
            setState("Running");
            try {
                writeManifest("running");
            } catch (const std::exception &e) {
                onError(-1, QString::fromUtf8(e.what()));
            }
        }
        partialStart.invalidate();
    } else if (controlsBaseboardRun() && sharedStart.isValid() &&
               phase == "Armed" && sharedStart.elapsed() > 5000) {
        recordRunDiagnostics("start-timeout-before-STOP");
        onError(-1, "Baseboard RUN was requested but not all DAQs entered RUNNING within 5 s. "
                    "See per-module RUN diagnostics above. If RUN stays LOW, check baseboard "
                    "I/O Settings > DAQ Sync In: route sync_reg.1 to FPGA SYNC 1. "
                    "A LOW input before Start does not prove this route is configured. "
                    "Lowering RUN and stopping.");
    } else if (running > 0 && phase == "Armed") {
        if (!partialStart.isValid())
            partialStart.start();
        else if (partialStart.elapsed() > 2000) {
            recordRunDiagnostics("partial-start-before-STOP");
            onError(-1, "Only some modules saw RUN; synchronization failed");
        }
    }
    if (controlsBaseboardRun() && phase == "Armed" && sharedStart.isValid() &&
        sharedStart.elapsed() >= 500 && !runInputReported) {
        runInputReported = true;
        recordRunDiagnostics("after-RUN-request");
    }
}
} // namespace daq
