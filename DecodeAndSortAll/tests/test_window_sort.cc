#include "WindowSort.hh"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using r5560::Event;
using r5560::WindowSort;
namespace {
unsigned checks = 0;
void require(bool value, const std::string &message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class Action> void throws(Action action, const std::string &text) {
    try { action(); }
    catch (const std::exception &error) {
        require(std::string(error.what()).find(text) != std::string::npos,
                "Expected '" + text + "', got: " + error.what());
        return;
    }
    throw std::runtime_error("Expected exception containing: " + text);
}
Event event(std::int64_t ns, std::uint32_t module = 0, std::uint64_t record = 0,
            std::uint32_t channel = 0) {
    Event e;
    e.timeNs = ns; e.module = module; e.record = record; e.channel = channel;
    e.energy = double(record) + 0.5;
    return e;
}
auto key(const Event &e) { return std::make_tuple(e.timeNs, e.module, e.record); }
void match(const std::vector<Event> &actual, std::vector<Event> expected) {
    std::stable_sort(expected.begin(), expected.end(), [](const Event &a, const Event &b) {
        return key(a) < key(b);
    });
    require(actual.size() == expected.size(), "Event count preserved");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        require(key(actual[i]) == key(expected[i]), "Independent time/module/record order");
        require(actual[i].energy == expected[i].energy, "Payload preserved");
    }
}
void defaultsAndTail() {
    WindowSort sorter(64 * sizeof(Event));
    std::vector<Event> input, output;
    auto sink = [&](const Event &e) { output.push_back(e); };
    for (int i = 9; i >= 0; --i) { input.push_back(event(i, 0, i)); sorter.add(input.back()); }
    require(sorter.size() == 10 && sorter.peakEvents() == 10, "Buffer peak before flush");
    sorter.flush(sink);
    require(output.size() == 9 && sorter.size() == 1, "Default emits 90 percent, retains tail");
    require(output.front().timeNs == 0 && output.back().timeNs == 8, "Earliest prefix emitted");
    for (int i = 19; i >= 10; --i) { input.push_back(event(i, 0, i)); sorter.add(input.back()); }
    sorter.flush(sink);
    require(output.size() == 18 && sorter.size() == 2, "Retained tail joins next window");
    require(sorter.peakEvents() == 11 && sorter.cycles() == 2, "Peak and cycle counters");
    sorter.finish(sink);
    require(sorter.size() == 0 && sorter.written() == 20 && sorter.cycles() == 3,
            "EOF emits the remaining tail");
    match(output, input);
    sorter.finish(sink); sorter.flush(sink);
    require(sorter.written() == 20 && sorter.cycles() == 3, "Empty finish/flush is a no-op");
}
void interleavedAndDuplicateKeys() {
    WindowSort sorter(100 * sizeof(Event), 0.5);
    std::vector<Event> input, output;
    auto sink = [&](const Event &e) { output.push_back(e); };
    const std::vector<Event> first = {event(100, 2, 3), event(100, 0, 7),
        event(-40, 0, 0), event(100, 1, 9), event(100, 0, 7), event(50, 3, 1)};
    for (const auto &e : first) { input.push_back(e); sorter.add(e); }
    sorter.flush(sink);
    require(output.size() == 3, "Half of first window emitted");
    // Equal keys are legal. Neither map-style collisions nor deduplication may
    // remove these events, including an exact key repeated after an output.
    for (const auto &e : {event(100, 0, 7), event(100, 3, 0), event(100, 1, 10), event(200, 0, 20)}) {
        input.push_back(e); sorter.add(e);
    }
    sorter.finish(sink);
    match(output, input);
    require(std::count_if(output.begin(), output.end(), [](const Event &e) {
        return e.timeNs == 100 && e.module == 0 && e.record == 7;
    }) == 3, "Three identical keys survive");
}
void lateAndCapacity() {
    std::vector<Event> output;
    auto sink = [&](const Event &e) { output.push_back(e); };
    WindowSort sorter(8 * sizeof(Event), 1);
    sorter.add(event(100, 2, 9, 5)); sorter.flush(sink);
    throws([&] { sorter.add(event(99, 3, 20, 7)); }, "Mod 3 Ch 7 record 20 time_ns=99");
    throws([&] { sorter.add(event(100, 1, 100)); }, "Late event");
    throws([&] { sorter.add(event(100, 2, 8)); }, "Late event");
    require(sorter.size() == 0 && sorter.written() == 1, "Rejected late input is not retained");
    sorter.add(event(100, 2, 9, 5)); sorter.add(event(100, 3, 0)); sorter.finish(sink);
    require(output.size() == 3, "Equal or later total keys accepted after flush-all");

    WindowSort full(2 * sizeof(Event));
    full.add(event(1)); full.add(event(2));
    throws([&] { full.add(event(3)); }, "--memory-mib");
    require(full.size() == 2 && full.peakEvents() == 2 && full.written() == 0,
            "Capacity error cannot cause silent flush or data loss");
    std::vector<Event> recovered;
    full.finish([&](const Event &e) { recovered.push_back(e); });
    require(recovered.size() == 2, "Existing memory-limit contents remain valid");
    throws([] { WindowSort s(sizeof(Event)-1); }, "at least one event");
    for (double f : {0.0, -0.1, 1.01, std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN()})
        throws([f] { WindowSort s(sizeof(Event), f); }, "(0, 1]");
}
void smallFractionsAndFailure() {
    WindowSort small(10 * sizeof(Event), 0.001);
    std::vector<Event> out;
    small.add(event(2)); small.add(event(1));
    small.flush([&](const Event &e) { out.push_back(e); });
    require(out.size() == 1 && small.size() == 1 && out[0].timeNs == 1,
            "A nonempty fractional flush makes at least one-event progress");
    small.finish([&](const Event &e) { out.push_back(e); });
    require(out.size() == 2, "Final tail survives tiny output fraction");
    WindowSort failing(4 * sizeof(Event), 1);
    failing.add(event(1)); failing.add(event(2));
    unsigned calls = 0;
    throws([&] { failing.flush([&](const Event &) {
        if (++calls == 2) throw std::runtime_error("synthetic output failure");
    }); }, "synthetic output failure");
    require(failing.written() == 1, "Successful writes accounted before sink exception");
    throws([&] { failing.flush([](const Event &) {}); }, "cannot resume");
    throws([&] { failing.add(event(3)); }, "cannot resume");
}
void repeatedWindows() {
    WindowSort sorter(160 * sizeof(Event), 0.8);
    std::vector<Event> input, output;
    auto sink = [&](const Event &e) { output.push_back(e); };
    for (int window = 0; window < 100; ++window) {
        // Deliberately reverse time/module order within each read window.
        // Gaps between windows make the retained-tail assumption valid.
        for (int i = 95; i >= 0; --i) {
            auto e = event(window * 1000 + i/4, unsigned(i%4), unsigned(window*100+i));
            input.push_back(e); sorter.add(e);
        }
        sorter.flush(sink);
        require(sorter.size() > 0 && sorter.size() <= 25, "Tail remains bounded across windows");
    }
    sorter.finish(sink);
    require(sorter.written() == 9600 && sorter.peakEvents() <= 121, "Long stream stays memory bounded");
    match(output, input);
}
} // namespace

int main() {
    try {
        defaultsAndTail(); interleavedAndDuplicateKeys(); lateAndCapacity();
        smallFractionsAndFailure(); repeatedWindows();
        std::cout << "PASS: " << checks << " checks (rolling window retention/order/duplicates/capacity/EOF)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
