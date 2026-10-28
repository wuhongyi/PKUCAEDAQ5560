#include "DeviceHandle.hh"
#include "DmaReceiver.hh"
#include "EventDecoder.hh"
#include <QNetworkProxy>
#include <algorithm>
#include <cmath>

namespace daq {
void DeviceHandle::selectEventTransport(quint32 api) {
    if (api != ApiVersion)
        throw Error(QString("This device transport does not support firmware API 0x%1")
                        .arg(api, 8, 16, QChar('0')));
}
QVector<quint32> DeviceHandle::readBlock(quint32 a, int n) {
    QVector<quint32> v;
    for (int i = 0; i < n; ++i)
        v.append(read(a + quint32(i)));
    return v;
}
void DeviceHandle::writeChecked(quint32 a, quint32 v) {
    write(a, v);
    if (read(a) != v)
        throw Error(QString("Register 0x%1 readback mismatch").arg(a, 8, 16, QChar('0')));
}
void DeviceHandle::requireIdle() {
    if (!(read(R5560_SYNC_STATUS) & Idle))
        throw Error("Device is not IDLE; stop and drain before changing settings");
}
ModuleSettings DeviceHandle::readSettings() {
    ModuleSettings s;
    s.syncConfig = read(R5560_SYNC_CONFIG);
    s.channelMask = read(R5560_CHANNEL_ENABLE);
    for (int p = 0; p < ParameterCount; ++p) {
        auto a = readBlock(Parameters[p].address, Channels);
        for (int ch = 0; ch < Channels; ++ch)
            s.channels[ch][p] = a[ch];
    }
    return s;
}
void DeviceHandle::writeSettings(const ModuleSettings &s) {
    s.validate();
    requireIdle();
    for (int p = 0; p < ParameterCount; ++p)
        for (int ch = 0; ch < Channels; ++ch)
            write(Parameters[p].address + quint32(ch), s.channels[ch][p]);
    writeChecked(R5560_SYNC_CONFIG, s.syncConfig);
    writeChecked(R5560_CHANNEL_ENABLE, s.channelMask);
    auto back = readSettings();
    if (back.json() != s.json())
        throw Error("Parameter readback mismatch; settings were only partially applied. Read "
                    "device before retrying.");
}
TcpDevice::TcpDevice(const Endpoint &ep, int timeoutMs)
    : dmaEndpoint(QString("tcp://%1:5556").arg(ep.ip)), timeout(timeoutMs),
      peer(QString("%1:%2").arg(ep.ip).arg(ep.port)), request("TCP connect") {
    setTimeout(timeoutMs);
    socket.setProxy(QNetworkProxy::NoProxy);
    deadline.start();
    requestStartedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    socket.connectToHost(ep.ip, ep.port);
    if (!waitFor(WaitFor::Connection))
        fail("Connect");
    socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    socket.setReadBufferSize(2 * 1024 * 1024);
}
TcpDevice::~TcpDevice() = default;
void TcpDevice::selectEventTransport(quint32 api) {
    if (dmaReceiver)
        throw Error("Cannot change event transport while a DMA stream exists");
    if (!supportedApi(api))
        throw Error(QString("Unsupported firmware API 0x%1").arg(api, 8, 16, QChar('0')));
    dmaMode = api == DmaApiVersion;
}
void TcpDevice::openEventStream() {
    if (!dmaMode)
        return;
    if (dmaReceiver)
        throw Error("DMA stream already exists; explicit stop and drain is required");
    auto receiver = std::make_unique<DmaReceiver>();
    receiver->open(dmaEndpoint, timeout);
    dmaReceiver = std::move(receiver);
    if (diagnostic)
        diagnostic(QString("DMA ZMQ stream connected: %1; full-message receive, "
                           "max 32 MiB/message, queue HWM 2, automatic reconnect disabled")
                       .arg(dmaEndpoint));
}
void TcpDevice::closeEventStream() { dmaReceiver.reset(); }
QByteArray TcpDevice::takePendingEventData() {
    return dmaMode && dmaReceiver ? dmaReceiver->takePending() : QByteArray();
}
void TcpDevice::verifyEventStreamEmpty() {
    if (!dmaMode)
        return;
    if (!dmaReceiver)
        throw Error("DMA event stream has not been opened");
    dmaReceiver->verifyEmpty();
}
QByteArray TcpDevice::readEventData(int maxWords) {
    if (!dmaMode)
        return readFifo(maxWords);
    if (maxWords < 1 || maxWords > 65536)
        throw Error("Invalid DMA read block size");
    if (!dmaReceiver)
        throw Error("DMA event stream has not been opened");
    return dmaReceiver->read(maxWords * 4);
}
void TcpDevice::setDiagnosticHandler(std::function<void(const QString &)> handler) {
    diagnostic = std::move(handler);
}
void TcpDevice::setTimeout(int timeoutMs) {
    if (timeoutMs <= 0)
        throw Error("TCP timeout must be positive");
    timeout = timeoutMs;
}
int TcpDevice::remaining() const {
    return int(std::max<qint64>(0, qint64(timeout) - deadline.elapsed()));
}
void TcpDevice::warnIfSlow() {
    if (!warned && timeout > WarningTimeoutMs && deadline.elapsed() >= WarningTimeoutMs) {
        warned = true;
        if (diagnostic)
            diagnostic(QString("Slow transaction: %1 | %2 | reply %3/%4 bytes, elapsed %5 ms; "
                               "continuing the SAME request up to %6 ms, no command resend | "
                               "worker_utc=%7, request_start_utc=%8")
                           .arg(peer, request).arg(receivedBytes).arg(expectedBytes)
                           .arg(deadline.elapsed()).arg(timeout)
                           .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                                requestStartedUtc));
    }
}
void TcpDevice::completeRequest() {
    warnIfSlow();
    if (warned && diagnostic)
        diagnostic(QString("Slow transaction completed: %1 | %2 | elapsed %3 ms, reply %4 bytes; "
                           "same connection, no command resend | worker_utc=%5")
                       .arg(peer, request).arg(deadline.elapsed()).arg(receivedBytes)
                       .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
    responseBytes.clear();
}
bool TcpDevice::waitFor(WaitFor condition) {
    const auto ready = [&] {
        if (condition == WaitFor::Readable)
            return socket.bytesAvailable() > 0;
        if (socket.state() != QAbstractSocket::ConnectedState)
            return false;
        return condition == WaitFor::Connection || socket.bytesToWrite() == 0;
    };
    // Qt documents random Windows failures in the blocking waitFor* APIs.
    // Drive socket notifications only in the owning worker thread. The command
    // queue is still serialized by ReadoutThread; no GUI command runs here.
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setTimerType(Qt::PreciseTimer);
    const auto check = [&] {
        if (ready() || socket.state() == QAbstractSocket::UnconnectedState)
            loop.quit();
    };
    QObject::connect(&socket, &QTcpSocket::connected, &loop, check);
    QObject::connect(&socket, &QTcpSocket::bytesWritten, &loop, check);
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, check);
    QObject::connect(&socket, &QTcpSocket::stateChanged, &loop, check);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);
