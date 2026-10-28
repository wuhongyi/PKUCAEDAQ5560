#include "SettingsWindows.hh"
#include <algorithm>
#include <cmath>

namespace {
void error(QWidget *parent, const std::exception &e) {
    QMessageBox::warning(parent, "R5560", QString::fromUtf8(e.what()));
}
void setCell(QTableWidget *t, int r, int c, const QString &v) {
    if (!t->item(r, c))
        t->setItem(r, c, new QTableWidgetItem);
    t->item(r, c)->setText(v);
    t->item(r, c)->setTextAlignment(Qt::AlignCenter);
}
class Spectrum : public QWidget {
  public:
    std::array<quint64, 512> bins{};
    bool logarithmic = false;
    explicit Spectrum(QWidget *p = nullptr) : QWidget(p) {
        setMinimumSize(600, 300);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF plot(65, 25, width() - 95, height() - 85);
        p.setPen(Qt::black);
        p.drawLine(plot.bottomLeft(), plot.topLeft());
        p.drawLine(plot.bottomLeft(), plot.bottomRight());
        double max = 1;
        for (auto n : bins)
            max = std::max(max, logarithmic ? std::log10(double(n) + 1) : double(n));
        for (int i = 0; i <= 4; ++i) {
            double f = i / 4.0;
            int x = int(plot.left() + f * plot.width());
            p.drawText(QRectF(x - 35, plot.bottom() + 8, 70, 22), Qt::AlignCenter,
                       QString::number(int(f * 16384)));
            p.setPen(QColor(225, 225, 225));
            p.drawLine(QPointF(plot.left(), plot.bottom() - f * plot.height()),
                       QPointF(plot.right(), plot.bottom() - f * plot.height()));
            p.setPen(Qt::black);
            p.drawText(QRectF(0, plot.bottom() - f * plot.height() - 12, 58, 24),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(logarithmic ? std::pow(10, f * max) - 1 : f * max, 'g', 3));
        }
        QPainterPath path;
        for (int i = 0; i < 512; ++i) {
            double y = logarithmic ? std::log10(double(bins[i]) + 1) : double(bins[i]);
            QPointF v(plot.left() + i * plot.width() / 511,
                      plot.bottom() - y / max * plot.height());
            if (i == 0)
                path.moveTo(v);
            else
                path.lineTo(v);
        }
        p.setPen(QPen(QColor(35, 90, 190), 1.4));
        p.drawPath(path);
        p.setPen(Qt::black);
        p.drawText(QRectF(65, height() - 30, width() - 100, 22), Qt::AlignCenter,
                   "Reconstructed energy (ADC units)");
    }
};
} // namespace

