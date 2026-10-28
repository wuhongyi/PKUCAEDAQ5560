#include "R5560Decode.hh"
#include "Sort.hh"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
using Words = std::array<std::uint32_t, 8>;
namespace {
unsigned checks = 0;
void require(bool condition, const std::string &message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const std::string &message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <=
            1e-10 * std::max(1.0, std::abs(expected)), message);
}
template<class F> void throws(F action, const std::string &needle) {
    try { action(); }
    catch (const std::exception &e) {
        require(std::string(e.what()).find(needle) != std::string::npos,
                "Expected error containing '" + needle + "', got: " + e.what());
        return;
    }
    throw std::runtime_error("Expected an exception containing: " + needle);
}
struct Scratch {
    fs::path path;
    Scratch() {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        for (unsigned i = 0; i < 100; ++i) {
            path = fs::temp_directory_path() / ("r5560-decode-test-" + std::to_string(stamp) + "-" + std::to_string(i));
            if (fs::create_directory(path)) return;
        }
        throw std::runtime_error("Cannot create a fresh test directory");
    }
    ~Scratch() { std::error_code ec; fs::remove_all(path, ec); }
};

json metadata() {
    json p = {{"POL", 1}, {"THRE", 200}, {"TRIGFL", 12}, {"TRIGFLFG", 12},
              {"BLLEN", 128}, {"XIAOFFSET", 10}, {"XIASL", 240}, {"XIASG", 30}, {"TAU", 30000}};
    json channels = json::array();
    for (unsigned i = 0; i < 32; ++i) channels.push_back(p);
    return {{"schema", "r5560-list-v1"}, {"module_index", 7}, {"daq_id", 0x1234},
            {"run_id", 0xfedcba98u}, {"api_version", 0x30000}, {"event_format", 2},
            {"event_bytes", 32}, {"byte_order", "little"}, {"clock_hz", 125000000},
            {"simulated", false}, {"settings", {{"sync_config", 3},
             {"channel_enable", 0xffffffffu}, {"channels", channels}}}};
}
void put32(std::ostream &s, std::uint32_t w) {
    char b[4];
    for (unsigned i = 0; i < 4; ++i) b[i] = static_cast<char>((w >> (i * 8)) & 0xff);
    s.write(b, 4);
}
Words words(std::uint64_t timestamp, std::uint64_t previous, unsigned channel,
            std::uint32_t ba = 10000, std::uint32_t bb = 11000,
            std::uint32_t sl1 = 20000, std::uint32_t sg = 3000, std::uint32_t sl2 = 22000) {
    return {std::uint32_t(timestamp), std::uint32_t(((previous >> 32) << 16) | (timestamp >> 32)),
            (channel << 27) | ba, bb, sl1, sg, sl2, std::uint32_t(previous)};
}
void fixture(const fs::path &path, const json &h, const std::vector<Words> &events = {}) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::string text = h.dump();
    out.write("R5560D1\n", 8); put32(out, static_cast<std::uint32_t>(text.size()));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    for (const auto &event : events) for (auto w : event) put32(out, w);
    out.close();
    if (!out) throw std::runtime_error("Cannot write fixture");
}

