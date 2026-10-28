#include "MainWindow.hh"
#include <iostream>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("GDDAQ");
    QCoreApplication::setApplicationName("GDDAQ-R5560");
    QCoreApplication::setApplicationVersion("1.0.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("R5560 32-channel multi-module acquisition");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"demo", "Use a simulated crate"});
    parser.addOption({"modules", "Demo module count", "count", "3"});
    parser.addOption({"config", "Open a crate .dat configuration", "file"});
    parser.addOption({"fifo-kib", "Read block: 64/128/256 KiB (DMA: local slice; Avalon: request)", "size", "64"});
    parser.addOption({"smoke-test",
                      "Exercise demo connect/start/save/stop and capture windows in directory",
                      "directory"});
    parser.process(app);
    if (parser.isSet("smoke-test")) {
        const auto directory = QFileInfo(parser.value("smoke-test")).absoluteFilePath();
        QDir().mkpath(directory);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory);
        QCoreApplication::setApplicationName("GDDAQ-R5560-SelfTest");
        // Qt's Windows offscreen plugin does not enumerate the system fonts.
#ifdef Q_OS_WIN
        const int id = QFontDatabase::addApplicationFont(
            qEnvironmentVariable("SystemRoot", "C:/Windows") + "/Fonts/arial.ttf");
        if (id >= 0)
            app.setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(), 10));
#endif
        app.setStyle("Fusion");
    }
    MainWindow window;
    window.show();
    try {
        bool validSize = false;
        const int fifoKiB = parser.value("fifo-kib").toInt(&validSize);
        if (!validSize) throw daq::Error("Invalid FIFO request size");
        window.acquisition()->setReadoutBlockKiB(fifoKiB);
        window.setWindowTitle(QString("GDDAQ-R5560 — Read block %1 KiB").arg(fifoKiB));
        if (parser.isSet("smoke-test")) {
            QSettings::setDefaultFormat(QSettings::IniFormat);
            auto directory = QFileInfo(parser.value("smoke-test")).absoluteFilePath();
            QDir().mkpath(directory);
            auto *a = window.acquisition();
            auto done = std::make_shared<bool>(false);
            auto started = std::make_shared<bool>(false);
            QObject::connect(a, &daq::Acquisition::stateChanged, &window,
                             [&, a, directory, started](const QString &s) {
                                 if (s == "Idle" && !*started) {
                                     *started = true;
                                     QTimer::singleShot(0, &window, [&window, directory] {
                                         window.SetOutputSettings(directory, "smoke", "1", true);
                                         window.ShowOutputSettings();
                                         window.StartAcquisition();
                                     });
                                 } else if (s == "Armed" && !a->controlsBaseboardRun())
                                     a->simulateRunEdge();
                                 else if (s == "Running")
                                     QTimer::singleShot(2000, &window, [&, a, directory] {
                                         window.captureWindows(directory);
                                         a->stopRun();
                                     });
                             });
            QObject::connect(a, &daq::Acquisition::fault, &window, [&app](const QString &s) {
                std::cerr << s.toStdString() << std::endl;
                app.exit(2);
            });
            QObject::connect(a, &daq::Acquisition::runFinished, &window,
                             [&app, &window, directory, a, done](bool ok) {
                                 *done = true;
                                 quint64 count = 0;
                                 for (int i = 0; i < a->count(); ++i)
                                     count += a->snapshot(i).events;
                                 std::cout << "Smoke run: " << count << " events, "
                                           << a->lastManifest().toStdString() << std::endl;
                                 window.captureWindows(directory + "/idle");
                                 app.exit(ok && count > 0 ? 0 : 3);
                             });
            QTimer::singleShot(15000, &window, [&app, done] {
                if (!*done)
                    app.exit(4);
            });
            window.openDemo(3);
        } else if (parser.isSet("config"))
            window.openConfig(parser.value("config"), parser.isSet("demo"));
        else if (parser.isSet("demo")) {
            bool ok = false;
            int n = parser.value("modules").toInt(&ok);
            if (!ok)
                throw daq::Error("Invalid module count");
            window.openDemo(n);
        }
    } catch (const std::exception &e) {
        QMessageBox::critical(&window, "Startup", QString::fromUtf8(e.what()));
        return 1;
    }
    return app.exec();
}
