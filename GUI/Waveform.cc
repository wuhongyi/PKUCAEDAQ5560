#include "Waveform.hh"
#include <algorithm>
#include <cmath>

namespace daq {
void WaveformRequest::validate() const {
    if (channel < 0 || channel >= Channels || length < 1 || length > 16384 || pretrigger < 0 ||
        pretrigger >= length || timeoutMs < 100 || timeoutMs > 60000)
        throw Error("Invalid waveform channel, length, pretrigger or timeout");
}
void Waveform::append(const QVector<quint32> &words) {
    for (auto value : words) {
        if (value & ~0x00013fffu)
            throw Error("Invalid waveform sample format");
        samples.append(quint16(value & 0x3fff));
        triggers.append(quint8((value >> 16) & 1));
    }
}
WaveformStatistics waveformStatistics(const QVector<quint16> &samples, int first, int count) {
    if (first < 0 || count < 1 || first > samples.size() - count)
        throw Error("Select a valid baseline sample range");
    WaveformStatistics s;
    s.minimum = s.maximum = samples[first];
    double m2 = 0;
    for (int i = 0; i < count; ++i) {
        const double x = samples[first + i], delta = x - s.mean;
        s.mean += delta / (i + 1);
        m2 += delta * (x - s.mean);
        s.minimum = std::min(s.minimum, int(x));
        s.maximum = std::max(s.maximum, int(x));
    }
    s.rmsNoise = std::sqrt(m2 / count); // Population standard deviation, after removing DC.
    return s;
}
void Waveform::saveCsv(const QString &path) const {
    if (samples.isEmpty() || triggers.size() != samples.size())
        throw Error("No complete waveform to export");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Error(file.errorString());
    QTextStream out(&file);
    out.setLocale(QLocale::c());
    out << "# GDDAQ-R5560 waveform, ADC counts, 8 ns/sample\n"
        << "# module=" << module << ",channel=" << channel << ",sequence=" << sequence
        << ",pretrigger=" << pretrigger << ",forced=" << int(forced)
        << ",simulated=" << int(simulated) << "\n"
        << "sample,time_ns,adc,trigger\n";
    for (int i = 0; i < samples.size(); ++i)
        out << i << ',' << (i - pretrigger) * 8 << ',' << samples[i] << ',' << int(triggers[i])
            << '\n';
    out.flush();
    if (out.status() != QTextStream::Ok || !file.commit())
        throw Error("Cannot save waveform CSV: " + file.errorString());
}
} // namespace daq