void checkFormat(const fs::path &dir) {
    const auto file = dir / "fields.bin";
    const auto h = metadata();
    constexpr std::uint64_t ts = 0xfedcba987654ULL, prev = 0x123456789abcULL;
    fixture(file, h, {words(ts, prev, 31, 0x123456, 0xabcdef, 0x654321, 0x789abc, 0xffffff)});
    r5560::Reader reader(file.string());
    const auto &header = reader.header();
    require(header.json == h, "Header JSON was not preserved");
    require(header.dataOffset == 12 + h.dump().size() && header.payloadBytes == 32, "File offsets/size");
    require(header.module == 7 && header.daqId == 0x1234 && header.runId == 0xfedcba98u &&
            header.clockHz == 125000000, "Header identities");
    r5560::Event e;
    require(reader.next(e), "Missing event");
    require(e.timestamp == ts && e.previous == prev && e.timeNs == static_cast<std::int64_t>(ts * 8),
            "48-bit timestamp/previous or ns conversion");
    require(e.channel == 31 && e.baselineA == 0x123456 && e.baselineB == 0xabcdef &&
            e.sl1 == 0x654321 && e.sg == 0x789abc && e.sl2 == 0xffffff, "Integral/channel bit fields");
    require(e.module == 7 && e.daqId == 0x1234 && e.runId == 0xfedcba98u && e.record == 0, "Event identities");
    require(reader.counts()[31] == 1 && reader.counts()[0] == 0, "Channel counts");
    require(!reader.next(e), "Extra event");

    auto avalon = h; avalon["api_version"] = 0x20000;
    avalon["settings"]["sync_config"] = 64;
    fixture(file, avalon, {words(1, 0, 0)});
    r5560::Reader old(file.string());
    require(old.next(e) && e.previous == 0, "API 2 software mode or first previous=0");
    fixture(file, h);
    r5560::Reader empty(file.string());
    require(!empty.next(e) && empty.header().payloadBytes == 0, "Valid empty module file");
}

void checkFailures(const fs::path &dir) {
    const auto file = dir / "bad.bin";
    const auto h = metadata();
    auto badHeader = [&](json bad, const std::string &message) {
        fixture(file, bad);
        throws([&] { r5560::Reader input(file.string()); }, message);
    };
    for (const auto &key : {"schema", "event_bytes", "event_format", "byte_order"}) {
        auto bad = h; bad[key] = "unsupported"; badHeader(bad, "Invalid JSON header");
    }
    auto bad = h; bad["clock_hz"] = 100000000; badHeader(bad, "125 MHz");
    bad = h; bad["api_version"] = 0x40000; badHeader(bad, "API version");
    bad = h; bad["module_index"] = -1; badHeader(bad, "0..4294967295");
    bad = h; bad["daq_id"] = 1.25; badHeader(bad, "0..4294967295");
    bad = h; bad["simulated"] = 0; badHeader(bad, "JSON boolean");
    bad = h; bad["settings"]["channels"].erase(31); badHeader(bad, "32 channel");
    bad = h; bad["settings"]["channels"][31]["XIASL"] = 0; badHeader(bad, "out of supported range");
    bad = h; bad["settings"]["channels"][1]["TAU"] = 0; badHeader(bad, "out of supported range");
    bad = h; bad["settings"]["channels"][4]["TRIGFLFG"] = 11; badHeader(bad, "TRIGFLFG");
    bad = h; bad["settings"]["sync_config"] = 0; badHeader(bad, "trigger timestamp");
    bad = h; bad["settings"]["channel_enable"] = 0; badHeader(bad, "enabled");

    { std::ofstream s(file, std::ios::binary); s << "not-a-file-magic"; }
    throws([&] { r5560::Reader input(file.string()); }, "magic");
    { std::ofstream s(file, std::ios::binary); s << "R5560D1\n"; }
    throws([&] { r5560::Reader input(file.string()); }, "Truncated file header");
    { std::ofstream s(file, std::ios::binary); s.write("R5560D1\n", 8); put32(s, 0); }
    throws([&] { r5560::Reader input(file.string()); }, "header length");
    { std::ofstream s(file, std::ios::binary); s.write("R5560D1\n", 8); put32(s, 16777217); }
    throws([&] { r5560::Reader input(file.string()); }, "header length");
    { std::ofstream s(file, std::ios::binary); s.write("R5560D1\n", 8); put32(s, 10); s << "{}"; }
    throws([&] { r5560::Reader input(file.string()); }, "Truncated JSON");
    { std::ofstream s(file, std::ios::binary); s.write("R5560D1\n", 8); put32(s, 4); s << "{???"; }
    throws([&] { r5560::Reader input(file.string()); }, "Invalid JSON");

    fixture(file, h, {words(1000, 0, 0)});
    { std::ofstream out(file, std::ios::binary | std::ios::app); out.put('x'); }
    throws([&] { r5560::Reader input(file.string()); }, "Truncated final");
    for (unsigned i = 2; i <= 6; ++i) {
        auto w = words(1000, 0, 0); w[i] |= 1u << 24;
        fixture(file, h, {w});
        r5560::Reader input(file.string()); r5560::Event event;
        throws([&] { input.next(event); }, "byte offset " + std::to_string(input.header().dataOffset));
    }
    fixture(file, h, {words(1000, 0, 0), words(999, 0, 0)});
    r5560::Reader backward(file.string()); r5560::Event e;
    require(backward.next(e), "First backward fixture event");
    throws([&] { backward.next(e); }, "timestamp moved backwards");
    fixture(file, h, {words((1ULL << 48) - 1, 0, 1), words(1, 0, 1)});
    r5560::Reader rollover(file.string());
    require(rollover.next(e), "First rollover fixture event");
    throws([&] { rollover.next(e); }, "48-bit rollover");
}

