#pragma once
#include "R5560Decode.hh"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <queue>
#include <vector>

namespace r5560 {
// Temporary records are private to this process/build; never a public file format.
inline bool lessEvent(const Event &a, const Event &b) {
    if (a.timeNs != b.timeNs) return a.timeNs < b.timeNs;
    if (a.module != b.module) return a.module < b.module;
    return a.record < b.record;
}
class ExternalSort {
    std::filesystem::path directory;
    size_t limit, serial = 0;
    std::vector<Event> buffer;
    std::vector<std::filesystem::path> runs;
    static bool read(std::istream &s, Event &e) {
        s.read(reinterpret_cast<char *>(&e), sizeof(e));
        if (s.gcount() == sizeof(e)) return true;
        if (s.bad() || s.gcount() || !s.eof()) throw std::runtime_error("Temporary sort file read failed");
        return false;
    }
    static void write(std::ostream &s, const Event &e) {
        if (!s.write(reinterpret_cast<const char *>(&e), sizeof(e)))
            throw std::runtime_error("Temporary sort file write failed (disk full?)");
    }
    std::filesystem::path nextPath() { return directory / (std::to_string(serial++) + ".sort"); }
    template<class Sink> void merge(const std::vector<std::filesystem::path> &paths, Sink sink) {
        struct Item { Event event; size_t input; };
        auto later = [](const Item &a, const Item &b) { return lessEvent(b.event, a.event); };
        std::priority_queue<Item, std::vector<Item>, decltype(later)> queue(later);
        std::vector<std::ifstream> inputs;
        for (const auto &p : paths) {
            inputs.emplace_back(p, std::ios::binary);
            if (!inputs.back()) throw std::runtime_error("Cannot open temporary sort file");
            Event e{};
            if (read(inputs.back(), e)) queue.push({e, inputs.size()-1});
        }
        while (!queue.empty()) {
            auto item = queue.top(); queue.pop(); sink(item.event);
            if (read(inputs[item.input], item.event)) queue.push(item);
        }
    }
    void flush() {
        if (buffer.empty()) return;
        std::sort(buffer.begin(), buffer.end(), lessEvent);
        const auto p = nextPath();
        std::ofstream out(p, std::ios::binary);
        if (!out) throw std::runtime_error("Cannot create temporary sort file");
        for (const auto &e : buffer) write(out, e);
        out.close();
        if (!out) throw std::runtime_error("Cannot finish temporary sort file");
        runs.push_back(p); buffer.clear();
    }
  public:
    ExternalSort(const std::filesystem::path &dir, size_t bytes) : directory(dir), limit(std::max<size_t>(1, bytes/sizeof(Event))) {
        buffer.reserve(limit);
    }
    void add(const Event &e) { buffer.push_back(e); if (buffer.size() >= limit) flush(); }
    template<class Sink> void finish(Sink sink) {
        if (runs.empty()) {
            std::sort(buffer.begin(), buffer.end(), lessEvent);
            for (const auto &e : buffer) sink(e);
            buffer.clear(); return;
        }
        flush();
        // Bound open file descriptors even for multi-day acquisitions.
        constexpr size_t fanIn = 32;
        while (runs.size() > fanIn) {
            std::vector<std::filesystem::path> next;
            for (size_t begin = 0; begin < runs.size(); begin += fanIn) {
                const auto end = std::min(runs.size(), begin + fanIn);
                std::vector<std::filesystem::path> group(runs.begin()+begin, runs.begin()+end);
                const auto p = nextPath();
                std::ofstream out(p, std::ios::binary);
                if (!out) throw std::runtime_error("Cannot create merge file");
                merge(group, [&](const Event &e) { write(out, e); });
                out.close();
                if (!out) throw std::runtime_error("Cannot finish merge file");
                next.push_back(p);
                for (const auto &old : group) std::filesystem::remove(old);
            }
            runs = std::move(next);
        }
        merge(runs, sink);
        for (const auto &p : runs) std::filesystem::remove(p);
        runs.clear();
    }
};
} // namespace r5560
