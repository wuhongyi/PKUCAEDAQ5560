#include "Acquisition.hh"
#include <iostream>

using namespace daq;

static bool waitUntil(const std::function<bool()> &ready, int timeoutMs) {
    QEventLoop loop;
    QTimer poll, deadline;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.setTimerType(Qt::PreciseTimer);
    poll.start(20);
    deadline.setSingleShot(true);
    deadline.start(timeoutMs);
    if (!ready()) loop.exec();
    return ready();
}
static QVector<Snapshot> snapshots(const Acquisition &a) {
    QVector<Snapshot> v;
    for (int i = 0; i < a.count(); ++i) v.append(a.snapshot(i));
    return v;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Compare 64/128/256 KiB FIFO requests using normal synchronized runs; saves raw data. No parameter load, FIFO clear or automatic recovery.");
    parser.addHelpOption();
    parser.addOption({"config", "Crate configuration", "file"});
    parser.addOption({"output", "New result directory (must not exist)", "directory"});
    parser.addOption({"seconds", "Measurement seconds per run, after 2 s warmup", "seconds", "20"});
    parser.addOption({"repeats", "Repetitions, rotating block order", "count", "3"});
    parser.addOption({"sizes", "Comma-separated sizes in KiB", "sizes", "64,128,256"});
    parser.addOption({"demo", "Exercise the benchmark with simulated devices; not a bandwidth test"});
    parser.process(app);
    Acquisition acquisition;
    QJsonObject summary;
    QJsonArray rows;
    QString resultPath;
    QFile log;
    bool completed = false, complete = false;
    QObject::connect(&acquisition, &Acquisition::logMessage, &app, [&](const QString &message) {
        const auto line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " " + message + "\n";
        std::cout << line.toStdString() << std::flush;
        if (log.isOpen()) { log.write(line.toUtf8()); log.flush(); }
    });
    QObject::connect(&acquisition, &Acquisition::runFinished, &app, [&](bool ok) {
        completed = true;
        complete = ok;
    });
    try {
        if (!parser.isSet("config") || !parser.isSet("output"))
            throw Error("--config and --output are required");
        bool validSeconds = false, validRepeats = false;
        const int seconds = parser.value("seconds").toInt(&validSeconds);
        const int repeats = parser.value("repeats").toInt(&validRepeats);
        if (!validSeconds || seconds < 3 || seconds > 3600 || !validRepeats || repeats < 1 || repeats > 10)
            throw Error("seconds must be 3..3600; repeats must be 1..10");
        QVector<int> sizes;
        for (const auto &text : parser.value("sizes").split(',')) {
            bool valid = false;
            const int size = text.toInt(&valid);
            if (!valid || (size != 64 && size != 128 && size != 256) || sizes.contains(size))
                throw Error("sizes must contain unique values from 64,128,256");
            sizes.append(size);
        }
        const auto config = Config::load(parser.value("config"));
        const auto output = QFileInfo(parser.value("output")).absoluteFilePath();
        if (QFileInfo::exists(output) || !QDir().mkpath(output))
            throw Error("Output must be a new writable directory");
        resultPath = QDir(output).filePath("comparison.json");
        log.setFileName(QDir(output).filePath("benchmark.log"));
        if (!log.open(QIODevice::WriteOnly | QIODevice::NewOnly)) throw Error(log.errorString());
        summary = {{"schema", "r5560-block-comparison-v1"}, {"status", "connecting"},
                   {"simulated", parser.isSet("demo")}, {"crate", config.json()},
                   {"warmup_seconds", 2}, {"requested_measurement_seconds", seconds},
                   {"repetitions", repeats}, {"save_data", true}};
        writeObject(resultPath, summary);
        acquisition.connectCrate(config, parser.isSet("demo"));
        if (!waitUntil([&] { return acquisition.idle() || acquisition.state() == "Fault" ||
                                  acquisition.state() == "RecoveryRequired"; }, 30000) || !acquisition.idle())
            throw Error("All configured DAQs must connect and be IDLE; benchmark did not start or recover devices");
        quint32 run = 0;
        for (int repeat = 0; repeat < repeats; ++repeat) {
            for (int order = 0; order < sizes.size(); ++order) {
                if (QFileInfo::exists(QDir(output).filePath("STOP")))
                    throw Error("Benchmark cancelled by STOP file");
                const int kib = sizes[(order + repeat) % sizes.size()];
                acquisition.setReadoutBlockKiB(kib);
                completed = complete = false;
                acquisition.startRun(++run, output, QString("fifo%1").arg(kib), true);
                if (!waitUntil([&] { return acquisition.state() == "Running" ||
                                          acquisition.state() == "Fault" || completed; }, 30000) ||
                    acquisition.state() != "Running")
                    throw Error("Run did not enter RUNNING; aborting comparison");
                auto interrupted = [&] { return acquisition.state() != "Running" ||
                                                QFileInfo::exists(QDir(output).filePath("STOP")); };
                if (waitUntil(interrupted, 2000)) throw Error("Run interrupted during warmup");
                const auto before = snapshots(acquisition);
                if (waitUntil(interrupted, seconds * 1000)) throw Error("Run interrupted during measurement");
                const auto after = snapshots(acquisition);
                acquisition.stopRun();
                if (!waitUntil([&] { return completed; }, 25000) || !complete || !acquisition.idle())
                    throw Error("STOP/drain did not complete; no subsequent benchmark runs will start");
                QJsonArray modules;
                double totalRate = 0;
                for (int i = 0; i < after.size(); ++i) {
                    const auto &a = before[i], &b = after[i];
                    const auto &x = a.readout, &y = b.readout;
                    const double duration = (y.elapsedNs - x.elapsedNs) / 1e9;
                    const quint64 bytes = y.bytes - x.bytes, calls = y.fifoCalls - x.fifoCalls;
                    if (duration <= 0) throw Error("No valid measurement interval");
                    quint64 accepted = 0, rejected = 0;
                    for (int ch = 0; ch < Channels; ++ch) {
                        accepted += quint32(b.accepted[ch] - a.accepted[ch]);
                        rejected += quint32(b.rejected[ch] - a.rejected[ch]);
                    }
                    const double rate = bytes / duration / 1e6;
                    totalRate += rate;
                    modules.append(QJsonObject{{"module", i}, {"bytes", QString::number(bytes)},
                        {"elapsed_s", duration}, {"MB_s", rate}, {"calls", QString::number(calls)},
                        {"average_KiB", calls ? double(bytes) / calls / 1024 : 0},
                        {"mean_fifo_ms", calls ? (y.fifoNs - x.fifoNs) / 1e6 / calls : 0},
                        {"empty", QString::number(y.emptyReads - x.emptyReads)},
                        {"short", QString::number(y.shortReads - x.shortReads)},
                        {"accepted_delta", QString::number(accepted)},
                        {"rejected_delta", QString::number(rejected)}});
                }
                rows.append(QJsonObject{{"run", int(run)}, {"repeat", repeat + 1}, {"request_KiB", kib},
                    {"total_MB_s", totalRate}, {"modules", modules}, {"manifest", acquisition.lastManifest()}});
                summary["results"] = rows;
                summary["status"] = "testing";
                writeObject(resultPath, summary);
                std::cout << "RESULT request_KiB=" << kib << " total_MB_s=" << totalRate << std::endl;
                if (totalRate == 0)
                    throw Error("No event data during measurement; keep input signals on before comparing throughput");
            }
        }
        summary["status"] = "complete";
        writeObject(resultPath, summary);
        acquisition.disconnectCrate();
        return 0;
    } catch (const std::exception &error) {
        const QString reason = QString::fromUtf8(error.what());
        std::cerr << reason.toStdString() << std::endl;
        if (acquisition.hasSession()) {
            acquisition.stopRun();
            waitUntil([&] { return !acquisition.hasSession(); }, 25000);
        }
        if (!resultPath.isEmpty()) {
            summary["status"] = "failed";
            summary["error"] = reason;
            summary["results"] = rows;
            summary["session_still_active"] = acquisition.hasSession();
            try { writeObject(resultPath, summary); } catch (...) {}
        }
        acquisition.disconnectCrate();
        return 1;
    }
}
