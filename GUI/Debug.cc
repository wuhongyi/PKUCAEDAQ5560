// Layout and Mod/ch/Sent interaction follow GUI27xx Debug.cc. The plot uses the
// existing Qt Widgets dependency so Linux users do not need an extra Qt Charts SDK.
#include "Debug.hh"
#include <algorithm>
#include <cmath>

WaveformPlot::WaveformPlot(QWidget *parent) : QWidget(parent) {
    setObjectName("WaveformPlot");
    setMinimumSize(460, 350);
    setMouseTracking(true);
}
QRectF WaveformPlot::plotRect() const {
    return QRectF(72, 30, width() - 98, height() - 100);
}
QPointF WaveformPlot::toData(const QPointF &p) const {
    auto r = plotRect();
    return {xLow + (p.x() - r.left()) / r.width() * (xHigh - xLow),
            yHigh - (p.y() - r.top()) / r.height() * (yHigh - yLow)};
}
void WaveformPlot::setWaveform(const daq::Waveform &w) {
    if (w.module != wave.module || w.channel != wave.channel ||
        w.samples.size() != wave.samples.size() || w.pretrigger != wave.pretrigger)
        zoomed = false;
    wave = w;
    if (!zoomed)
        autoScale();
    update();
}
void WaveformPlot::setBaseline(int first, int count) {
    baselineFirst = first;
    baselineCount = count;
    update();
}
void WaveformPlot::autoScale() {
    zoomed = false;
    xLow = 0;
    xHigh = std::max(1, int(wave.samples.size()) - 1);
    if (!wave.samples.isEmpty()) {
        auto extrema = std::minmax_element(wave.samples.cbegin(), wave.samples.cend());
        double margin = std::max(5.0, (*extrema.second - *extrema.first) * 0.08);
        yLow = *extrema.first - margin;
        yHigh = *extrema.second + margin;
    }
    update();
}
void WaveformPlot::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    auto r = plotRect();
    auto x = [&](double v) { return r.left() + (v - xLow) / (xHigh - xLow) * r.width(); };
    auto y = [&](double v) { return r.bottom() - (v - yLow) / (yHigh - yLow) * r.height(); };
    p.setPen(QColor(60, 60, 60));
    p.drawText(QRectF(0, 4, width(), 22), Qt::AlignCenter,
               wave.samples.isEmpty() ? "Waveform / 125 MS/s / 14 bit"
                                      : QString("Mod %1  ch-%2  |  %3 samples  |  %4")
                                            .arg(wave.module)
                                            .arg(wave.channel, 2, 10, QChar('0'))
                                            .arg(wave.samples.size())
                                            .arg(wave.simulated ? "DEMO" : "ADC"));
    for (int i = 0; i <= 5; ++i) {
        double xx = r.left() + r.width() * i / 5, yy = r.top() + r.height() * i / 5;
        p.setPen(QColor(225, 229, 235));
        p.drawLine(QPointF(xx, r.top()), QPointF(xx, r.bottom()));
        p.drawLine(QPointF(r.left(), yy), QPointF(r.right(), yy));
        p.setPen(Qt::darkGray);
        p.drawText(
            QRectF(xx - 42, r.bottom() + 5, 84, 20), Qt::AlignCenter,
            QString::number((xLow + (xHigh - xLow) * i / 5 - wave.pretrigger) * 0.008, 'f', 2));
        p.drawText(QRectF(0, yy - 10, 65, 20), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(yHigh - (yHigh - yLow) * i / 5, 'f', 0));
    }
    p.drawRect(r);
    p.drawText(QRectF(0, height() - 28, width(), 20), Qt::AlignCenter,
               "Time relative to trigger (us)");
    p.save();
    p.translate(15, r.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-110, -12, 220, 24), Qt::AlignCenter, "ADC counts");
    p.restore();
    p.save();
    p.setClipRect(r);
    if (!wave.samples.isEmpty()) {
        p.fillRect(QRectF(QPointF(x(baselineFirst), r.top()),
                          QPointF(x(baselineFirst + baselineCount), r.bottom())),
                   QColor(50, 180, 120, 25));
        const int first = std::max(0, int(std::floor(xLow)));
        const int last = std::min(int(wave.samples.size()) - 1, int(std::ceil(xHigh)));
        p.setPen(QPen(QColor(32, 93, 185), 1));
        // Keep every spike visible when more than one sample maps to a pixel.
        int step = std::max(1, int((xHigh - xLow) / r.width()));
        if (step <= 1) {
            QPainterPath path;
            for (int i = first; i <= last; ++i) {
                QPointF point(x(i), y(wave.samples[i]));
                if (i == first)
                    path.moveTo(point);
                else
                    path.lineTo(point);
            }
            p.drawPath(path);
            if (first == last)
                p.drawEllipse(QPointF(x(first), y(wave.samples[first])), 2, 2);
        } else {
            for (int i = first; i <= last; i += step) {
                auto begin = wave.samples.cbegin() + i;
                auto end = wave.samples.cbegin() + std::min(last + 1, i + step);
                auto mm = std::minmax_element(begin, end);
                p.drawLine(QPointF(x(i), y(*mm.first)), QPointF(x(i), y(*mm.second) + 0.5));
            }
        }
        p.setPen(QPen(QColor(205, 60, 55), 1, Qt::DashLine));
        p.drawLine(QPointF(x(wave.pretrigger), r.top()), QPointF(x(wave.pretrigger), r.bottom()));
        p.setPen(QPen(QColor(205, 60, 55), 2));
        for (int i = first; i <= last; ++i)
            if (wave.triggers.value(i))
                p.drawLine(QPointF(x(i), r.bottom() - 12), QPointF(x(i), r.bottom()));
    } else {
        p.setPen(Qt::gray);
        p.drawText(r.adjusted(25, 20, -25, -20), Qt::AlignCenter | Qt::TextWordWrap,
                   "Select Mod and channel, then Sent.\nForce captures a baseline without waiting "
                   "for a channel trigger.");
    }
    if (dragging) {
        p.setPen(QPen(Qt::darkBlue, 1, Qt::DashLine));
        p.drawRect(QRectF(dragStart, dragEnd).normalized());
    }
    p.restore();
}
void WaveformPlot::wheelEvent(QWheelEvent *e) {
    if (wave.samples.isEmpty())
        return;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    auto point = e->position();
#else
    auto point = e->posF();
#endif
    if (!plotRect().contains(point))
        return;
    auto center = toData(point);
    double factor = e->angleDelta().y() > 0 ? 0.8 : 1.25;
    if (e->modifiers() & Qt::ShiftModifier) {
        yLow = center.y() + (yLow - center.y()) * factor;
        yHigh = center.y() + (yHigh - center.y()) * factor;
        if (yHigh - yLow < 1)
            yHigh = yLow + 1;
    } else {
        xLow = std::max(0.0, center.x() + (xLow - center.x()) * factor);
        xHigh = std::min(double(std::max(1, int(wave.samples.size()) - 1)),
                         center.x() + (xHigh - center.x()) * factor);
        if (xHigh - xLow < 1)
            xHigh = xLow + 1;
    }
    zoomed = true;
    update();
    e->accept();
}
void WaveformPlot::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::RightButton) {
        autoScale();
        return;
    }
    if (e->button() == Qt::LeftButton && plotRect().contains(e->pos()) && !wave.samples.isEmpty()) {
        dragging = true;
        dragStart = dragEnd = e->pos();
    }
}
void WaveformPlot::mouseMoveEvent(QMouseEvent *e) {
    if (dragging) {
        dragEnd = e->pos();
        update();
    }
    if (wave.samples.isEmpty() || !plotRect().contains(e->pos()))
        return;
    int i = std::clamp(int(std::round(toData(e->pos()).x())), 0, int(wave.samples.size()) - 1);
    emit cursorText(QString("Sample %1 | t = %2 ns | ADC = %3 | trigger = %4")
                        .arg(i)
                        .arg((i - wave.pretrigger) * 8)
                        .arg(wave.samples[i])
                        .arg(wave.triggers[i]));
}
void WaveformPlot::mouseReleaseEvent(QMouseEvent *e) {
    if (!dragging)
        return;
    dragging = false;
    auto box = QRectF(dragStart, QPointF(e->pos())).normalized().intersected(plotRect());
    if (box.width() > 8 && box.height() > 8) {
        auto a = toData(box.topLeft()), b = toData(box.bottomRight());
        xLow = a.x();
        xHigh = std::max(a.x() + 1, b.x());
        yLow = b.y();
        yHigh = std::max(b.y() + 1, a.y());
        zoomed = true;
    }
    update();
}
void WaveformPlot::mouseDoubleClickEvent(QMouseEvent *) {
    autoScale();
}

