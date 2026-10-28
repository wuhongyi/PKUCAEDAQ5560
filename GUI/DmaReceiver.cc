#include "DmaReceiver.hh"
#include "DeviceHandle.hh"
#include <algorithm>
#include <cerrno>
#include <cstring>

namespace daq {
namespace {
// Stable libzmq 4.x C ABI, loaded dynamically to keep Avalon-only installations
// independent of libzmq. The SDK's old zmq.h declares a 32-byte message; modern
// libzmq uses 64 bytes. Do not include that obsolete header with a 4.3 library.
struct alignas(void *) ZmqMessage { unsigned char storage[64]; };
constexpr int Pair = 0, Pull = 7, DontWait = 1;
constexpr int Linger = 17, ReconnectInterval = 18, MaxMessageSize = 22,
              ReceiveHwm = 24, ReceiveTimeout = 27, TcpKeepalive = 34,
              TcpKeepaliveCount = 35, TcpKeepaliveIdle = 36,
              TcpKeepaliveInterval = 37, Immediate = 39,
              HandshakeInterval = 66, ConnectTimeout = 79;
constexpr quint16 Connected = 0x0001, ConnectDelayed = 0x0002,
                  ConnectRetried = 0x0004, Closed = 0x0080,
                  Disconnected = 0x0200, HandshakeFailed = 0x0800,
                  HandshakeSucceeded = 0x1000, ProtocolFailed = 0x2000,
                  AuthenticationFailed = 0x4000;
constexpr int MonitorEvents = Connected | ConnectDelayed | ConnectRetried | Closed |
                              Disconnected | HandshakeFailed | HandshakeSucceeded |
                              ProtocolFailed | AuthenticationFailed;
}

struct DmaReceiver::Impl {
    QLibrary library;
    void *context = nullptr, *socket = nullptr, *monitor = nullptr;
    QString endpoint, fault;
    QByteArray pending;
    int pendingOffset = 0;
    bool established = false;
    void *(*ctxNew)() = nullptr;
    int (*ctxTerm)(void *) = nullptr;
    int (*ctxShutdown)(void *) = nullptr;
    void *(*socketNew)(void *, int) = nullptr;
    int (*socketClose)(void *) = nullptr;
    int (*setOption)(void *, int, const void *, size_t) = nullptr;
    int (*connect)(void *, const char *) = nullptr;
    int (*socketMonitor)(void *, const char *, int) = nullptr;
    int (*messageInit)(ZmqMessage *) = nullptr;
    int (*messageClose)(ZmqMessage *) = nullptr;
    int (*messageRecv)(ZmqMessage *, void *, int) = nullptr;
    void *(*messageData)(ZmqMessage *) = nullptr;
    size_t (*messageSize)(const ZmqMessage *) = nullptr;
    int (*messageMore)(const ZmqMessage *) = nullptr;
    int (*errorNumber)() = nullptr;
    const char *(*errorString)(int) = nullptr;
    void (*version)(int *, int *, int *) = nullptr;

