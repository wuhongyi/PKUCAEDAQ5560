#include "AnalysisDialog.hh"

AnalysisDialog::AnalysisDialog(QWidget *parent) : QDialog(parent, Qt::Window) {
    setWindowTitle("Offline analysis / Coincidence");
    resize(820, 530);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto pathRow = [&](const QString &label, bool save) {
        auto *widget = new QWidget;
        auto *row = new QHBoxLayout(widget);
        row->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit;
        auto *browse = new QPushButton("Browse");
        row->addWidget(edit);
        row->addWidget(browse);
        form->addRow(label, widget);
        connect(browse, &QPushButton::clicked, this, [this, edit, save] {
            auto path = save ? QFileDialog::getSaveFileName(this, "New CSV output", edit->text(),
                                                            "CSV (*.csv)")
                             : QFileDialog::getOpenFileName(this, "Module data file", edit->text(),
                                                            "R5560 list data (*.bin)");
            if (!path.isEmpty())
                edit->setText(path);
        });
        return edit;
    };
    fileA = pathRow("Module A file", false);
    fileB = pathRow("Module B file", false);
    output = pathRow("New CSV output", true);
    channelA = new QSpinBox;
    channelB = new QSpinBox;
    channelA->setRange(0, 31);
    channelB->setRange(0, 31);
    windowNs = new QSpinBox;
    windowNs->setRange(0, 1000000);
    windowNs->setValue(40);
    windowNs->setSuffix(" ns");
    offsetNs = new QSpinBox;
    offsetNs->setRange(-1000000000, 1000000000);
    offsetNs->setSuffix(" ns");
    form->addRow("Channel A", channelA);
    form->addRow("Channel B", channelB);
    form->addRow("Coincidence half-window", windowNs);
    form->addRow("Correction added to B time", offsetNs);
    auto *note =
        new QLabel("Coincidence: choose two modules from the same completed run. Every pair with "
                   "|(B + correction) − A| ≤ window is exported. Input is sorted on disk. Physical "
                   "clock/RUN synchronization and cable-delay calibration must be established "
                   "before interpreting coincidences. Python 3 is required.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    decode = new QPushButton("Decode A to CSV");
    coincidence = new QPushButton("Find coincidences");
    cancel = new QPushButton("Cancel analysis");
    cancel->setEnabled(false);
    buttons->addWidget(decode);
    buttons->addWidget(coincidence);
    buttons->addWidget(cancel);
    log = new QPlainTextEdit;
    log->setReadOnly(true);
    layout->addWidget(log);
    connect(decode, &QPushButton::clicked, this, [this] { launch(false); });
    connect(coincidence, &QPushButton::clicked, this, [this] { launch(true); });
    connect(cancel, &QPushButton::clicked, this, [this] {
        process.kill();
        log->appendPlainText(
            "Cancelled. Any new CSV output may be partial; use a new filename to retry.");
    });
    connect(&process, &QProcess::readyReadStandardOutput, this,
            [this] { log->appendPlainText(QString::fromUtf8(process.readAllStandardOutput())); });
    connect(&process, &QProcess::readyReadStandardError, this,
            [this] { log->appendPlainText(QString::fromUtf8(process.readAllStandardError())); });
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                log->appendPlainText(code == 0 && status == QProcess::NormalExit
                                         ? "Finished."
                                         : "Failed / cancelled; any new CSV may be partial.");
                decode->setEnabled(true);
                coincidence->setEnabled(true);
                cancel->setEnabled(false);
            });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        log->appendPlainText(process.errorString());
        decode->setEnabled(true);
        coincidence->setEnabled(true);
        cancel->setEnabled(false);
    });
}
AnalysisDialog::~AnalysisDialog() {
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
    }
}
void AnalysisDialog::launch(bool pairs) {
    if (process.state() != QProcess::NotRunning)
        return;
    auto python = QStandardPaths::findExecutable("python3");
    if (python.isEmpty())
        python = QStandardPaths::findExecutable("python");
    if (python.isEmpty()) {
        log->appendPlainText("Python 3 was not found in PATH.");
        return;
    }
    if (fileA->text().isEmpty() || output->text().isEmpty() || (pairs && fileB->text().isEmpty())) {
        log->appendPlainText("Choose input and output files first.");
        return;
    }
    QString script = scripts.filePath("r5560_data.py");
    if (!QFileInfo::exists(script) && !QFile::copy(":/tools/r5560_data.py", script)) {
        log->appendPlainText("Cannot extract the analysis script.");
        return;
    }
    QStringList args{script, pairs ? "coincidence" : "decode", fileA->text()};
    if (pairs)
        args << fileB->text();
    args << output->text();
    if (pairs)
        args << "--channel-a" << QString::number(channelA->value()) << "--channel-b"
             << QString::number(channelB->value()) << "--window-ns"
             << QString::number(windowNs->value()) << "--offset-ns"
             << QString::number(offsetNs->value());
    decode->setEnabled(false);
    coincidence->setEnabled(false);
    cancel->setEnabled(true);
    log->appendPlainText("Analyzing ... large runs use temporary disk space for sorting.");
    process.start(python, args);
}
