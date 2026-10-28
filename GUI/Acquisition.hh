#pragma once
#include "ReadoutThread.hh"

namespace daq {
class Acquisition final : public QObject {
    Q_OBJECT
  public:
    explicit Acquisition(QObject *parent = nullptr);
    ~Acquisition() override;
    QString state() const {
        return phase;
    }
    bool idle() const {
        return phase == "Idle";
    }
    bool simulated() const {
        return demo;
    }
    bool hasSession() const {
        return session;
    }
    int count() const {
        return workers.size();
    }
    Snapshot snapshot(int index) const {
        return workers.at(index)->snapshot();
    }
    QVector<ModuleSettings> settings() const;
    const Config &config() const {
        return crate;
    }
    QString lastManifest() const {
        return manifestPath;
    }
    void connectCrate(const Config &config, bool simulated);
    void disconnectCrate();
    void apply(const QVector<ModuleSettings> &settings);
    void applyModule(int module, const ModuleSettings &settings);
    void refresh();
    void refreshModule(int module);
    void clearErrors();
    void resetDmaEpoch(); // Explicit user-confirmed discard, never an automatic recovery.
    void setReadoutBlockKiB(int kib);
    int readoutRequestKiB() const { return readoutBlockKiB; }
    void startRun(quint32 run, const QString &folder, const QString &prefix, bool save);
    void stopRun();
    void recoverDevices(const QString &folder);
    void cancelRecovery();
    void simulateRunEdge();
    bool waveformBusy() const {
        return waveModule >= 0;
    }
    bool controlsBaseboardRun() const { return workers.size() > 1; }
    void captureWaveform(int module, const WaveformRequest &request);
    void cancelWaveform();
  signals:
    void stateChanged(const QString &state);
    void settingsChanged();
    void moduleSettingsChanged(int module);
    void logMessage(const QString &message);
    void fault(const QString &message);
    void runFinished(bool complete);
    void recoveryFinished(bool complete);
    void waveformReady(const daq::Waveform &waveform);
    void waveformFailed(const QString &message);
    void waveformBusyChanged(bool busy);

  private:
    QVector<ReadoutThread *> workers;
    Config crate;
    QString phase = "Disconnected", manifestPath;
    bool demo = false, runFailed = false, session = false, recoverySession = false;
    QSet<int> waiting, members;
    QTimer monitor;
    QElapsedTimer partialStart;
    QElapsedTimer sharedStart;
    bool runInputReported = false;
    enum class RunControlAction { Prepare, Start, Stop, Recover };
    QMap<quint64, RunControlAction> runControlRequests;
    quint64 runControlSerial = 0;
    bool controllerFault = false, stopRequested = false, stopDispatched = false;
    bool recoveryDispatched = false;
    bool prepareDispatched = false;
    QVector<RunOptions> runOptions;
    QString recoveryDirectory;
    QJsonObject manifest;
    int waveModule = -1;
    int configurationModule = -1;
    int readoutBlockKiB = 64;
    QString configurationResult;
    void setState(const QString &state);
    void onError(int index, const QString &message);
    void onCompleted(int index);
    void finishRun();
    void writeManifest(const QString &status);
    void checkState();
    void recordRunDiagnostics(const QString &stage);
    void requestBaseboardRun(bool high, RunControlAction next);
    void onBaseboardRunWritten(quint64 request, bool high, bool performed);
    void prepareWorkers();
    void stopWorkers();
    void recoverWorkers();
    void finishIfReady();
};
} // namespace daq