Debug::Debug(daq::Acquisition *a, QWidget *parent) : QMainWindow(parent, Qt::Window), acq(a) {
    setObjectName("Debug");
    setWindowTitle("Online Debug / R5560 Waveform");
    resize(1120, 760);
    auto *tabs = new QTabWidget;
    setCentralWidget(tabs);
    auto *split = new QSplitter;
    tabs->addTab(split, "Waveform");
    auto *left = new QWidget;
    left->setMaximumWidth(290);
    left->setMinimumWidth(235);
    auto *layout = new QVBoxLayout(left);
    modules = new QToolBox;
    modules->setObjectName("DebugModules");
    layout->addWidget(modules, 1);
    auto *source = new QGroupBox("Online Sources");
    layout->addWidget(source);
    auto *form = new QFormLayout(source);
    channel = new QComboBox;
    channel->setObjectName("DebugChannel");
    for (int ch = 0; ch < 32; ++ch)
        channel->addItem(QString("ch-%1").arg(ch, 2, 10, QChar('0')));
    form->addRow("Channel", channel);
    length = new QSpinBox;
    length->setObjectName("WaveLength");
    length->setRange(1, 16384);
    length->setValue(4096);
    pretrigger = new QSpinBox;
    pretrigger->setObjectName("WavePretrigger");
    pretrigger->setRange(0, 4095);
    pretrigger->setValue(1024);
    timeout = new QSpinBox;
    timeout->setRange(1, 60);
    timeout->setValue(5);
    timeout->setSuffix(" s");
    form->addRow("Samples", length);
    form->addRow("Pretrigger", pretrigger);
    form->addRow("Timeout", timeout);
    continuous = new QCheckBox("Continuous");
    continuous->setObjectName("WaveContinuous");
    form->addRow(continuous);
    auto *buttons = new QHBoxLayout;
    sent = new QPushButton("Sent");
    sent->setObjectName("WaveSent");
    sent->setToolTip("Capture on the selected user_ch trigger; no RUN is required");
    force = new QPushButton("Force");
    force->setObjectName("WaveForce");
    force->setToolTip("Software-triggered snapshot for baseline/noise diagnostics");
    stop = new QPushButton("Stop");
    stop->setObjectName("WaveStop");
    buttons->addWidget(sent);
    buttons->addWidget(force);
    buttons->addWidget(stop);
    form->addRow(buttons);
    auto *note = new QLabel(
        "ADC counts, 8 ns/sample\n16384 points = 131.072 us\nRed: trigger; green: baseline range");
    note->setWordWrap(true);
    form->addRow(note);
    auto *baseline = new QGroupBox("Baseline / Noise");
    layout->addWidget(baseline);
    auto *statsForm = new QFormLayout(baseline);
    baselineFirst = new QSpinBox;
    baselineFirst->setRange(0, 16383);
    baselineFirst->setObjectName("BaselineFirst");
    baselineCount = new QSpinBox;
    baselineCount->setRange(1, 16384);
    baselineCount->setValue(512);
    baselineCount->setObjectName("BaselineCount");
    statsForm->addRow("First sample", baselineFirst);
    statsForm->addRow("Samples", baselineCount);
    statistics = new QLabel("Capture a waveform first");
    statistics->setObjectName("WaveStatistics");
    statistics->setWordWrap(true);
    statistics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statsForm->addRow(statistics);
    split->addWidget(left);
    auto *right = new QWidget;
    auto *r = new QVBoxLayout(right);
    auto *tools = new QHBoxLayout;
    auto *scale = new QPushButton("Auto scale");
    save = new QPushButton("Save CSV");
    save->setObjectName("WaveSave");
    tools->addWidget(scale);
    tools->addWidget(save);
    tools->addStretch();
    r->addLayout(tools);
    plot = new WaveformPlot;
    r->addWidget(plot, 1);
    auto *help = new QLabel("Drag: zoom  |  Wheel: time zoom  |  Shift+wheel: ADC zoom  |  Right "
                            "click / double click: reset");
    help->setWordWrap(true);
    r->addWidget(help);
    info = new QLabel("Connect a DAQ or open Demo to capture a waveform.");
    info->setObjectName("WaveStatus");
    info->setWordWrap(true);
    r->addWidget(info);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    logView = new QPlainTextEdit;
    logView->setReadOnly(true);
    logView->setMaximumBlockCount(3000);
    tabs->addTab(logView, "Run log");
    connect(plot, &WaveformPlot::cursorText, this,
            [this](const QString &text) { statusBar()->showMessage(text); });
    connect(scale, &QPushButton::clicked, plot, &WaveformPlot::autoScale);
    connect(length, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int n) { pretrigger->setMaximum(n - 1); });
    connect(baselineFirst, qOverload<int>(&QSpinBox::valueChanged), this,
            [this] { updateStatistics(); });
    connect(baselineCount, qOverload<int>(&QSpinBox::valueChanged), this,
            [this] { updateStatistics(); });
    connect(sent, &QPushButton::clicked, this, [this] { capture(false); });
    connect(force, &QPushButton::clicked, this, [this] { capture(true); });
    connect(stop, &QPushButton::clicked, this, &Debug::cancel);
    nextCapture.setSingleShot(true);
    nextCapture.setInterval(250);
    connect(&nextCapture, &QTimer::timeout, this, [this] {
        if (continuous->isChecked() && isVisible())
            capture(repeatForce);
        else
            updateControls();
    });
    connect(continuous, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!enabled)
            nextCapture.stop();
        updateControls();
    });
    connect(acq, &daq::Acquisition::stateChanged, this, [this] {
        reloadModules();
        if (acq->state() != "Idle" && acq->state() != "Running")
            cancel();
        updateControls();
    });
    connect(acq, &daq::Acquisition::waveformBusyChanged, this, [this] { updateControls(); });
    connect(acq, &daq::Acquisition::waveformReady, this, [this](const daq::Waveform &w) {
        if (!capturing)
            return;
        capturing = false;
        wave = w;
        plot->setWaveform(w);
        {
            QSignalBlocker b(baselineFirst);
            baselineFirst->setMaximum(w.samples.size() - 1);
        }
        updateStatistics();
        info->setText(QString("%1Mod %2 ch-%3 | %4 samples | sequence %5 | %6")
                          .arg(w.simulated ? "DEMO / " : "")
                          .arg(w.module)
                          .arg(w.channel, 2, 10, QChar('0'))
                          .arg(w.samples.size())
                          .arg(w.sequence)
                          .arg(w.forced ? "Forced snapshot" : "user_ch trigger"));
        if (continuous->isChecked() && isVisible())
            nextCapture.start();
        updateControls();
    });
    connect(acq, &daq::Acquisition::waveformFailed, this, [this](const QString &reason) {
        capturing = false;
        nextCapture.stop();
        continuous->setChecked(false);
        info->setText(reason);
        updateControls();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        auto name =
            QString("wave_M%1_ch%2_%3.csv").arg(wave.module).arg(wave.channel).arg(wave.sequence);
        auto file = QFileDialog::getSaveFileName(this, "Save waveform", name, "CSV (*.csv)");
        if (file.isEmpty())
            return;
        try {
            wave.saveCsv(file);
            info->setText("Saved: " + file);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Waveform", e.what());
        }
    });
    reloadModules();
    updateControls();
}
void Debug::reloadModules() {
    if (modules->count() == acq->count())
        return;
    while (modules->count()) {
        auto *w = modules->widget(0);
        modules->removeItem(0);
        delete w;
    }
    for (int i = 0; i < acq->count(); ++i) {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);
        form->addRow("Family", new QLabel("R5560"));
        form->addRow("DAQ ID", new QLabel(QString::number(acq->config().endpoints[i].id)));
        auto *ip = new QLabel(acq->config().endpoints[i].ip);
        ip->setWordWrap(true);
        form->addRow("IP", ip);
        form->addRow("Sample", new QLabel("125 MS/s"));
        form->addRow("Bits", new QLabel("14"));
        form->addRow("Channels", new QLabel("32"));
        modules->addItem(page, QString("Mod %1").arg(i));
    }
}
void Debug::updateControls() {
    bool enabled = acq->state() == "Idle" || acq->state() == "Running";
    bool busy = acq->waveformBusy() || nextCapture.isActive();
    for (auto *w : QVector<QWidget *>{modules, channel, length, pretrigger, timeout, sent, force})
        w->setEnabled(enabled && !busy && acq->count() > 0);
    stop->setEnabled(capturing || nextCapture.isActive());
    continuous->setEnabled(enabled);
    save->setEnabled(!wave.samples.isEmpty());
}
void Debug::capture(bool forceTrigger) {
    try {
        daq::WaveformRequest request;
        request.channel = channel->currentIndex();
        request.length = length->value();
        request.pretrigger = pretrigger->value();
        request.timeoutMs = timeout->value() * 1000;
        request.force = forceTrigger;
        capturing = true;
        repeatForce = forceTrigger;
        acq->captureWaveform(modules->currentIndex(), request);
        info->setText(
            QString("Mod %1 ch-%2: %3")
                .arg(modules->currentIndex())
                .arg(request.channel, 2, 10, QChar('0'))
                .arg(forceTrigger ? "capturing baseline..." : "waiting for user_ch trigger..."));
    } catch (const std::exception &e) {
        capturing = false;
        nextCapture.stop();
        continuous->setChecked(false);
        info->setText(QString::fromUtf8(e.what()));
    }
    updateControls();
}
void Debug::cancel() {
    nextCapture.stop();
    continuous->setChecked(false);
    if (capturing)
        acq->cancelWaveform();
    updateControls();
}
void Debug::updateStatistics() {
    if (wave.samples.isEmpty())
        return;
    int first = baselineFirst->value();
    {
        QSignalBlocker block(baselineCount);
        baselineCount->setMaximum(wave.samples.size() - first);
    }
    int count = baselineCount->value();
    auto stats = daq::waveformStatistics(wave.samples, first, count);
    statistics->setText(QString("Mean: %1 ADC\nRMS noise: %2 ADC\nPeak-to-peak: %3 ADC\nMin / Max: "
                                "%4 / %5\nRange: [%6, %7]")
                            .arg(stats.mean, 0, 'f', 3)
                            .arg(stats.rmsNoise, 0, 'f', 3)
                            .arg(stats.maximum - stats.minimum)
                            .arg(stats.minimum)
                            .arg(stats.maximum)
                            .arg(first)
                            .arg(first + count - 1));
    plot->setBaseline(first, count);
}
void Debug::closeEvent(QCloseEvent *e) {
    cancel();
    e->accept();
}