BasicSettings::BasicSettings(daq::Acquisition *a, QWidget *parent)
    : QMainWindow(parent, Qt::Window), acq(a) {
    setWindowTitle("Basic Settings");
    setObjectName("BasicSettings");
    resize(1180, 780);
    auto *split = new QSplitter(this);
    setCentralWidget(split);
    modules = new QToolBox;
    modules->setMinimumWidth(160);
    modules->setMaximumWidth(230);
    split->addWidget(modules);
    editArea = new QWidget;
    split->addWidget(editArea);
    auto *v = new QVBoxLayout(editArea);
    auto *tabs = new QTabWidget;
    v->addWidget(tabs);
    table = new QTableWidget(daq::Channels, daq::ParameterCount + 1);
    table->setObjectName("ChannelParameters");
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    tabs->addTab(table, "UserDPP");
    QStringList headers{"Enable"};
    for (auto &p : daq::Parameters)
        headers << p.title;
    table->setHorizontalHeaderLabels(headers);
    QStringList rows;
    for (int ch = 0; ch < daq::Channels; ++ch)
        rows << QString::number(ch);
    table->setVerticalHeaderLabels(rows);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int p = 0; p < daq::ParameterCount; ++p)
        table->horizontalHeaderItem(p + 1)->setToolTip(daq::Parameters[p].help);
    message = new QLabel(
        "Windows: samples (8 ns/sample). Tau: ns. Read device before editing an existing setup.");
    message->setObjectName("ParameterStatus");
    message->setWordWrap(true);
    v->addWidget(message);

    // Follow GUI27xx BasicSettings_TabUserDPP: select destinations independently
    // of the source channel, then copy the matching columns into selected items.
    auto *copyGroup = new QGroupBox;
    copyGroup->setObjectName("ChannelCopyControls");
    auto *copyLayout = new QHBoxLayout(copyGroup);
    copyLayout->setContentsMargins(11, 2, 11, 2);
    copyLayout->setSpacing(10);
    copyLayout->addWidget(new QLabel("Select"));
    for (const auto &mode : {qMakePair(QString("Row"), QAbstractItemView::SelectRows),
                             qMakePair(QString("Column"), QAbstractItemView::SelectColumns),
                             qMakePair(QString("Item"), QAbstractItemView::SelectItems)}) {
        auto *radio = new QRadioButton(mode.first, copyGroup);
        radio->setObjectName("Select" + mode.first);
        radio->setChecked(mode.second == QAbstractItemView::SelectItems);
        copyLayout->addWidget(radio);
        connect(radio, &QRadioButton::clicked, this,
                [this, behavior = mode.second] { table->setSelectionBehavior(behavior); });
    }
    copyLayout->addStretch();
    copyLayout->addWidget(new QLabel("copy from"));
    copyFrom = new QComboBox;
    copyFrom->setObjectName("CopySourceChannel");
    for (int ch = 0; ch < daq::Channels; ++ch)
        copyFrom->addItem(QString("ch - %1").arg(ch, 2, 10, QChar('0')));
    copyFrom->setToolTip("Source channel in the current module");
    copyLayout->addWidget(copyFrom);
    auto *copy = new QPushButton("Copy");
    copy->setObjectName("CopySelectedParameters");
    copy->setToolTip("Copy from the source channel to selected cells only. "
                     "Use Row for whole channels; Ctrl selects separate destinations.");
    copyLayout->addWidget(copy);
    v->addWidget(copyGroup);
    auto *copyHint = new QLabel(
        "Select destination rows or cells (Ctrl: separate, Shift: range), choose copy from, "
        "then Copy. Only selected cells are changed.\n"
        "Switching modules reads device values. Use Write module before switching to keep edits.");
    copyHint->setWordWrap(true);
    v->addWidget(copyHint);
    auto *buttons = new QHBoxLayout;
    v->addLayout(buttons);
    auto *read = new QPushButton("Read module"), *write = new QPushButton("Write module"),
         *copyModule = new QPushButton("Copy from module ...");
    read->setObjectName("ReadModuleParameters");
    write->setObjectName("WriteModuleParameters");
    read->setToolTip("Read all 32 channels of the currently displayed module");
    write->setToolTip("Validate and write all 32 channels of the currently displayed module. "
                      "Other modules are not written.");
    copyModule->setObjectName("CopyModuleParameters");
    copyModule->setToolTip("Read another module and copy its 32 channels into this editor. "
                          "Use Write module to apply them to the displayed module.");
    for (auto *b : {read, write, copyModule})
        buttons->addWidget(b);
    connect(read, &QPushButton::clicked, this, [this] {
        try {
            acq->refreshModule(selected);
        } catch (const std::exception &e) {
            error(this, e);
        }
    });
    connect(write, &QPushButton::clicked, this, [this] {
        try {
            acq->applyModule(selected, editedModuleSettings());
        } catch (const std::exception &e) {
            const QString text = QString("No registers were written. %1\n"
                "Write module checks the 32 channels of the displayed module only. "
                "Editor values have been kept; correct the selected parameter and retry.")
                                     .arg(QString::fromUtf8(e.what()));
            message->setText(text);
            emit parameterMessage(text);
            QMessageBox::warning(this, "R5560", text);
        }
    });
    connect(copy, &QPushButton::clicked, this, &BasicSettings::copyParameters);
    connect(copyModule, &QPushButton::clicked, this, [this] {
        try {
            QStringList options;
            QVector<int> sources;
            for (int i = 0; i < edited.size(); ++i) {
                if (i == selected)
                    continue;
                options << QString("Mod %1").arg(i);
                sources.append(i);
            }
            if (sources.isEmpty()) {
                message->setText("Connect another module to copy its channel settings.");
                return;
            }
            bool ok = false;
            auto chosen = QInputDialog::getItem(this, "Copy from module",
                                                QString("Source (copies into Mod %1 editor; "
                                                        "Write module applies to hardware)").arg(selected),
                                                options, 0, false, &ok);
            if (!ok)
                return;
            copySourceModule = sources.at(options.indexOf(chosen));
            acq->refreshModule(copySourceModule);
        } catch (const std::exception &e) {
            copySourceModule = -1;
            error(this, e);
        }
    });
    connect(modules, &QToolBox::currentChanged, this, [this](int index) {
        if (loading || index < 0)
            return;
        if (!acq->idle()) {
            QSignalBlocker blocker(modules);
            modules->setCurrentIndex(selected);
            return;
        }
        try {
            // Follow GUI27xx: each selection reads that DAQ. Unwritten editor
            // values are not carried into another module or auto-written.
            edited[selected] = acq->snapshot(selected).settings;
            display(index);
            message->setText(QString("Reading Mod %1 from device...").arg(index));
            acq->refreshModule(index);
        } catch (const std::exception &e) {
            QSignalBlocker blocker(modules);
            modules->setCurrentIndex(selected);
            error(this, e);
        }
    });
    connect(table, &QTableWidget::itemChanged, this, [this] {
        if (!loading)
            message->setText("Edited locally. Write module applies to this DAQ and checks readback; "
                             "File > Save stores the editor values.");
    });
    auto *copyAction = new QAction(this);
    copyAction->setShortcut(QKeySequence::Copy);
    table->addAction(copyAction);
    connect(copyAction, &QAction::triggered, this, &BasicSettings::copyCells);
    auto *pasteAction = new QAction(this);
    pasteAction->setShortcut(QKeySequence::Paste);
    table->addAction(pasteAction);
    connect(pasteAction, &QAction::triggered, this, &BasicSettings::pasteCells);
    connect(acq, &daq::Acquisition::settingsChanged, this, &BasicSettings::reload);
    connect(acq, &daq::Acquisition::moduleSettingsChanged, this, &BasicSettings::reloadModule);
    connect(acq, &daq::Acquisition::stateChanged, this,
            [this](const QString &state) {
                editArea->setEnabled(acq->idle());
                modules->setEnabled(acq->idle());
                if (state == "Fault")
                    copySourceModule = -1;
            });
    reload();
}
void BasicSettings::reload() {
    loading = true;
    copySourceModule = -1;
    setWindowTitle("Basic Settings");
    edited = acq->settings();
    while (modules->count()) {
        QWidget *w = modules->widget(0);
        modules->removeItem(0);
        delete w;
    }
    for (int i = 0; i < edited.size(); ++i) {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        auto ep = acq->config().endpoints[i];
        layout->addWidget(new QLabel(
            QString(
                "Family: R5560\nDAQ ID: %1\nIP: %2\nChannels: 32\nClock: 125 MHz\nAPI: %4\n\n%3")
                .arg(ep.id)
                .arg(ep.ip)
                .arg(acq->simulated() ? "DEMO — simulated" :
                     acq->snapshot(i).dma ? "Avalon control / DMA events" : "Ethernet / Avalon")
                .arg(acq->snapshot(i).dma ? "3.0" : "2.0")));
        layout->addStretch();
        modules->addItem(page, QString("Mod %1").arg(i));
    }
    selected = edited.isEmpty() ? 0 : std::min(selected, int(edited.size()) - 1);
    if (!edited.isEmpty())
        modules->setCurrentIndex(selected);
    loading = false;
    if (!edited.isEmpty())
        display(selected);
    showModuleStatus();
    editArea->setEnabled(acq->idle());
    modules->setEnabled(acq->idle());
}
void BasicSettings::reloadModule(int index) {
    if (index < 0 || index >= edited.size())
        return;
    edited[index] = acq->snapshot(index).settings;
    if (index == copySourceModule) {
        copySourceModule = -1;
        edited[selected].channels = edited[index].channels;
        edited[selected].channelMask = edited[index].channelMask;
        display(selected);
        message->setText(QString("Copied Mod %1 channel settings into Mod %2 editor. "
                                 "Write module applies them to Mod %2 only.").arg(index).arg(selected));
    } else if (index == selected) {
        display(selected);
        showModuleStatus();
    }
}
void BasicSettings::showModuleStatus() {
    if (edited.isEmpty())
        return;
    try {
        daq::validateModuleSettings(edited[selected], selected);
        message->setText(QString("Mod %1 device settings loaded. Read module / Write module "
                                 "operate on these 32 channels only.").arg(selected));
    } catch (const daq::Error &e) {
        message->setText(QString("Device settings need configuration: %1\n"
            "Fill a complete source row and copy to the selected channel rows, then Write module.")
                .arg(QString::fromUtf8(e.what())));
    }
}
void BasicSettings::display(int index) {
    selected = index;
    setWindowTitle(QString("Basic Settings - Mod %1").arg(index));
    loading = true;
    table->clearSelection();
    const auto &s = edited[index];
    for (int ch = 0; ch < daq::Channels; ++ch) {
        auto *enable = new QTableWidgetItem;
        enable->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        enable->setCheckState(s.channelMask & (1u << ch) ? Qt::Checked : Qt::Unchecked);
        table->setItem(ch, 0, enable);
        for (int p = 0; p < daq::ParameterCount; ++p)
            setCell(table, ch, p + 1, QString::number(s.channels[ch][p]));
    }
    loading = false;
}
void BasicSettings::store() {
    if (edited.isEmpty())
        return;
    auto s = edited[selected];
    s.channelMask = 0;
    for (int ch = 0; ch < daq::Channels; ++ch) {
        if (table->item(ch, 0)->checkState() == Qt::Checked)
            s.channelMask |= 1u << ch;
        for (int p = 0; p < daq::ParameterCount; ++p)
            s.channels[ch][p] =
                daq::parseUInt(table->item(ch, p + 1)->text(),
                               QString("Mod %1 Ch %2 %3")
                                   .arg(selected).arg(ch).arg(daq::Parameters[p].name));
    }
    // Keep incomplete edits while navigating/copying between modules. Range
    // validation belongs at the write/save/start boundary, not on tab changes.
    edited[selected] = s;
}
QVector<daq::ModuleSettings> BasicSettings::editedSettings() {
    store();
    try {
        daq::validateSettings(edited);
    } catch (const daq::SettingsError &e) {
        focusInvalidSetting(e);
        throw;
    }
    return edited;
}
daq::ModuleSettings BasicSettings::editedModuleSettings() {
    store();
    try {
        daq::validateModuleSettings(edited[selected], selected);
    } catch (const daq::SettingsError &e) {
        focusInvalidSetting(e);
        throw;
    }
    return edited[selected];
}
void BasicSettings::focusInvalidSetting(const daq::SettingsError &e) {
    if (e.module < 0 || e.module >= edited.size())
        return;
    if (selected != e.module) {
        // store() already captured the visible module. Do not run the tab-change
        // handler again or reload from hardware and overwrite those draft values.
        QSignalBlocker blocker(modules);
        modules->setCurrentIndex(e.module);
        display(e.module);
    }
    if (e.channel >= 0 && e.channel < daq::Channels &&
        e.parameter >= 0 && e.parameter < daq::ParameterCount) {
        table->setCurrentCell(e.channel, e.parameter + 1, QItemSelectionModel::ClearAndSelect);
        table->scrollToItem(table->currentItem(), QAbstractItemView::PositionAtCenter);
        table->setFocus();
    }
}
void BasicSettings::copyParameters() {
    if (!acq->idle() || edited.isEmpty())
        return;
    const auto destinations = table->selectedItems();
    if (destinations.isEmpty()) {
        message->setText("Select destination rows or cells first. Ctrl selects separate channels; "
                         "Shift selects a range.");
        return;
    }
    const int source = copyFrom->currentIndex();
    // Snapshot editor values, including unapplied edits and the enable checkbox.
    QStringList values;
    for (int col = 0; col < table->columnCount(); ++col)
        values.append(table->item(source, col)->text());
    const auto enabled = table->item(source, 0)->checkState();
    QSignalBlocker blocker(table);
    QSet<int> channels;
    int cells = 0;
    for (auto *item : destinations) {
        if (item->row() == source)
            continue;
        if (item->column() == 0)
            item->setCheckState(enabled);
        else
            item->setText(values[item->column()]);
        channels.insert(item->row());
        ++cells;
    }
    if (cells == 0) {
        message->setText("Only the source channel is selected. Select destination rows or cells.");
        return;
    }
    message->setText(QString("Copied Ch %1 to %2 selected channel(s), %3 cell(s), in Mod %4. "
                             "Write module applies and verifies the settings.")
                         .arg(source)
                         .arg(channels.size())
                         .arg(cells)
                         .arg(selected));
}
void BasicSettings::copyCells() {
    auto ranges = table->selectedRanges();
    if (ranges.isEmpty())
        return;
    auto r = ranges.first();
    QStringList rows;
    for (int ch = r.topRow(); ch <= r.bottomRow(); ++ch) {
        QStringList cells;
        for (int c = r.leftColumn(); c <= r.rightColumn(); ++c)
            cells << (c == 0 ? (table->item(ch, c)->checkState() == Qt::Checked ? "1" : "0")
                             : table->item(ch, c)->text());
        rows << cells.join('\t');
    }
    QApplication::clipboard()->setText(rows.join('\n'));
}
void BasicSettings::pasteCells() {
    if (!acq->idle())
        return;
    int row = table->currentRow(), col = table->currentColumn();
    if (row < 0 || col < 0)
        return;
    auto lines = QApplication::clipboard()->text().trimmed().split('\n');
    for (int r = 0; r < lines.size() && row + r < 32; ++r) {
        auto values = lines[r].split('\t');
        for (int c = 0; c < values.size() && col + c < table->columnCount(); ++c) {
            if (col + c == 0)
                table->item(row + r, 0)
                    ->setCheckState(values[c].trimmed() == "1" ? Qt::Checked : Qt::Unchecked);
            else
                table->item(row + r, col + c)->setText(values[c].trimmed());
        }
    }
}