    template <class Function> void resolve(Function &function, const char *symbol) {
        function = reinterpret_cast<Function>(library.resolve(symbol));
        if (!function)
            throw Error(QString("DMA: libzmq lacks %1: %2").arg(symbol, library.errorString()));
    }
    void load() {
        if (version)
            return;
        QStringList candidates;
        const auto overridePath = qEnvironmentVariable("R5560_ZMQ_LIBRARY");
        if (!overridePath.isEmpty())
            candidates << overridePath;
        else {
#ifdef Q_OS_WIN
            candidates << QDir(QCoreApplication::applicationDirPath())
                              .filePath("libzmq-v140-mt-4_3_4.dll")
                       << "libzmq-v140-mt-4_3_4.dll" << "libzmq.dll";
#else
            candidates << "libzmq.so.5" << "libzmq.so";
#endif
        }
        QStringList failures;
        for (const auto &name : candidates) {
            library.setFileName(name);
            if (library.load())
                break;
            failures << name + ": " + library.errorString();
        }
        if (!library.isLoaded())
            throw Error("DMA requires libzmq >= 4.3 (Windows: libzmq and libsodium beside "
                        "the executable; Linux: libzmq5). " + failures.join("; "));
        // Avoid unloading libzmq while another worker may be terminating its I/O
        // thread. QLibrary shares reference counts; keeping this DLL loaded is cheap.
        library.setLoadHints(QLibrary::PreventUnloadHint);
        resolve(ctxNew, "zmq_ctx_new");
        resolve(ctxTerm, "zmq_ctx_term");
        resolve(ctxShutdown, "zmq_ctx_shutdown");
        resolve(socketNew, "zmq_socket");
        resolve(socketClose, "zmq_close");
        resolve(setOption, "zmq_setsockopt");
        resolve(connect, "zmq_connect");
        resolve(socketMonitor, "zmq_socket_monitor");
        resolve(messageInit, "zmq_msg_init");
        resolve(messageClose, "zmq_msg_close");
        resolve(messageRecv, "zmq_msg_recv");
        resolve(messageData, "zmq_msg_data");
        resolve(messageSize, "zmq_msg_size");
        resolve(messageMore, "zmq_msg_more");
        resolve(errorNumber, "zmq_errno");
        resolve(errorString, "zmq_strerror");
        void (*candidateVersion)(int *, int *, int *) = nullptr;
        resolve(candidateVersion, "zmq_version");
        int major = 0, minor = 0, patch = 0;
        candidateVersion(&major, &minor, &patch);
        if (major != 4 || minor < 3)
            throw Error(QString("DMA requires libzmq 4.3 or newer within ABI 4; loaded %1.%2.%3")
                            .arg(major).arg(minor).arg(patch));
        version = candidateVersion;
    }
    QString zmqError() const {
        return QString::fromLocal8Bit(errorString(errorNumber()));
    }
    [[noreturn]] void fail(const QString &why) {
        fault = QString("DMA %1: %2; stream cannot resume or reconnect within this run")
                    .arg(endpoint, why);
        // Pending bytes have already left the board. Give them to fault recovery
        // as raw DMA bytes, never pretend they are a FIFO count-prefixed reply.
        auto residual = pending.mid(pendingOffset);
        pending.clear();
        pendingOffset = 0;
        throw TransportError(fault, residual, true);
    }
    void option(int name, int value) {
        if (setOption(socket, name, &value, sizeof(value)) != 0)
            fail(QString("socket option %1 failed: %2").arg(name).arg(zmqError()));
    }
    // Reads one entire ZMQ frame. The transport-level MAXMSGSIZE limits allocation
    // before this method is called; the size check is a second defensive limit.
    bool frame(void *source, QByteArray &bytes, bool &more, int maxBytes) {
        ZmqMessage message{};
        if (messageInit(&message) != 0)
            fail("message initialization failed: " + zmqError());
        const int rc = messageRecv(&message, source, DontWait);
        if (rc < 0) {
            const int error = errorNumber();
            const auto detail = QString::fromLocal8Bit(errorString(error));
            messageClose(&message);
            if (error == EAGAIN || error == EINTR)
                return false;
            fail("receive failed: " + detail);
        }
        const auto size = messageSize(&message);
        if (size > size_t(maxBytes)) {
            messageClose(&message);
            fail(QString("message exceeds %1 bytes; refusing truncated data").arg(maxBytes));
        }
        bytes = QByteArray(static_cast<const char *>(messageData(&message)), int(size));
        more = messageMore(&message) != 0;
        messageClose(&message);
        return true;
    }
    void checkMonitor() {
        if (!fault.isEmpty())
            throw TransportError(fault, {}, true);
        // Connection retries are disabled, so bounded monitor traffic is small.
        for (int i = 0; i < 64; ++i) {
            QByteArray header, address;
            bool more = false;
            if (!frame(monitor, header, more, 64))
                return;
            if (header.size() != 6 || !more || !frame(monitor, address, more, 4096) || more)
                fail("invalid socket monitor message");
            quint16 event = 0;
            quint32 value = 0;
            std::memcpy(&event, header.constData(), sizeof(event));
            std::memcpy(&value, header.constData() + sizeof(event), sizeof(value));
            if (event == HandshakeSucceeded)
                established = true;
            else if (event == Disconnected || event == Closed || event == ConnectRetried ||
                     event == HandshakeFailed || event == ProtocolFailed ||
                     event == AuthenticationFailed)
                fail(QString("connection/handshake lost (event 0x%1, value %2)")
                         .arg(event, 4, 16, QChar('0')).arg(value));
        }
    }
    void close() noexcept {
        if (socket) {
            socketMonitor(socket, nullptr, 0);
            socketClose(socket);
            socket = nullptr;
        }
        if (monitor) {
            socketClose(monitor);
            monitor = nullptr;
        }
        if (context) {
            ctxShutdown(context);
            while (ctxTerm(context) != 0 && errorNumber() == EINTR) {}
            context = nullptr;
        }
        pending.clear();
        pendingOffset = 0;
        established = false;
        fault.clear();
    }
};

DmaReceiver::DmaReceiver() : impl(std::make_unique<Impl>()) {}
DmaReceiver::~DmaReceiver() { close(); }
void DmaReceiver::close() noexcept { impl->close(); }
bool DmaReceiver::isOpen() const {
    return impl->socket && impl->established && impl->fault.isEmpty();
}
void DmaReceiver::open(const QString &endpoint, int timeoutMs) {
    if (impl->socket)
        throw Error("DMA stream already open; stop and drain before reopening");
    if (timeoutMs < 1 || !endpoint.startsWith("tcp://"))
        throw Error("Invalid DMA endpoint or connection timeout");
    impl->load();
    impl->endpoint = endpoint;
    impl->fault.clear();
    try {
        impl->context = impl->ctxNew();
        if (!impl->context)
            impl->fail("context creation failed: " + impl->zmqError());
        impl->socket = impl->socketNew(impl->context, Pull);
        if (!impl->socket)
            impl->fail("socket creation failed: " + impl->zmqError());
        impl->option(Linger, 0);
        impl->option(ReconnectInterval, -1);
        impl->option(ReceiveHwm, ReceiveHighWaterMark);
        impl->option(ReceiveTimeout, 0);
        impl->option(Immediate, 1);
        impl->option(ConnectTimeout, timeoutMs);
        impl->option(HandshakeInterval, timeoutMs);
        // The vendor service can use ZMTP 3.0, which predates PING/PONG.
        // libzmq 4.3 sends configured heartbeats even to a 3.0 peer, so rely on
        // TCP keepalive, monitor events and the worker's control/progress checks.
        impl->option(TcpKeepalive, 1);
        impl->option(TcpKeepaliveCount, 3);
        impl->option(TcpKeepaliveIdle, 5);
        impl->option(TcpKeepaliveInterval, 1);
        const qint64 maxSize = MaxMessageBytes;
        if (impl->setOption(impl->socket, MaxMessageSize, &maxSize, sizeof(maxSize)) != 0)
            impl->fail("setting maximum message size failed: " + impl->zmqError());
        const auto monitorEndpoint = "inproc://r5560-dma-" + QUuid::createUuid().toString();
        if (impl->socketMonitor(impl->socket, monitorEndpoint.toUtf8().constData(), MonitorEvents) != 0)
            impl->fail("socket monitoring failed: " + impl->zmqError());
        impl->monitor = impl->socketNew(impl->context, Pair);
        if (!impl->monitor)
            impl->fail("monitor creation failed: " + impl->zmqError());
        const int linger = 0;
        if (impl->setOption(impl->monitor, Linger, &linger, sizeof(linger)) != 0 ||
            impl->connect(impl->monitor, monitorEndpoint.toUtf8().constData()) != 0 ||
            impl->connect(impl->socket, endpoint.toUtf8().constData()) != 0)
            impl->fail("connect failed: " + impl->zmqError());
        QElapsedTimer deadline;
        deadline.start();
        while (!impl->established) {
            impl->checkMonitor();
            if (impl->established)
                break;
            if (deadline.elapsed() >= timeoutMs)
                impl->fail(QString("ZMTP handshake timed out after %1 ms").arg(timeoutMs));
            QThread::msleep(2);
        }
    } catch (...) {
        close();
        throw;
    }
}
QByteArray DmaReceiver::read(int maxBytes) {
    if (maxBytes < 1 || maxBytes > MaxMessageBytes)
        throw Error("Invalid DMA read size");
    if (!impl->socket)
        throw Error("DMA event stream has not been opened");
    impl->checkMonitor();
    if (!impl->established)
        impl->fail("stream handshake is incomplete");
    if (impl->pendingOffset == impl->pending.size()) {
        impl->pending.clear();
        impl->pendingOffset = 0;
        bool more = false;
        if (!impl->frame(impl->socket, impl->pending, more, MaxMessageBytes)) {
            impl->checkMonitor();
            return {};
        }
        if (more)
            impl->fail("multipart DMA messages are unsupported; refusing ambiguous framing");
    }
    const auto result = impl->pending.mid(impl->pendingOffset, maxBytes);
    impl->pendingOffset += result.size();
    return result;
}
void DmaReceiver::verifyEmpty() {
    if (!impl->socket)
        throw Error("DMA event stream has not been opened");
    impl->checkMonitor();
    if (impl->pendingOffset < impl->pending.size())
        impl->fail("extra/stale bytes after FPGA end count");
    impl->pending.clear();
    impl->pendingOffset = 0;
    // Check messages already queued by the ZMQ I/O thread as well as the
    // caller's partially consumed message. Empty frames carry no event bytes.
    for (int i = 0; i < 64; ++i) {
        bool more = false;
        if (!impl->frame(impl->socket, impl->pending, more, MaxMessageBytes)) {
            impl->checkMonitor();
            return;
        }
        if (!impl->pending.isEmpty())
            impl->fail("extra/stale bytes after FPGA end count");
        if (more)
            impl->fail("multipart DMA messages after FPGA end count");
    }
    impl->fail("too many empty DMA messages to verify the end of the stream");
}
QByteArray DmaReceiver::takePending() {
    auto bytes = impl->pending.mid(impl->pendingOffset);
    impl->pending.clear();
    impl->pendingOffset = 0;
    return bytes;
}
} // namespace daq
