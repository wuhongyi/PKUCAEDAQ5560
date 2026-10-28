#pragma once
#include <QtWidgets>
#include "Acquisition.hh"

class ControlPanel;
class BasicSettings;
class LogicSettings;
class OnlineRate;
class Debug;
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool SetOutputSettings(QString folder, QString prefix, QString run, bool saved);
    void GetOutputSettings(QString &folder, QString &prefix, QString &run, bool &saved);
    void ShowOutputSettings();
    void openConfig(const QString &path, bool demo = false);
    void openDemo(int modules = 3);
    daq::Acquisition *acquisition() {
        return &acq;
    }
    void captureWindows(const QString &directory);
  public slots:
    void StartAcquisition();
    void StopAcquisition();
    void ShowBasicSettings();
    void ShowLogicSettings();
    void ShowOnlineRate();

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    daq::Acquisition acq;
    ControlPanel *control;
    BasicSettings *basic;
    LogicSettings *logic;
    OnlineRate *online;
    Debug *debug;
    QPlainTextEdit *logView;
    QLineEdit *leFolder, *lePrefix, *leRun;
    QCheckBox *leSaved;
    QLabel *status, *message;
    QTableWidget *modules;
    QAction *connectAction, *disconnectAction, *loadAction, *saveAction, *saveAsAction,
        *outputAction, *configuredAction, *resetDmaAction;
    QVector<QAction *> deviceActions;
    QString outputFolder, outputPrefix, outputRun, settingsPath;
    bool closing = false;
    void connectDialog();
    void log(const QString &message);
    void updateState();
    void updateModules();
    void resetDmaEpoch();
    void loadParameters(const QString &path = QString());
    void loadConfiguredParameters();
    void saveParameters(bool choose);
    void persistOutput();
    void report(const std::exception &error);
};