void checkEnergyAndOrdering(const fs::path &dir) {
    const auto file = dir / "energy.bin";
    auto h = metadata();
    // All integrals at the same constant baseline must reconstruct zero energy;
    // subsequent close triggers must retain that channel's established baseline.
    constexpr double base = 100, sl = 240, sg = 30, bl = 128, tau = 30000;
    fixture(file, h, {
        words(10000, 0, 0, 12800, 12800, 24000, 3000, 24000),
        words(10001, 10000, 0, 123, 456, 24000, 3000, 24000),
        words(7, 6, 1, 12800, 12800, 24000, 3000, 24000),
        words(10002, 10001, 0, 123, 456, 24000, 3000, 25000),
        words(20000, 20000 - 500, 0, 0, 14100, 24000, 3000, 24000),
        words(20000, 19999, 0, 0, 0, 24000, 3000, 24000)});
    r5560::Reader input(file.string()); r5560::Event e;
    require(input.next(e), "Baseline event"); near(e.baseline, base, "Constant-baseline estimate");
    near(e.energy, 0, "Constant-baseline energy");
    require(input.next(e), "Close event"); near(e.baseline, base, "Close trigger should keep VDC");
    near(e.energy, 0, "Retained-baseline energy");
    require(input.next(e) && e.timestamp == 7 && e.channel == 1, "Inter-channel backwards order must be legal");
    near(e.baseline, 0, "VDC must be independent for each channel");
    const double b = std::exp(-8.0 / tau);
    const double a1 = (1 - b) / (1 - std::pow(b, sl));
    const double a0 = -a1 * std::pow(b, sl), ag = 1 - b;
    near(e.energy, a0 * 24000 + ag * 3000 + a1 * 24000, "Independent-channel energy");
    require(input.next(e), "Pulse event"); near(e.baseline, base, "Pulse close-trigger baseline");
    near(e.energy, a1 * 1000, "Pulse reconstructed energy");
    require(input.next(e), "Middle-interval event");
    const double abl = (1 - std::pow(b, bl)) / (std::pow(b, bl) * (1 - std::pow(b, sl)));
    const double newBase = (14100 - abl * 24000) / (bl - abl * sl);
    near(e.baseline, newBase, "Intermediate trigger interval baseline formula");
    near(e.energy, a0 * 24000 + ag * 3000 + a1 * 24000 -
         (a0 * newBase * sl + ag * newBase * sg + a1 * newBase * sl), "Baseline-corrected energy formula");
    require(input.next(e) && e.timestamp == 20000, "Equal timestamps must not be dropped");
    near(e.baseline, newBase, "Retain new baseline across equal timestamp");
    require(!input.next(e) && input.counts()[0] == 5 && input.counts()[1] == 1, "Energy fixture event counts");

    // Extreme valid TAU creates non-finite baseline candidates, which GUI keeps
    // from replacing the finite historical VDC. This is intentionally not an error.
    h["settings"]["channels"][0]["TAU"] = 1;
    fixture(file, h, {words(10000, 0, 0)});
    r5560::Reader extreme(file.string()); require(extreme.next(e), "Extreme valid TAU event");
    near(e.baseline, 0, "Non-finite baseline candidate must preserve VDC");
    require(std::isfinite(e.energy), "Extreme valid TAU energy remains finite");
}

