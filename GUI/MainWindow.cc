#include "MainWindow.hh"
#include "ControlPanel.hh"
#include "OutputSettingDialog.hh"
#include "SettingsWindows.hh"
#include "AnalysisDialog.hh"
#include "Debug.hh"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), acq(this) {
    setObjectName("MainWindow");
    setWindowTitle("GDDAQ-R5560");
    setWindowIcon(QIcon(":/images/logo2.png"));
    resize(480, 800);
    QSettings prefs;
    outputFolder = prefs.value("output/folder", QDir::homePath()).toString();
    outputPrefix = prefs.value("output/prefix", "data").toString();
    outputRun = prefs.value("output/run", "0").toString();
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    control = new ControlPanel(this);
    layout->addWidget(control);
    auto *output = new QGroupBox("Output");
    layout->addWidget(output);
    auto *form = new QFormLayout(output);
    leFolder = new QLineEdit;
    lePrefix = new QLineEdit;
    leRun = new QLineEdit;
    leSaved = new QCheckBox("Save Data");
    leSaved->setChecked(prefs.value("output/save", false).toBool());
    leFolder->setReadOnly(true);
    lePrefix->setReadOnly(true);
    leRun->setReadOnly(true);
    form->addRow("Folder", leFolder);
    form->addRow("Prefix", lePrefix);
    form->addRow("Run", leRun);
    form->addRow(leSaved);
    ShowOutputSettings();
    modules = new QTableWidget;
    modules->setColumnCount(3);
    modules->setHorizontalHeaderLabels({"Module", "IP", "State"});
    modules->setEditTriggers(QAbstractItemView::NoEditTriggers);
    modules->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    modules->verticalHeader()->hide();
    layout->addWidget(modules, 1);
    message = new QLabel("File > Connect to device");
    message->setWordWrap(true);
    message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(message);
    status = new QLabel("Disconnected");
    statusBar()->addPermanentWidget(status, 1);
    basic = new BasicSettings(&acq, this);
    connect(basic, &BasicSettings::parameterMessage, this, &MainWindow::log);
    logic = new LogicSettings(&acq, this);
    online = new OnlineRate(&acq, this);
    debug = new Debug(&acq, this);
    logView = debug->logWidget();
    auto *debugTools = debug->addToolBar("Actions");
    auto *clear = debugTools->addAction("Clear hardware error flags (IDLE)");
    connect(clear, &QAction::triggered, this, [this] {
        try {
            acq.clearErrors();
        } catch (const std::exception &e) {
            report(e);
        }
    });
    auto *exportLog = debugTools->addAction("Save log");
    connect(exportLog, &QAction::triggered, this, [this] {
        auto file = QFileDialog::getSaveFileName(this, "Save log", "gddaq-r5560.log");
        if (file.isEmpty())
            return;
        QSaveFile f(file);
        auto data = logView->toPlainText().toUtf8();
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
            QMessageBox::warning(this, "Log", f.errorString());
    });
    auto *toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    auto *exit = toolbar->addAction(QIcon(":/images/exit.png"), "Exit");
    connect(exit, &QAction::triggered, this, &QWidget::close);
    auto *b = toolbar->addAction("Basic");
    connect(b, &QAction::triggered, this, &MainWindow::ShowBasicSettings);
    auto *l = toolbar->addAction("Logic");
    connect(l, &QAction::triggered, this, &MainWindow::ShowLogicSettings);
    auto *o = toolbar->addAction("Online");
    connect(o, &QAction::triggered, this, &MainWindow::ShowOnlineRate);
    auto *d = toolbar->addAction("Debug");
    connect(d, &QAction::triggered, this, [this] {
        debug->show();
        debug->raise();
    });
    deviceActions = {b, l, o};
    auto *file = menuBar()->addMenu("&File");
    connectAction = file->addAction("Connect to device");
    disconnectAction = file->addAction("Disconnect");
    outputAction = file->addAction("Output Configure");
    file->addSeparator();
    loadAction = file->addAction(QIcon(":/images/open.png"), "Load Settings");
    configuredAction = file->addAction("Load configured parameters");
    saveAction = file->addAction(QIcon(":/images/save.png"), "Save");
    saveAsAction = file->addAction("Save As ...");
    file->addSeparator();
    file->addAction(exit);
    connect(connectAction, &QAction::triggered, this, &MainWindow::connectDialog);
    connect(disconnectAction, &QAction::triggered, this, [this] {
        try {
            acq.disconnectCrate();
        } catch (const std::exception &e) {
            report(e);
        }
    });
    connect(outputAction, &QAction::triggered, this, [this] {
        OutputSettingDialog dialog(this);
        dialog.exec();
    });
    connect(loadAction, &QAction::triggered, this, [this] { loadParameters(); });
    connect(configuredAction, &QAction::triggered, this, &MainWindow::loadConfiguredParameters);
    connect(saveAction, &QAction::triggered, this, [this] { saveParameters(false); });
    connect(saveAsAction, &QAction::triggered, this, [this] { saveParameters(true); });
    auto *deviceMenu = menuBar()->addMenu("&Device");
    resetDmaAction = deviceMenu->addAction("Reset DMA epoch ...");
    resetDmaAction->setObjectName("ResetDmaEpochAction");
    connect(resetDmaAction, &QAction::triggered, this, &MainWindow::resetDmaEpoch);
    auto *analysis = new AnalysisDialog(this);
    auto *tools = menuBar()->addMenu("&Tools");
    auto *analysisAction = tools->addAction("Offline analysis / Coincidence");
    connect(analysisAction, &QAction::triggered, this, [analysis] {
        analysis->show();
        analysis->raise();
    });
    auto *help = menuBar()->addMenu("&Help");
    auto *demo = help->addAction("Open demo crate (3 modules)");
    connect(demo, &QAction::triggered, this, [this] {
        try {
            openDemo();
        } catch (const std::exception &e) {
            report(e);
        }
    });
    auto *about = help->addAction("About");
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, "GDDAQ-R5560",
            "GDDAQ-R5560 1.0\n32 channels per module / Avalon control, Ethernet data\n\nGUI27xx interface, "
            "ControlPanel, OutputSettingDialog, images and UserDPP energy formula reused/adapted "
            "from Hongyi Wu's reference project.\n\nSupports firmware API 2.0 (Avalon FIFO) "
            "and 3.0 (DMA). Shared "
            "clock and external RUN wiring are required for multi-module time alignment.");
    });
    connect(&acq, &daq::Acquisition::stateChanged, this,
            [this](const QString &) { updateState(); });
    connect(&acq, &daq::Acquisition::logMessage, this, &MainWindow::log);
    connect(&acq, &daq::Acquisition::fault, this, [this](const QString &text) {
        message->setText(text + "\nSee Debug. When the current operation finishes, use File > "
                                "Disconnect, then Connect to device. If a device is not IDLE, "
                                "use Stop / Recover.");
        message->setStyleSheet("color:#b00020");
    });
    connect(&acq, &daq::Acquisition::runFinished, this, [this](bool success) {
        if (success) {
            auto n = daq::parseUInt(outputRun, "Run");
            if (n < 0xffffffffu)
                outputRun = QString::number(n + 1);
            ShowOutputSettings();
            persistOutput();
        }
        updateState();
        if (closing)
            QTimer::singleShot(0, this, &QWidget::close);
    });
    connect(&acq, &daq::Acquisition::recoveryFinished, this, [this](bool) {
        updateState();
        if (closing)
            QTimer::singleShot(0, this, &QWidget::close);
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::updateModules);
    timer->start(500);
    updateState();
}
void MainWindow::report(const std::exception &e) {
    log(QString::fromUtf8(e.what()));
    QMessageBox::warning(this, "R5560", QString::fromUtf8(e.what()));
}
void MainWindow::resetDmaEpoch() {
    if (acq.state() != "RecoveryRequired" || acq.hasSession())
        return;
    QMessageBox confirm(QMessageBox::Warning, "Reset DMA epoch",
        "Use Stop / Recover first to save residual data in the Output folder.\n\n"
        "This action discards any remaining unread DMA data and resets the transfer "
        "epoch on every connected DMA module. All modules must be stopped, with "
        "external RUN LOW.\n\n"
        "Existing saved files are kept. Missing data cannot be restored, and the "
        "interrupted run remains incomplete.", QMessageBox::Cancel, this);
    confirm.setObjectName("ResetDmaEpochConfirmation");
    auto *discard = confirm.addButton("Discard remaining DMA data and reset",
                                      QMessageBox::DestructiveRole);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.setEscapeButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != discard)
        return;
    try {
        acq.resetDmaEpoch();
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::log(const QString &s) {
    logView->appendPlainText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") +
                             "  " + s);
}
void MainWindow::persistOutput() {
    QSettings s;
    s.setValue("output/folder", outputFolder);
    s.setValue("output/prefix", outputPrefix);
    s.setValue("output/run", outputRun);
    s.setValue("output/save", leSaved->isChecked());
}
bool MainWindow::SetOutputSettings(QString folder, QString prefix, QString run, bool saved) {
    try {
        if (!acq.idle() && acq.state() != "Disconnected" && acq.state() != "RecoveryRequired")
            throw daq::Error("Output settings require IDLE or awaiting recovery");
        if (folder.isEmpty())
            folder = QDir::homePath();
        if (!QDir(folder).exists())
            throw daq::Error("Output directory does not exist");
        if (!QRegularExpression("^[A-Za-z0-9_-]+$").match(prefix).hasMatch())
            throw daq::Error("Prefix may contain letters, digits, _ and - only");
        auto n = daq::parseUInt(run, "Run");
        outputFolder = QDir(folder).absolutePath();
        outputPrefix = prefix;
        outputRun = QString::number(n);
        leSaved->setChecked(saved);
        persistOutput();
        return true;
    } catch (const std::exception &e) {
        report(e);
        return false;
    }
}
void MainWindow::GetOutputSettings(QString &folder, QString &prefix, QString &run, bool &saved) {
    folder = outputFolder;
    prefix = outputPrefix;
    run = outputRun;
    saved = leSaved->isChecked();
}
void MainWindow::ShowOutputSettings() {
    leFolder->setText(outputFolder);
    lePrefix->setText(outputPrefix);
    leRun->setText(outputRun);
}
void MainWindow::updateState() {
    auto phase = acq.state();
    bool idle = acq.idle(), disconnected = phase == "Disconnected";
    bool busy = phase == "Preparing" || phase == "Arming" || phase == "Armed" ||
                phase == "Running" || phase == "Stopping";
    setWindowTitle(QString("GDDAQ-R5560%1 — Read block %2 KiB")
                       .arg(acq.simulated() ? " — DEMO" : "").arg(acq.readoutRequestKiB()));
    status->setText(phase);
    status->setStyleSheet(phase == "Fault"     ? "font-weight:bold;color:#b00020"
                          : phase == "Running" ? "font-weight:bold;color:#087a21"
                                               : "font-weight:bold");
    control->pbStartAcq->setEnabled(idle);
    control->pbStopAcq->setText(phase == "RecoveryRequired" ? "Stop / Recover"
                               : phase == "Recovering" ? "Cancel recovery" : "Stop");
    control->pbStopAcq->setMaximumWidth(qMax(100, control->pbStopAcq->sizeHint().width()));
    control->pbStopAcq->setEnabled(busy || acq.hasSession() || phase == "RecoveryRequired");
    connectAction->setEnabled(disconnected);
    disconnectAction->setEnabled(!acq.hasSession() &&
        (idle || phase == "Fault" || phase == "Connecting" || phase == "RecoveryRequired"));
    outputAction->setEnabled(idle || disconnected || phase == "RecoveryRequired");
    bool hasDma = false;
    for (int i = 0; i < acq.count(); ++i)
        hasDma |= acq.snapshot(i).dma;
    resetDmaAction->setEnabled(phase == "RecoveryRequired" && !acq.hasSession() && hasDma);
    leSaved->setEnabled(idle || disconnected);
    loadAction->setEnabled(idle);
    configuredAction->setEnabled(idle);
    saveAction->setEnabled(idle);
    saveAsAction->setEnabled(idle);
    for (auto *a : deviceActions)
        a->setEnabled(acq.count() > 0);
    if (phase != "Fault") {
        message->setStyleSheet("");
        message->setText(
            phase == "Armed"      ? (acq.controlsBaseboardRun()
                ? "All modules ARMED. Mod 0 is raising baseboard RUN; waiting for RUNNING."
                : "Module ARMED. Apply the external RUN edge.")
            : phase == "RecoveryRequired"
                ? "Device requires recovery. Use Stop / Recover to stop and save residual "
                  "data in the Output folder (even when Save Data is off)."
            : phase == "Recovering"
                ? "Stopping devices and saving residual FIFO data. Cancel leaves unread data "
                  "in hardware; no FIFO clear is issued."
            : phase == "Stopping" ? "Stopping and reading remaining FIFO data ..."
            : phase == "Idle"
                ? "Ready. Parameters may differ by channel/module; shared RUN settings must match."
                : phase);
    }
    updateModules();
}
void MainWindow::updateModules() {
    modules->setRowCount(acq.count());
    for (int i = 0; i < acq.count(); ++i) {
        const auto s = acq.snapshot(i);
        QStringList row{QString("Mod %1").arg(i), acq.config().endpoints[i].ip,
                        s.connected ? daq::statusText(s.status) : "Disconnected"};
        for (int c = 0; c < 3; ++c) {
            if (!modules->item(i, c))
                modules->setItem(i, c, new QTableWidgetItem);
            modules->item(i, c)->setText(row[c]);
        }
    }
}
void MainWindow::connectDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Connect to a crate");
    dialog.resize(550, 300);
    auto *v = new QVBoxLayout(&dialog);
    auto *load = new QPushButton("Load configure file");
    v->addWidget(load);
    auto *path = new QLineEdit;
    v->addWidget(path);
    auto *info = new QPlainTextEdit;
    info->setReadOnly(true);
    v->addWidget(info);
    auto *demo = new QCheckBox("DEMO — simulated modules, no hardware connection");
    v->addWidget(demo);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Connect");
    v->addWidget(buttons);
    connect(load, &QPushButton::clicked, &dialog, [&] {
        auto f = QFileDialog::getOpenFileName(&dialog, "Open config file", QString(),
                                              "GDDAQ configure (*.dat *.json);;All Files (*)");
        if (f.isEmpty())
            return;
        path->setText(f);
        try {
            auto c = daq::Config::load(f);
            QStringList lines;
            for (int i = 0; i < c.endpoints.size(); ++i)
                lines << QString("Mod %1    %2:%3    32 channels    DAQ ID %4")
                             .arg(i)
                             .arg(c.endpoints[i].ip)
                             .arg(c.endpoints[i].port)
                             .arg(c.endpoints[i].id);
            info->setPlainText(lines.join('\n'));
        } catch (const std::exception &e) {
            info->setPlainText(QString::fromUtf8(e.what()));
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            openConfig(path->text(), demo->isChecked());
            dialog.accept();
        } catch (const std::exception &e) {
            report(e);
        }
    });
    dialog.exec();
}
void MainWindow::openConfig(const QString &path, bool demo) {
    auto c = daq::Config::load(path);
    acq.connectCrate(c, demo);
    settingsPath = c.parameterFile;
    log("Loaded crate " + c.filename +
        "; paths resolve relative to this file. Connect reads current hardware settings.");
    if (!c.parameterFile.isEmpty() || !c.userFile.isEmpty())
        log("Use File > Load configured parameters to apply par/userpars. These files are "
            "validated together before any write.");
}
void MainWindow::openDemo(int count) {
    if (count < 1 || count > 64)
        throw daq::Error("Demo module count must be 1..64");
    daq::Config c;
    for (int i = 0; i < count; ++i)
        c.endpoints.append({QString("127.0.0.%1").arg(i + 1), 8888, quint32(i)});
    acq.connectCrate(c, true);
    log("DEMO: generated events; no hardware is accessed");
}
void MainWindow::loadParameters(const QString &supplied) {
    QString path = supplied;
    if (path.isEmpty())
        path = QFileDialog::getOpenFileName(this, "Load Settings", settingsPath,
                                            "R5560 settings (*.json *.par);;All Files (*)");
    if (path.isEmpty())
        return;
    try {
        auto s = daq::loadSettings(path, acq.count());
        acq.apply(s);
        settingsPath = path;
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::loadConfiguredParameters() {
    try {
        auto c = acq.config();
        auto s = acq.settings();
        if (!c.parameterFile.isEmpty())
            s = daq::loadSettings(c.parameterFile, acq.count());
        if (!c.userFile.isEmpty())
            daq::applyUserFile(c.userFile, s);
        for (auto &v : s)
            v.validate();
        acq.apply(s);
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::saveParameters(bool choose) {
    auto path = settingsPath;
    if (choose || path.isEmpty())
        path = QFileDialog::getSaveFileName(this, "Save Settings",
                                            path.isEmpty() ? "setting-r5560.json" : path,
                                            "R5560 settings (*.json)");
    if (path.isEmpty())
        return;
    try {
        daq::saveSettings(path, basic->editedSettings());
        settingsPath = path;
        log("Saved editor parameters: " + path);
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::StartAcquisition() {
    try {
        auto edited = basic->editedSettings(), actual = acq.settings();
        for (int i = 0; i < edited.size(); ++i)
            if (edited[i].json() != actual[i].json())
                throw daq::Error("Basic Settings contains unapplied edits. Use Write module or "
                                 "Read module before Start.");
        acq.startRun(daq::parseUInt(outputRun, "Run"), outputFolder, outputPrefix,
                     leSaved->isChecked());
        persistOutput();
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::StopAcquisition() {
    try {
        if (acq.state() == "RecoveryRequired")
            acq.recoverDevices(outputFolder);
        else
            acq.stopRun();
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::ShowBasicSettings() {
    basic->show();
    basic->raise();
}
void MainWindow::ShowLogicSettings() {
    logic->show();
    logic->raise();
}
void MainWindow::ShowOnlineRate() {
    online->show();
    online->raise();
}
void MainWindow::closeEvent(QCloseEvent *e) {
    auto s = acq.state();
    if (s == "Recovering") {
        closing = true;
        e->ignore();
        return;
    }
    if (s == "Preparing" || s == "Arming" || s == "Armed" || s == "Running" || s == "Stopping") {
        closing = true;
        acq.stopRun();
        e->ignore();
        return;
    }
    try {
        acq.disconnectCrate();
    } catch (const std::exception &) {
        closing = true;
        acq.stopRun();
        e->ignore();
        return;
    }
    basic->close();
    logic->close();
    online->close();
    debug->close();
    for (auto *dialog : findChildren<QDialog *>())
        dialog->close();
    persistOutput();
    e->accept();
}
void MainWindow::captureWindows(const QString &directory) {
    QDir().mkpath(directory);
    show();
    basic->show();
    logic->show();
    online->show();
    debug->show();
    QApplication::processEvents();
    for (QWidget *w : QVector<QWidget *>{this, basic, logic, online, debug})
        w->grab().save(QDir(directory).filePath(w->objectName() + ".png"));
    auto *tabs = online->findChild<QTabWidget *>();
    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    online->grab().save(QDir(directory).filePath("Spectrum.png"));
    tabs->setCurrentIndex(0);
}
