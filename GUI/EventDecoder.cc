#include "EventDecoder.hh"
#include <QtEndian>
#include <cmath>

namespace daq {
quint32 word(const char *p) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}
void appendWord(QByteArray &b, quint32 v) {
    char tmp[4];
    qToLittleEndian(v, reinterpret_cast<uchar *>(tmp));
    b.append(tmp, 4);
}
EventDecoder::EventDecoder(const ModuleSettings &settings) {
    settings.validate();
    // Energy / baseline formula adapted directly from Hongyi Wu's
    // GUI27xx/RealDecoderThread.cc (UserDPP). Replace ROOT TMath with <cmath>.
    // The R5560 stores 24-bit integrals; tau uses ns, windows use 8 ns samples.
    for (int ch = 0; ch < Channels; ++ch) {
        const auto &p = settings.channels[ch];
        auto &c = coeff[ch];
        c.sl = p[6];
        c.sg = p[7];
        c.bl = p[4];
        double x = -8.0 / p[8], oneMinusB = -std::expm1(x), decaySl = std::exp(x * c.sl);
        c.a1 = oneMinusB / (-std::expm1(x * c.sl));
        c.a0 = -c.a1 * decaySl;
        c.ag = oneMinusB;
        const double decayBl = std::exp(x * c.bl);
        c.abl = (-std::expm1(x * c.bl)) / (decayBl * (-std::expm1(x * c.sl)));
        c.abl2 = 1.0 / decayBl;
    }
}
std::vector<Event> EventDecoder::feed(const QByteArray &bytes) {
    pending.append(bytes);
    std::vector<Event> result;
    result.reserve(size_t(pending.size() / 32));
    int offset = 0;
    while (pending.size() - offset >= 32) {
        const char *p = pending.constData() + offset;
        std::array<quint32, 8> w;
        for (int i = 0; i < 8; ++i)
            w[i] = word(p + 4 * i);
        if ((w[2] & 0x07000000u) || ((w[3] | w[4] | w[5] | w[6]) & 0xff000000u))
            throw Error("Invalid event reserved bits; FIFO alignment or firmware format mismatch");
        Event e;
        e.timestamp = (quint64(w[1] & 0xffff) << 32) | w[0];
        e.previous = (quint64(w[1] >> 16) << 32) | w[7];
        e.channel = int(w[2] >> 27);
        e.baselineA = w[2] & 0xffffff;
        e.baselineB = w[3];
        e.sl1 = w[4];
        e.sg = w[5];
        e.sl2 = w[6];
        auto &c = coeff[e.channel];
        quint64 dt = (e.timestamp - e.previous) & 0xffffffffffffULL;
        double candidate = c.vdc;
        if (dt > 2 * c.bl + c.sl + c.sg)
            candidate = (e.baselineA - c.abl2 * e.baselineB) / (c.bl - c.abl2 * c.bl);
        else if (dt > c.bl + c.sl + c.sg)
            candidate = (e.baselineB - c.abl * e.sl1) / (c.bl - c.abl * c.sl);
        if (std::isfinite(candidate))
            c.vdc = candidate;
        e.energy = c.a0 * e.sl1 + c.ag * e.sg + c.a1 * e.sl2 -
                   (c.a0 * c.vdc * c.sl + c.ag * c.vdc * c.sg + c.a1 * c.vdc * c.sl);
        if (!std::isfinite(e.energy))
            throw Error("Non-finite reconstructed energy");
        result.push_back(e);
        offset += 32;
    }
    pending.remove(0, offset);
    return result;
}
} // namespace daq