#else
    QObject::connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                     &loop, &QEventLoop::quit);
#endif
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    while (true) {
        warnIfSlow();
        if (!remaining())
            return false;
        if (ready())
            return true;
        if (socket.state() == QAbstractSocket::UnconnectedState)
            return false;
        int waitMs = remaining();
        if (!warned && timeout > WarningTimeoutMs)
            waitMs = std::min(waitMs, std::max(1, WarningTimeoutMs - int(deadline.elapsed())));
        timer.start(waitMs);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        // The first timer expiry is only a warning. Keep this exact transaction
        // and its partial reply; the absolute hard deadline is never restarted.
    }
}
[[noreturn]] void TcpDevice::fail(const QString &why) {
    const auto msg = QString("%1 | %2 | %3 | reply %4/%5 bytes, elapsed %6 ms / limit %7 ms | %8"
                            " | socket_state=%9, socket_error=%10 | worker_utc=%11, "
                            "request_start_utc=%12")
                         .arg(peer, request, why)
                         .arg(receivedBytes).arg(expectedBytes)
                         .arg(deadline.elapsed()).arg(timeout).arg(socket.errorString())
                         .arg(int(socket.state())).arg(int(socket.error()))
                         .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                              requestStartedUtc);
    socket.abort();
    throw TransportError(msg, responseBytes);
}
void TcpDevice::send(const QVector<quint32> &words) {
    deadline.restart();
    requestStartedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    responseBytes.clear();
    warned = false;
    receivedBytes = expectedBytes = 0;
    request = QString("command %1, address 0x%2, words %3")
                  .arg(words[1]).arg(words[2], 8, 16, QChar('0')).arg(words[3]);
    QByteArray bytes;
    for (auto v : words)
        appendWord(bytes, v);
    if (socket.state() != QAbstractSocket::ConnectedState)
        fail("Disconnected");
    if (socket.write(bytes) != bytes.size())
        fail("Send failed");
    while (socket.bytesToWrite() > 0)
        if (!waitFor(WaitFor::Written))
            fail("Send timeout");
    if (!remaining())
        fail("Send deadline exceeded");
}
QByteArray TcpDevice::receive(int bytes) {
    expectedBytes += bytes;
    QByteArray result;
    result.reserve(bytes);
    while (result.size() < bytes) {
        if (!remaining())
            fail("Response timeout / truncated frame");
        auto part = socket.read(bytes - result.size());
        result.append(part);
        responseBytes.append(part);
        receivedBytes += part.size();
        if (result.size() == bytes)
            break;
        if (socket.state() != QAbstractSocket::ConnectedState)
            fail("Connection closed during response");
        if (!waitFor(WaitFor::Readable))
            fail("Response timeout / truncated frame");
    }
    return result;
}
quint32 TcpDevice::read(quint32 a) {
    send({0x80000000, 1, a, 1});
    const auto value = word(receive(4).constData());
    completeRequest();
    return value;
}
void TcpDevice::write(quint32 a, quint32 v) {
    send({0x80000000, 0, a, 1, v});
    completeRequest();
}
void TcpDevice::writeInternal(quint32 a, quint32 v) {
    // NI_InternalWriteReg(data, address, handle): command 5 has no reply.
    // Do not use command 0 or wait for an ACK that the protocol never sends.
    send({0x80000000, 5, a, 1, v});
    completeRequest();
}
quint32 TcpDevice::readInternal(quint32 a) {
    // NI_InternalReadReg uses command 6, distinct from the FPGA register bus.
    send({0x80000000, 6, a, 1});
    const auto value = word(receive(4).constData());
    completeRequest();
    return value;
}
QVector<quint32> TcpDevice::readBlock(quint32 a, int n) {
    if (n < 1 || n > 65536)
        throw Error("Invalid block size");
    send({0x80000000, 3, a, quint32(n)});
    auto b = receive(n * 4);
    QVector<quint32> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i)
        v.append(word(b.constData() + 4 * i));
    completeRequest();
    return v;
}
QByteArray TcpDevice::readFifo(int n) {
    if (dmaMode)
        throw Error("Avalon FIFO reads are disabled for DMA firmware; use the DMA event stream");
    if (n < 1 || n > 65536)
        throw Error("Invalid FIFO block size");
    // SDK command 4, STREAMING_NONBLOCKING. Returned count can be short or zero.
    send({0x80000000, 4, R5560_LIST_BUS_WRITEDATA, quint32(n), R5560_LIST_BUS_STATUS, 10});
    auto count = word(receive(4).constData());
    if (count > quint32(n))
        fail("FIFO returned too many words");
    auto bytes = receive(int(count) * 4);
    completeRequest();
    return bytes;
}
DemoDevice::DemoDevice(int m) : module(m) {
    registers[R5560_WAVE_VERSION] = R5560_WAVE_API;
    registers[R5560_WAVE_LENGTH] = 16384;
    registers[R5560_WAVE_PRETRIGGER] = 2048;
    waveMemory.resize(16384);
    registers[R5560_API_VERSION] = ApiVersion;
    registers[R5560_CLOCK_HZ] = ClockHz;
    registers[R5560_DIAG_VERSION] = R5560_DIAG_API;
    registers[R5560_CHANNEL_FIFO_DEPTH] = 1024;
    registers[R5560_DMA_FIFO_DEPTH] = 8192;
    registers[R5560_EVENT_FORMAT] = 2;
    registers[R5560_CHANNEL_ENABLE] = 0xffffffff;
    ModuleSettings s;
    writeSettings(s);
}
quint32 DemoDevice::read(quint32 a) {
    if (a == R5560_WAVE_STATUS && (registers[a] & 2) &&
        waveClock.elapsed() >= (waveForced ? 20 : 200)) {
        int n = int(registers[R5560_WAVE_LENGTH]);
        int pre = int(registers[R5560_WAVE_PRETRIGGER]);
        int ch = int(registers[R5560_WAVE_CHANNEL]);
        const int start = 16300;
        for (int i = 0; i < n; ++i) {
            const double x = double(i - pre + 24) / 30;
            double pulse = waveForced ? 0 : 1200 * std::exp(-x * x / 2);
            if (!registers[R5560_POL + quint32(ch)])
                pulse = -pulse;
            int noise = ((i * 17 + i / 7 + ch * 11) % 13) - 6;
            auto adc = quint32(std::clamp(int(6000 + module * 120 + pulse) + noise, 0, 16383));
            waveMemory[(start + i) % 16384] = adc | ((!waveForced && i == pre) ? 0x10000u : 0u);
        }
        registers[R5560_WAVE_POSITION] = start;
        registers[R5560_WAVE_SEQUENCE]++;
        registers[R5560_WAVE_STATUS] = 1;
    }
    if (a >= R5560_WAVE_DATA_START && a < R5560_WAVE_DATA_STOP)
        return registers[R5560_WAVE_STATUS] & 1 ? waveMemory[int(a - R5560_WAVE_DATA_START)] : 0;
    if (a == R5560_SYNC_STATUS)
        return status;
    if (a == R5560_LIST_BUS_STATUS)
        return 1;
    if (a == R5560_TIMESTAMP_LO)
        return clock.isValid() ? quint32(clock.nsecsElapsed() / 8) : 0;
    if (a == R5560_TIMESTAMP_HI)
        return clock.isValid() ? quint32(quint64(clock.nsecsElapsed() / 8) >> 32) : 0;
    if (a >= R5560_TRIGGERIN && a < R5560_TRIGGERIN + Channels)
        return status & Running ? 125 : 0;
    if (a >= R5560_TRIGGEROUT && a < R5560_TRIGGEROUT + Channels)
        return status & Running ? 125 : 0;
    if (a >= R5560_ACCEPTED && a < R5560_ACCEPTED + Channels)
        return counts[a - R5560_ACCEPTED];
    return registers.value(a, 0);
}
void DemoDevice::write(quint32 a, quint32 v) {
    if (a == R5560_WAVE_COMMAND) {
        if (v & R5560_WAVE_ABORT)
            registers[R5560_WAVE_STATUS] = 0;
        else if (v & R5560_WAVE_ARM) {
            if (registers[R5560_WAVE_CHANNEL] >= 32 || registers[R5560_WAVE_LENGTH] < 1 ||
                registers[R5560_WAVE_LENGTH] > 16384 ||
                registers[R5560_WAVE_PRETRIGGER] >= registers[R5560_WAVE_LENGTH] ||
                (registers[R5560_WAVE_STATUS] & 2))
                registers[R5560_WAVE_STATUS] |= 8;
            else {
                registers[R5560_WAVE_STATUS] = 6;
                waveForced = false;
                waveClock.start();
            }
        } else if ((v & R5560_WAVE_FORCE) && (registers[R5560_WAVE_STATUS] & 2))
            waveForced = true;
        return;
    }
    if (a >= R5560_WAVE_CHANNEL && a <= R5560_WAVE_PRETRIGGER && (registers[R5560_WAVE_STATUS] & 2))
        return;
    if (a == R5560_SYNC_COMMAND) {
        if (v & R5560_CMD_SNAPSHOT) {
            // Demo has no hardware queues or processing dead time. Its zero
            // diagnostics are explicitly simulated, but obey snapshot/epoch rules.
            ++registers[R5560_DIAG_SEQUENCE];
            registers[R5560_DIAG_START_COUNT] = registers[R5560_START_COUNT];
        }
        if (v == R5560_CMD_ARM) {
            requireIdle();
            counts.fill(0);
            for (int ch = 0; ch < Channels; ++ch) {
                registers[R5560_REJECTED_PROCESSING + ch] = 0;
                registers[R5560_REJECTED_FIFO + ch] = 0;
                registers[R5560_CHANNEL_FIFO_PEAK + ch] = 0;
            }
            registers[R5560_DMA_FIFO_PEAK] = 0;
            registers[R5560_DMA_STALL_MAX_LO] = 0;
            registers[R5560_DMA_STALL_MAX_HI] = 0;
            registers[R5560_DIAG_START_COUNT] = 0;
            nextEvent = 0;
            status = Armed | ClockGood | FifoReady;
            if (!(registers[R5560_SYNC_CONFIG] & 1))
                demoRun();
        }
        if (v == R5560_CMD_STOP)
            status = Idle | ClockGood | FifoReady;
        if (v == R5560_CMD_CLEAR_ERRORS)
            registers[R5560_SYNC_ERRORS] = 0;
    } else
        registers[a] = v;
}
void DemoDevice::demoRun() {
    if (status & Armed) {
        clock.restart();
        status = Running | ClockGood | FifoReady;
        registers[R5560_START_COUNT]++;
    }
}
void DemoDevice::writeInternal(quint32 a, quint32 v) {
    if (a != BaseboardRunRegister || v > 1)
        throw Error("Unsupported demo board register");
    if (v)
        status |= RunInputHigh;
    else
        status &= ~RunInputHigh;
    // The acquisition coordinator distributes the simulated edge to peers.
}
QByteArray DemoDevice::readFifo(int n) {
    QByteArray b;
    if (!(status & Running))
        return b;
    const quint64 elapsed = quint64(clock.nsecsElapsed() / 8);
    while (nextEvent * 31250 < elapsed && b.size() + 32 <= n * 4) {
        int ch = int(nextEvent % Channels);
        quint64 ts = (nextEvent / Channels) * 1000000 + quint64(ch) * 8;
        ++nextEvent;
        if (!(registers[R5560_CHANNEL_ENABLE] & (1u << ch)))
            continue;
        quint64 previous = ts >= 1000000 ? ts - 1000000 : 0;
        auto sl = registers[R5560_XIASL + ch];
        auto sg = registers[R5560_XIASG + ch];
        auto bl = registers[R5560_BLLEN + ch];
        int peak = 350 + module * 45 + ch * 5 + int(25 * std::sin(double(nextEvent) * 0.137));
        appendWord(b, quint32(ts));
        appendWord(b, quint32((ts >> 32) & 0xffff) | quint32((previous >> 32) << 16));
        appendWord(b, (quint32(ch) << 27) | (bl * 100));
        appendWord(b, bl * 100);
        appendWord(b, sl * 100);
        appendWord(b, sg * 100);
        appendWord(b, sl * quint32(100 + peak));
        appendWord(b, quint32(previous));
        ++counts[ch];
    }
    return b;
}
} // namespace daq