LogicSettings::LogicSettings(daq::Acquisition *a, QWidget *parent)
    : QMainWindow(parent, Qt::Window), acq(a) {
    setWindowTitle("Logic Settings");
    setObjectName("LogicSettings");
    resize(820, 600);
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *v = new QVBoxLayout(central);
    auto *tabs = new QTabWidget;
    v->addWidget(tabs);
    auto *run = new QWidget;
    tabs->addTab(run, "Run / Sync");
    auto *layout = new QVBoxLayout(run);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    mode = new QComboBox;
    mode->addItems({"External shared RUN (multi-module)", "Software start (one module only)"});
    runInput = new QComboBox;
    t0Input = new QComboBox;
    runInput->addItems({"SYNC 0", "SYNC 1", "SYNC 2"});
    runInput->setToolTip("R5560 baseboard sync_reg.1 drives SYNC 1. Use SYNC 1 for multiple DAQs.");
    t0Input->addItems({"SYNC 0", "SYNC 1", "SYNC 2"});
    t0Enabled = new QCheckBox("Capture T0 marker (does not reset event timestamp)");
    form->addRow("Start source", mode);
    form->addRow("RUN input", runInput);
    form->addRow("T0 input", t0Input);
    form->addRow("T0", t0Enabled);
    auto *note = new QLabel(
        "Before Start: distribute one common 125 MHz reference clock and shared RUN to every DAQ. "
        "With multiple DAQs, Mod 0 sets baseboard sync_reg.1 LOW before ARM, HIGH after all DAQs "
        "are ARMED, and LOW on Stop. Select RUN input SYNC 1 on every DAQ. "
        "Leave T0 disabled unless a separate marker is wired. In the BASEBOARD web I/O Settings, "
        "route sync_reg.1 (SYNC REG 2 in the older one-based source menu) to DAQ Sync In lane 1 "
        "(the second lane, indices 0/1/2). "
        "Changing SYNC REG does not configure this route.\nStop requests every DAQ to stop, then drains pending "
        "events. Hardware RUN falling can also end acquisition.");
    note->setWordWrap(true);
    layout->addWidget(note);
    apply = new QPushButton("Write All");
    layout->addWidget(apply);
    state = new QLabel;
    layout->addWidget(state);
    simulate = new QPushButton("Simulate shared RUN (DEMO only)");
    layout->addWidget(simulate);
    status = new QTableWidget;
    status->setColumnCount(7);
    status->setHorizontalHeaderLabels(
        {"Module / DAQ ID", "State", "Clock", "Errors", "Starts", "T0 count", "Last T0 (ticks)"});
    status->setEditTriggers(QAbstractItemView::NoEditTriggers);
    status->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(status);
    connect(apply, &QPushButton::clicked, this, [this] {
        try {
            auto s = acq->settings();
            quint32 value = mode->currentIndex() == 0 ? 1 : 64;
            value |= quint32(runInput->currentIndex()) << 1;
            value |= quint32(t0Input->currentIndex()) << 3;
            if (t0Enabled->isChecked())
                value |= 32;
            for (auto &m : s)
                m.syncConfig = value;
            acq->apply(s);
        } catch (const std::exception &e) {
            error(this, e);
        }
    });
    connect(simulate, &QPushButton::clicked, this, [this] {
        try {
            acq->simulateRunEdge();
        } catch (const std::exception &e) {
            error(this, e);
        }
    });
    connect(acq, &daq::Acquisition::settingsChanged, this, &LogicSettings::reload);
    connect(acq, &daq::Acquisition::moduleSettingsChanged, this, &LogicSettings::reload);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &LogicSettings::updateStatus);
    timer->start(250);
    reload();
}
void LogicSettings::reload() {
    auto s = acq->settings();
    if (!s.isEmpty()) {
        auto c = s[0].syncConfig;
        mode->setCurrentIndex(c & 1 ? 0 : 1);
        runInput->setCurrentIndex(int((c >> 1) & 3));
        t0Input->setCurrentIndex(int((c >> 3) & 3));
        t0Enabled->setChecked(c & 32);
    }
    updateStatus();
}
void LogicSettings::updateStatus() {
    apply->setEnabled(acq->idle());
    mode->setEnabled(acq->idle());
    runInput->setEnabled(acq->idle());
    t0Input->setEnabled(acq->idle());
    t0Enabled->setEnabled(acq->idle());
    simulate->setVisible(acq->simulated());
    simulate->setEnabled(acq->state() == "Armed" && !acq->controlsBaseboardRun());
    state->setText("Crate: " + acq->state());
    status->setRowCount(acq->count());
    for (int i = 0; i < acq->count(); ++i) {
        auto s = acq->snapshot(i);
        setCell(status, i, 0, QString("Mod %1 / %2").arg(i).arg(acq->config().endpoints[i].id));
        setCell(status, i, 1, daq::statusText(s.status));
        setCell(status, i, 2, s.status & daq::ClockGood ? "Locked" : "NOT LOCKED");
        setCell(status, i, 3, QString("0x%1").arg(s.errors, 8, 16, QChar('0')));
        setCell(status, i, 4, QString::number(s.startCount));
        setCell(status, i, 5, QString::number(s.t0Count));
        setCell(status, i, 6, s.t0Count ? QString::number(s.t0Ticks) : QString("—"));
    }
}

