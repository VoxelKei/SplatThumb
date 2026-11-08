#pragma once

#include <chrono>

namespace splatthumb {

// A point in time after which work must stop.
class Deadline {
public:
    using Clock = std::chrono::steady_clock;

    explicit Deadline(Clock::time_point at) noexcept : at_(at) {}

    static Deadline after(Clock::duration d) noexcept { return Deadline(Clock::now() + d); }

    bool expired() const noexcept { return Clock::now() >= at_; }

    // Earlier deadlines compare less, so std::min picks the one that comes first.
    friend bool operator<(const Deadline& l, const Deadline& r) noexcept { return l.at_ < r.at_; }

private:
    Clock::time_point at_;
};

} // namespace splatthumb
