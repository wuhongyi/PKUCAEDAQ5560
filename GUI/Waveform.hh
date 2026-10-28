#pragma once
#include "Config.hh"

namespace daq {
struct WaveformRequest {
    int channel = 0, length = 16384, pretrigger = 2048, timeoutMs = 5000;
    bool force = false;
    void validate() const;
};
struct Waveform {
    int module = 0, channel = 0, pretrigger = 0;
    quint32 sequence = 0;
    bool forced = false, simulated = false;
    QVector<quint16> samples;
    QVector<quint8> triggers;
    void append(const QVector<quint32> &words);
    void saveCsv(const QString &path) const;
};
struct WaveformStatistics {
    double mean = 0, rmsNoise = 0;
    int minimum = 0, maximum = 0;
};
WaveformStatistics waveformStatistics(const QVector<quint16> &samples, int first, int count);
} // namespace daq
Q_DECLARE_METATYPE(daq::Waveform)
