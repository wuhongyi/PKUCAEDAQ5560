#include "R5560Decode.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace r5560 {
namespace {
constexpr std::uint64_t TimestampMask = (std::uint64_t(1) << 48) - 1;
constexpr std::uint32_t MaximumHeaderBytes = 16 * 1024 * 1024;

std::uint32_t word(const char *p) {
    const auto *b = reinterpret_cast<const unsigned char *>(p);
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) |
           (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
}

std::uint32_t uint32(const nlohmann::json &value, const std::string &field) {
    if (!value.is_number())
        throw std::runtime_error(field + ": unsigned JSON integer required");
    const double number = value.get<double>();
    if (!std::isfinite(number) || number < 0 || number > 4294967295.0 ||
        std::floor(number) != number)
        throw std::runtime_error(field + ": expected integer in 0..4294967295");
    return static_cast<std::uint32_t>(number);
}

std::uint32_t field(const nlohmann::json &object, const char *name) {
    return uint32(object.at(name), name);
}
} // namespace

[[noreturn]] void Reader::fail(const std::string &message, std::uint64_t offset) const {
    throw std::runtime_error(path_ + ": byte offset " + std::to_string(offset) + ": " + message);
}

Reader::Reader(const std::string &path) : path_(path), stream_(path, std::ios::binary) {
    if (!stream_) fail("Cannot open input file", 0);
    char prefix[12];
    stream_.read(prefix, sizeof(prefix));
    if (stream_.gcount() != sizeof(prefix)) fail("Truncated file header", 0);
    if (std::memcmp(prefix, "R5560D1\n", 8) != 0)
        fail("Expected GUI5560 magic R5560D1; raw recovery/partial-reply files are unsupported", 0);
    const auto jsonSize = word(prefix + 8);
    if (jsonSize == 0 || jsonSize > MaximumHeaderBytes)
        fail("JSON header length must be in 1..16777216 bytes", 8);
    std::string jsonText(jsonSize, '\0');
    stream_.read(&jsonText[0], static_cast<std::streamsize>(jsonSize));
    if (stream_.gcount() != static_cast<std::streamsize>(jsonSize))
        fail("Truncated JSON header", 12);
    header_.dataOffset = 12 + std::uint64_t(jsonSize);

    try {
        header_.json = nlohmann::json::parse(jsonText);
        const auto &h = header_.json;
        if (!h.is_object() || h.at("schema") != "r5560-list-v1" ||
            field(h, "event_bytes") != 32 || field(h, "event_format") != 2 ||
            h.at("byte_order") != "little")
            throw std::runtime_error("Unsupported schema, event format, size or byte order");
        header_.module = field(h, "module_index");
        header_.daqId = field(h, "daq_id");
        header_.runId = field(h, "run_id");
        header_.clockHz = field(h, "clock_hz");
        if (header_.clockHz != 125000000)
            throw std::runtime_error("Only the 125 MHz / 8 ns R5560 format is supported");
        const auto api = field(h, "api_version");
        if (api != 0x00020000 && api != 0x00030000)
            throw std::runtime_error("Unsupported FPGA API version");
        if (!h.at("simulated").is_boolean())
            throw std::runtime_error("simulated must be a JSON boolean");
        const auto &settings = h.at("settings");
        const auto sync = field(settings, "sync_config");
        if ((sync & ~0x7fu) || ((sync >> 1) & 3) > 2 || ((sync >> 3) & 3) > 2 ||
            ((sync & 0x21) == 0x21 && ((sync >> 1) & 3) == ((sync >> 3) & 3)))
            throw std::runtime_error("Invalid SYNC_CONFIG");
        if (!(sync & 1) && !(sync & 64))
            throw std::runtime_error("Event format 2 requires trigger timestamp mode");
        if (!field(settings, "channel_enable"))
            throw std::runtime_error("At least one channel must be enabled");
        const auto &rows = settings.at("channels");
        if (!rows.is_array() || rows.size() != 32)
            throw std::runtime_error("Exactly 32 channel parameter objects are required");
        static constexpr const char *names[] = {
            "POL", "THRE", "TRIGFL", "TRIGFLFG", "BLLEN", "XIAOFFSET", "XIASL", "XIASG", "TAU"};
        static constexpr std::uint32_t minimum[] = {0, 0, 1, 1, 1, 0, 1, 1, 1};
        static constexpr std::uint32_t maximum[] = {
            1, 16383, 127, 127, 1023, 255, 1023, 1023, 0xffffffffu};
        for (std::size_t ch = 0; ch < 32; ++ch) {
            std::array<std::uint32_t, 9> p{};
            for (std::size_t i = 0; i < p.size(); ++i) {
                p[i] = uint32(rows[ch].at(names[i]), "Ch " + std::to_string(ch) + " " + names[i]);
                if (p[i] < minimum[i] || p[i] > maximum[i])
                    throw std::runtime_error("Ch " + std::to_string(ch) + " " + names[i] +
                                             ": parameter out of supported range");
            }
            if (p[3] < p[2]) throw std::runtime_error("TRIGFLFG must be >= TRIGFL");
            // Match GUI5560/EventDecoder.cc exactly: TAU is ns; all window
            // lengths are samples. VDC starts at zero independently per channel.
            auto &c = coeff_[ch];
            c.sl = p[6]; c.sg = p[7]; c.bl = p[4];
            const double x = -8.0 / p[8];
            const double oneMinusB = -std::expm1(x), decaySl = std::exp(x * c.sl);
            c.a1 = oneMinusB / (-std::expm1(x * c.sl));
            c.a0 = -c.a1 * decaySl;
            c.ag = oneMinusB;
            const double decayBl = std::exp(x * c.bl);
            c.abl = (-std::expm1(x * c.bl)) / (decayBl * (-std::expm1(x * c.sl)));
            c.abl2 = 1.0 / decayBl;
        }
    } catch (const std::exception &e) {
        fail(std::string("Invalid JSON header: ") + e.what(), 12);
    }

    stream_.seekg(0, std::ios::end);
    const auto end = stream_.tellg();
    if (end == std::streampos(-1)) fail("Cannot determine input size", header_.dataOffset);
    const auto size = static_cast<std::uint64_t>(end);
    if (size < header_.dataOffset) fail("Truncated file", size);
    header_.payloadBytes = size - header_.dataOffset;
    if (header_.payloadBytes % 32)
        fail("Truncated final 32-byte event; no alignment repair is attempted",
             header_.dataOffset + header_.payloadBytes - header_.payloadBytes % 32);
    stream_.seekg(static_cast<std::streamoff>(header_.dataOffset), std::ios::beg);
    if (!stream_) fail("Cannot seek to event data", header_.dataOffset);
}

