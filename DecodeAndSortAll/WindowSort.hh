#pragma once
#include "Sort.hh"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace r5560 {

// Rolling in-memory sorting. The caller reads all input modules to a common
// time frontier before flush(). Each flush writes an early fraction and retains
// the tail for the next read window. Unlike ExternalSort, this cannot repair
// arbitrary late input: once an event is emitted, an older key is an error.
class WindowSort {
    std::vector<Event> buffer_;
    std::size_t limit_, peak_ = 0;
    double fraction_;
    std::uint64_t cycles_ = 0, written_ = 0;
    Event last_{};
    bool haveLast_ = false, failed_ = false;

    void usable() const {
        if (failed_) throw std::runtime_error("Window sort cannot resume after an output failure");
    }
    void checkOrder(const Event &event) const {
        if (haveLast_ && lessEvent(event, last_))
            throw std::runtime_error(
                "Late event exceeds rolling sort retention: Mod " + std::to_string(event.module) +
                " Ch " + std::to_string(event.channel) + " record " + std::to_string(event.record) +
                " time_ns=" + std::to_string(event.timeNs) +
                "; last output Mod " + std::to_string(last_.module) +
                " record " + std::to_string(last_.record) +
                " time_ns=" + std::to_string(last_.timeNs) +
                ". Increase --window-ns or retain a larger tail (lower output fraction), "
                "or use --sort-mode external. No event was silently discarded.");
    }
    template<class Sink> void emitPrefix(std::size_t count, Sink &sink) {
        for (std::size_t i = 0; i < count; ++i) {
            checkOrder(buffer_[i]);
            try {
                sink(buffer_[i]);
            } catch (...) {
                // Previously emitted records may already be in a ROOT basket.
                // A caller must abandon this conversion, not retry the prefix.
                failed_ = true;
                throw;
            }
            last_ = buffer_[i];
            haveLast_ = true;
            ++written_;
        }
        buffer_.erase(buffer_.begin(), buffer_.begin() + count);
        ++cycles_;
    }

  public:
    explicit WindowSort(std::size_t bytes, double fraction = 0.9)
        : limit_(bytes / sizeof(Event)), fraction_(fraction) {
        if (!limit_) throw std::invalid_argument("Window sort memory must hold at least one event");
        if (!std::isfinite(fraction_) || fraction_ <= 0 || fraction_ > 1)
            throw std::invalid_argument("Window output fraction must be in (0, 1]");
        buffer_.reserve(limit_);
    }
    void add(const Event &event) {
        usable();
        checkOrder(event);
        if (buffer_.size() >= limit_)
            throw std::runtime_error(
                "Rolling sort memory limit reached (" + std::to_string(limit_) +
                " events). Increase --memory-mib or reduce --window-ns; "
                "all modules must reach the window frontier before output.");
        buffer_.push_back(event);
        peak_ = std::max(peak_, buffer_.size());
    }
    template<class Sink> void flush(Sink sink) {
        usable();
        if (buffer_.empty()) return;
        std::sort(buffer_.begin(), buffer_.end(), lessEvent);
        auto count = static_cast<std::size_t>(double(buffer_.size()) * fraction_);
        count = std::min(buffer_.size(), std::max<std::size_t>(1, count));
        emitPrefix(count, sink);
    }
    template<class Sink> void finish(Sink sink) {
        usable();
        if (buffer_.empty()) return;
        std::sort(buffer_.begin(), buffer_.end(), lessEvent);
        emitPrefix(buffer_.size(), sink);
    }
    std::size_t size() const { return buffer_.size(); }
    std::size_t peakEvents() const { return peak_; }
    std::uint64_t cycles() const { return cycles_; }
    std::uint64_t written() const { return written_; }
};

} // namespace r5560
