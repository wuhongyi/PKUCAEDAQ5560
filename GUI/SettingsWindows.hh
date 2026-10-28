#pragma once
#include <QtWidgets>
#include "Acquisition.hh"

class BasicSettings final : public QMainWindow {
    Q_OBJECT
  public:
    explicit BasicSettings(daq::Acquisition *acq, QWidget *parent = nullptr);
    void reload();
    QVector<daq::ModuleSettings> editedSettings();
    daq::ModuleSettings editedModuleSettings();

  signals:
    void parameterMessage(const QString &text);

  private:
    daq::Acquisition *acq;
    QVector<daq::ModuleSettings> edited;
    QToolBox *modules;
    QTableWidget *table;
    QComboBox *copyFrom;
    QWidget *editArea;
    QLabel *message;
    int selected = 0;
    int copySourceModule = -1;
    bool loading = false;
    void store();
    void display(int index);
    void reloadModule(int index);
    void showModuleStatus();
    void focusInvalidSetting(const daq::SettingsError &error);
    void copyParameters();
    void copyCells();
    void pasteCells();
};
class LogicSettings final : public QMainWindow {
    Q_OBJECT
  public:
    explicit LogicSettings(daq::Acquisition *acq, QWidget *parent = nullptr);
    void reload();

  private:
    daq::Acquisition *acq;
    QComboBox *mode, *runInput, *t0Input;
    QCheckBox *t0Enabled;
    QPushButton *apply, *simulate;
    QTableWidget *status;
    QLabel *state;
    void updateStatus();
};
class OnlineRate final : public QMainWindow {
    Q_OBJECT
  public:
    explicit OnlineRate(daq::Acquisition *acq, QWidget *parent = nullptr);

  private:
    friend class OnlineDiagnosticsTests;
    daq::Acquisition *acq;
    QComboBox *module, *channel;
    QTableWidget *table;
    QWidget *spectrum;
    QLabel *totals, *dmaDiagnostics, *diagnosticsStatus;
    void updateData();
    void displaySnapshot(const daq::Snapshot &snapshot);
};