bool Reader::next(Event &event) {
    if (position_ == buffered_) {
        if (bytesRead_ == header_.payloadBytes) {
            // Do not silently ignore data appended while decoding an active run.
            if (stream_.peek() != std::char_traits<char>::eof())
                fail("Input grew during decoding; stop acquisition before converting",
                     header_.dataOffset + bytesRead_);
            if (stream_.bad()) fail("I/O error at end of input", header_.dataOffset + bytesRead_);
            return false;
        }
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(
            buffer_.size(), header_.payloadBytes - bytesRead_));
        stream_.read(buffer_.data(), static_cast<std::streamsize>(n));
        if (stream_.gcount() != static_cast<std::streamsize>(n))
            fail("Input was truncated or could not be read", header_.dataOffset + bytesRead_);
        buffered_ = n; position_ = 0; bytesRead_ += n;
    }
    const auto offset = header_.dataOffset + record_ * 32;
    const char *data = buffer_.data() + position_;
    std::array<std::uint32_t, 8> w{};
    for (std::size_t i = 0; i < w.size(); ++i) w[i] = word(data + 4 * i);
    if ((w[2] & 0x07000000u) || ((w[3] | w[4] | w[5] | w[6]) & 0xff000000u))
        fail("Nonzero reserved event bits; alignment or firmware format mismatch", offset);
    Event e;
    e.module = header_.module; e.daqId = header_.daqId; e.runId = header_.runId;
    e.record = record_;
    e.timestamp = (std::uint64_t(w[1] & 0xffff) << 32) | w[0];
    e.timeNs = static_cast<std::int64_t>(e.timestamp * 8);
    e.previous = (std::uint64_t(w[1] >> 16) << 32) | w[7];
    e.channel = w[2] >> 27;
    e.baselineA = w[2] & 0xffffff;
    e.baselineB = w[3]; e.sl1 = w[4]; e.sg = w[5]; e.sl2 = w[6];
    if (counts_[e.channel] && e.timestamp < lastTimestamp_[e.channel])
        fail("Ch " + std::to_string(e.channel) +
             ": timestamp moved backwards (48-bit rollover, epoch reset or corrupted/replayed data); "
             "epochs are not inferred", offset);

    auto &c = coeff_[e.channel];
    const auto dt = (e.timestamp - e.previous) & TimestampMask;
    double candidate = c.vdc;
    if (dt > 2 * c.bl + c.sl + c.sg)
        candidate = (e.baselineA - c.abl2 * e.baselineB) / (c.bl - c.abl2 * c.bl);
    else if (dt > c.bl + c.sl + c.sg)
        candidate = (e.baselineB - c.abl * e.sl1) / (c.bl - c.abl * c.sl);
    if (std::isfinite(candidate)) c.vdc = candidate;
    e.baseline = c.vdc;
    e.energy = c.a0 * e.sl1 + c.ag * e.sg + c.a1 * e.sl2 -
               (c.a0 * c.vdc * c.sl + c.ag * c.vdc * c.sg + c.a1 * c.vdc * c.sl);
    if (!std::isfinite(e.energy)) fail("Non-finite reconstructed energy", offset);
    ++counts_[e.channel]; lastTimestamp_[e.channel] = e.timestamp;
    ++record_; position_ += 32;
    event = e;
    return true;
}

} // namespace r5560
