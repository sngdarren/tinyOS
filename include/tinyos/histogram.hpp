#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace tinyos {

// Latency histogram.
//
// The reason this exists rather than reporting an average: in latency work the
// mean is close to meaningless. A cache miss on 1-in-1000 allocations barely
// moves the mean but dominates p99.9, and p99.9 is what an interviewer will
// ask about. Always report tails.
class Histogram {
public:
    explicit Histogram(std::string label = {}) : label_(std::move(label)) {}

    void add(double sample) { samples_.push_back(sample); }

    void reserve(std::size_t n) { samples_.reserve(n); }
    void clear() { samples_.clear(); sorted_ = false; }

    [[nodiscard]] std::size_t count() const { return samples_.size(); }
    [[nodiscard]] bool empty() const { return samples_.empty(); }

    // Nearest-rank percentile. `p` in [0, 100].
    [[nodiscard]] double percentile(double p) {
        if (samples_.empty()) {
            return 0.0;
        }
        sort();
        auto const rank = static_cast<std::size_t>(p / 100.0 * static_cast<double>(samples_.size()));
        return samples_[std::min(rank, samples_.size() - 1)];
    }

    [[nodiscard]] double min() { return percentile(0.0); }
    [[nodiscard]] double max() {
        if (samples_.empty()) {
            return 0.0;
        }
        sort();
        return samples_.back();
    }

    [[nodiscard]] double mean() const {
        if (samples_.empty()) {
            return 0.0;
        }
        double total = 0.0;
        for (double s : samples_) {
            total += s;
        }
        return total / static_cast<double>(samples_.size());
    }

    // One row per histogram, so several can be stacked into a comparison table.
    void print_row(char const* unit = "ns") {
        std::printf("%-24s %8zu %9.1f %9.1f %9.1f %9.1f %9.1f %9.1f  %s\n",
                    label_.c_str(), count(), mean(), percentile(50.0), percentile(90.0),
                    percentile(99.0), percentile(99.9), max(), unit);
    }

    static void print_header() {
        std::printf("%-24s %8s %9s %9s %9s %9s %9s %9s\n",
                    "name", "samples", "mean", "p50", "p90", "p99", "p99.9", "max");
        std::printf("%s\n", std::string(100, '-').c_str());
    }

private:
    void sort() {
        if (!sorted_) {
            std::sort(samples_.begin(), samples_.end());
            sorted_ = true;
        }
    }

    std::string label_;
    std::vector<double> samples_;
    bool sorted_ = false;
};

}  // namespace tinyos