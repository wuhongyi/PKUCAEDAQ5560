#pragma once
#include "Config.hh"
#include <vector>

namespace daq {
struct Event {
    quint64 timestamp = 0, previous = 0;
    quint32 baselineA = 0, baselineB = 0, sl1 = 0, sg = 0, sl2 = 0;
    int channel = 0;
    double energy = 0;
};
// Carries incomplete 32-byte events across FIFO responses. No resynchronization guess.
class EventDecoder {
  public:
    explicit EventDecoder(const ModuleSettings &settings = ModuleSettings());
    std::vector<Event> feed(const QByteArray &bytes);
    int pendingBytes() const {
        return pending.size();
    }

  private:
    struct Coeff {
        double a0, ag, a1, abl, abl2, sl, sg, bl, vdc = 0;
    };
    std::array<Coeff, Channels> coeff;
    QByteArray pending;
};
quint32 word(const char *p);
void appendWord(QByteArray &data, quint32 value);
} // namespace daq