OnlineRate::OnlineRate(daq::Acquisition *a, QWidget *parent)
    : QMainWindow(parent, Qt::Window), acq(a) {
    setWindowTitle("Online Rate");
    setObjectName("OnlineRate");
    resize(1320, 800);
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *v = new QVBoxLayout(central);
    auto *selectors = new QHBoxLayout;
    v->addLayout(selectors);
    module = new QComboBox;
    module->setObjectName("OnlineModule");
    channel = new QComboBox;
    channel->setObjectName("OnlineSpectrumChannel");
    for (int ch = 0; ch < 32; ++ch)
        channel->addItem(QString("Ch %1").arg(ch));
    selectors->addWidget(new QLabel("Module"));
    selectors->addWidget(module);
    selectors->addWidget(new QLabel("Spectrum"));
    selectors->addWidget(channel);
    selectors->addStretch();
    totals = new QLabel;
    totals->setObjectName("OnlineTotals");
    totals->setWordWrap(true);
    v->addWidget(totals);
    dmaDiagnostics = new QLabel;
    dmaDiagnostics->setObjectName("OnlineDmaDiagnostics");
    dmaDiagnostics->setWordWrap(true);
    dmaDiagnostics->setToolTip(
        "DMA FIFO peak is its maximum occupancy in 32-byte events, with the configured depth. "
        "Longest stall counts consecutive clocks with valid data and no downstream ready; "
        "it includes an ongoing stall. ARM clears these values; STOP preserves them.");
    v->addWidget(dmaDiagnostics);
    diagnosticsStatus = new QLabel;
    diagnosticsStatus->setObjectName("OnlineDiagnosticsStatus");
    diagnosticsStatus->setWordWrap(true);
    v->addWidget(diagnosticsStatus);
    auto *tabs = new QTabWidget;
    v->addWidget(tabs);
    table = new QTableWidget(32, 10);
    table->setObjectName("OnlineChannelRates");
    table->setHorizontalHeaderLabels(
        {"Channel", "Input / s", "Output / s", "Accepted", "Rejected", "Received", "Reject %",
         "Processing busy\nrejected", "FIFO busy\nrejected", "FIFO peak / depth\n(events)"});
    table->horizontalHeaderItem(4)->setToolTip(
        "Cumulative rejected triggers (32-bit, wraps at 2^32). With diagnostics, the total is "
        "Processing busy rejected + FIFO busy rejected modulo 2^32. The categories are exclusive; "
        "when both are busy, the rejection is assigned to FIFO busy.");
    table->horizontalHeaderItem(7)->setToolTip(
        "Cumulative rejected triggers while event processing is busy and the downstream FIFO "
        "is not busy. 32-bit counter; reset on ARM, retained after STOP. N/A means unavailable.");
    table->horizontalHeaderItem(8)->setToolTip(
        "Cumulative rejected triggers while the downstream FIFO is busy, including when processing "
        "is also busy. 32-bit counter; reset on ARM, retained after STOP. N/A means unavailable.");
    table->horizontalHeaderItem(9)->setToolTip(
        "Maximum channel FIFO occupancy / configured depth, in complete 32-byte events. "
        "Reset on ARM, retained after STOP. This is not the DDR buffer occupancy.");
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setMinimumSectionSize(80);
    tabs->addTab(table, "Rate");
    auto *page = new QWidget;
    tabs->addTab(page, "Energy Spectrum");
    auto *pv = new QVBoxLayout(page);
    auto *plot = new Spectrum;
    spectrum = plot;
    pv->addWidget(plot);
    auto *log = new QCheckBox("Log Y");
    pv->addWidget(log);
    connect(log, &QCheckBox::toggled, plot, [plot](bool on) {
        plot->logarithmic = on;
        plot->update();
    });
    auto *exportButton = new QPushButton("Export spectrum CSV");
    pv->addWidget(exportButton);
    connect(exportButton, &QPushButton::clicked, this, [this, plot] {
        auto file =
            QFileDialog::getSaveFileName(this, "Export spectrum", "spectrum.csv", "CSV (*.csv)");
        if (file.isEmpty())
            return;
        QSaveFile f(file);
        if (!f.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, "Export", f.errorString());
            return;
        }
        QByteArray b("energy_low,energy_high,counts\n");
        for (int i = 0; i < 512; ++i)
            b += QByteArray::number(i * 32) + "," + QByteArray::number((i + 1) * 32) + "," +
                 QByteArray::number(plot->bins[i]) + "\n";
        if (f.write(b) != b.size() || !f.commit())
            QMessageBox::warning(this, "Export", f.errorString());
    });
    auto *note =
        new QLabel("Energy uses the GUI27xx UserDPP reconstruction formula with R5560 24-bit sums. "
                   "Verify polarity, Tau and calibration with a pulser before physics use. Raw "
                   "integrals are always preserved in the data file.");
    note->setWordWrap(true);
    pv->addWidget(note);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &OnlineRate::updateData);
    connect(module, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { updateData(); });
    connect(channel, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { updateData(); });
    timer->start(500);
    updateData();
}
void OnlineRate::updateData() {
    if (module->count() != acq->count()) {
        const QSignalBlocker blocker(module);
        const int previous = module->currentIndex();
        module->clear();
        for (int i = 0; i < acq->count(); ++i)
            module->addItem(QString("Mod %1").arg(i));
        if (module->count())
            module->setCurrentIndex(std::min(std::max(0, previous), module->count() - 1));
    }
    if (acq->count() == 0) {
        totals->setText("No modules connected");
        dmaDiagnostics->setText("DMA FIFO peak: N/A   Longest DMA stall: N/A");
        diagnosticsStatus->setText("Diagnostics: N/A (no module selected)");
        diagnosticsStatus->setStyleSheet({});
        for (int c = 0; c < daq::Channels; ++c) {
            setCell(table, c, 0, QString::number(c));
            for (int column = 1; column < table->columnCount(); ++column)
                setCell(table, c, column, "N/A");
        }
        auto *plot = static_cast<Spectrum *>(spectrum);
        plot->bins.fill(0);
        plot->update();
        return;
    }
    int mod = std::max(0, module->currentIndex());
    displaySnapshot(acq->snapshot(mod));
}
void OnlineRate::displaySnapshot(const daq::Snapshot &s) {
    auto ch = channel->currentIndex();
    totals->setText(QString("%1   %2 events   %3 MB   %4 MB/s   Spectrum under/over: %5 / %6")
                        .arg(daq::statusText(s.status))
                        .arg(s.events)
                        .arg(s.bytes / 1e6, 0, 'f', 2)
                        .arg(s.megabytesPerSecond, 0, 'f', 2)
                        .arg(s.underflow[ch])
                        .arg(s.overflow[ch]));
    const bool supported = s.diagnosticsVersion == R5560_DIAG_API;
    const bool available = s.diagnosticsAvailable();
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const qint64 age = s.diagnosticsReadMs > 0 ? std::max(qint64(0), now - s.diagnosticsReadMs) : -1;
    const bool stale = available && (!s.connected || age < 0 || age > 5000);
    QString status;
    if (!supported) {
        status = QString("Diagnostics: N/A (firmware version 0x%1 is unsupported or not yet read)")
                     .arg(s.diagnosticsVersion, 8, 16, QChar('0'));
    } else if (!available) {
        status = "Diagnostics: N/A (waiting for a complete snapshot for the current ARM epoch)";
    } else {
        const QString readCompleted = s.diagnosticsReadMs > 0
            ? QDateTime::fromMSecsSinceEpoch(s.diagnosticsReadMs, Qt::UTC)
                  .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'")
            : QString("unknown time");
        status = QString("Diagnostics: %1  |  read completed %2  |  elapsed %3  |  snapshot %4, start %5")
                     .arg(stale ? (s.connected ? "STALE (last known values)"
                                              : "STALE / disconnected (last known values)")
                                : "current")
                     .arg(readCompleted)
                     .arg(age >= 0 ? QString::number(age / 1000.0, 'f', 1) + " s" : "N/A")
                     .arg(s.diagnosticsSequence).arg(s.diagnosticsStartCount);
    }
    diagnosticsStatus->setText(status);
    diagnosticsStatus->setStyleSheet(stale ? "color: #9a6700;" : "");
    diagnosticsStatus->setToolTip(
        "Diagnostics are a hardware snapshot. The UTC time is when the complete register read "
        "finished, not the exact hardware sampling time. More than 5 seconds since the last "
        "successful read, or disconnection, marks values STALE. Rejected columns are cumulative "
        "counts, not rates. All peaks and the longest stall clear on ARM and remain after STOP.");
    QString dmaPeak = "N/A", stall = "N/A";
    if (available) {
        if (s.dmaFifoDepth)
            dmaPeak = QString("%1 / %2 events").arg(s.dmaFifoPeak).arg(s.dmaFifoDepth);
        if (s.clockHz) {
            const double microseconds = double(s.dmaStallMaxCycles) * 1e6 / s.clockHz;
            stall = microseconds < 1000.0
                ? QString::number(microseconds, 'f', 3) + " us"
                : QString::number(microseconds / 1000.0, 'f', 3) + " ms";
            stall += QString(" (%1 cycles)").arg(s.dmaStallMaxCycles);
        } else {
            stall = QString("N/A (clock unavailable; %1 cycles)").arg(s.dmaStallMaxCycles);
        }
    }
    dmaDiagnostics->setText(QString("DMA FIFO peak: %1   Longest DMA stall: %2")
                                .arg(dmaPeak, stall));
    for (int c = 0; c < 32; ++c) {
        setCell(table, c, 0, QString::number(c));
        setCell(table, c, 1, QString::number(s.input[c]));
        setCell(table, c, 2, QString::number(s.output[c]));
        setCell(table, c, 3, QString::number(s.accepted[c]));
        setCell(table, c, 4, QString::number(s.rejected[c]));
        setCell(table, c, 5, QString::number(s.received[c]));
        double n = double(s.accepted[c]) + s.rejected[c];
        setCell(table, c, 6, n ? QString::number(100.0 * s.rejected[c] / n, 'f', 2) : "0.00");
        setCell(table, c, 7, available ? QString::number(s.rejectedProcessing[c]) : "N/A");
        setCell(table, c, 8, available ? QString::number(s.rejectedFifo[c]) : "N/A");
        setCell(table, c, 9, available && s.channelFifoDepth
                    ? QString("%1 / %2").arg(s.channelFifoPeak[c]).arg(s.channelFifoDepth) : "N/A");
    }
    auto *plot = static_cast<Spectrum *>(spectrum);
    plot->bins = s.histogram[ch];
    plot->update();
}
