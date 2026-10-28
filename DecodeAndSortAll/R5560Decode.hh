#pragma once

#include "nlohmann/json.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

namespace r5560 {

struct Header {
    nlohmann::json json;
    std::uint32_t module = 0, daqId = 0, runId = 0, clockHz = 0;
    std::uint64_t dataOffset = 0, payloadBytes = 0;
};

struct Event {
    std::uint64_t timestamp = 0, previous = 0, record = 0;
    std::int64_t timeNs = 0;
    std::uint32_t module = 0, daqId = 0, runId = 0, channel = 0;
    std::uint32_t baselineA = 0, baselineB = 0, sl1 = 0, sg = 0, sl2 = 0;
    double energy = 0, baseline = 0;
};

// GUI5560 list-mode files only. Decode in source order before sorting: the
// baseline estimate has per-channel state. No guessed alignment or epoch repair.
class Reader {
  public:
    explicit Reader(const std::string &path);
    const Header &header() const { return header_; }
    const std::array<std::uint64_t, 32> &counts() const { return counts_; }
    bool next(Event &event);

  private:
    struct Coeff {
        double a0 = 0, ag = 0, a1 = 0, abl = 0, abl2 = 0;
        double sl = 0, sg = 0, bl = 0, vdc = 0;
    };
    [[noreturn]] void fail(const std::string &message, std::uint64_t offset) const;
    std::string path_;
    std::ifstream stream_;
    Header header_;
    std::array<Coeff, 32> coeff_{};
    std::array<std::uint64_t, 32> counts_{};
    std::array<std::uint64_t, 32> lastTimestamp_{};
    std::array<char, 32 * 8192> buffer_{};
    std::size_t buffered_ = 0, position_ = 0;
    std::uint64_t bytesRead_ = 0, record_ = 0;
};

} // namespace r5560