void checkBufferBoundary(const fs::path &dir) {
    const auto file = dir / "buffer.bin";
    std::vector<Words> entries;
    for (unsigned i = 0; i < 8201; ++i) entries.push_back(words(i * 5 + 10, i * 5, i % 32));
    fixture(file, metadata(), entries);
    r5560::Reader input(file.string()); r5560::Event e; std::uint64_t count = 0;
    while (input.next(e)) {
        require(e.record == count && e.timestamp == count * 5 + 10 && e.channel == count % 32,
                "Reader loses/alters events at its buffered read boundary");
        ++count;
    }
    require(count == entries.size(), "Buffered fixture total");
}

bool ordered(const r5560::Event &a, const r5560::Event &b) {
    if (a.timeNs != b.timeNs) return a.timeNs < b.timeNs;
    if (a.module != b.module) return a.module < b.module;
    return a.record < b.record;
}
void checkSort(const fs::path &dir) {
    fs::create_directory(dir / "sort");
    const auto temp = dir / "sort";
    { r5560::ExternalSort empty(temp, 1024); unsigned rows = 0;
      empty.finish([&](const r5560::Event &) { ++rows; }); require(rows == 0, "Empty sort"); }
    std::vector<r5560::Event> source;
    for (unsigned i = 0; i < 2110; ++i) {
        r5560::Event e;
        e.timestamp = 50000 - i; // deliberately different from corrected time ordering
        e.timeNs = static_cast<std::int64_t>((i * 73) % 101) - 50;
        e.module = i % 4; e.record = i;
        e.daqId = 500 + e.module; e.runId = 9; e.channel = i % 32;
        e.energy = 1.25 * i; e.baseline = 10.0; e.baselineA = i;
        source.push_back(e);
    }
    source.push_back(source[123]); // byte-identical repeated event remains present
    auto expected = source;
    std::sort(expected.begin(), expected.end(), ordered);
    auto exercise = [&](std::size_t capacity, const char *label) {
        r5560::ExternalSort sort(temp, capacity);
        for (const auto &e : source) sort.add(e);
        std::size_t pos = 0;
        sort.finish([&](const r5560::Event &e) {
            require(pos < expected.size(), std::string(label) + ": extra row");
            const auto &want = expected[pos++];
            require(e.timeNs == want.timeNs && e.module == want.module && e.record == want.record &&
                    e.timestamp == want.timestamp && e.energy == want.energy &&
                    e.daqId == want.daqId && e.baselineA == want.baselineA,
                    std::string(label) + ": ordering/payload mismatch");
        });
        require(pos == expected.size(), std::string(label) + ": missing/duplicate rows");
        require(fs::is_empty(temp), std::string(label) + ": temporary runs not removed");
    };
    exercise(sizeof(r5560::Event) * 3000, "in-memory sort");
    // >32*32 chunks: exercises two intermediate fan-in passes plus final merge.
    exercise(sizeof(r5560::Event) * 2, "external multi-pass sort");
}
} // namespace

int main() {
    try {
        Scratch scratch;
        checkFormat(scratch.path);
        checkFailures(scratch.path);
        checkEnergyAndOrdering(scratch.path);
        checkBufferBoundary(scratch.path);
        checkSort(scratch.path);
        std::cout << "PASS: " << checks << " checks (Reader format/errors/energy/buffering; external sort)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
