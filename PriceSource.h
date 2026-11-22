#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include "engine/TradingEngine.h"

namespace tx {

// Where the bots get a "reference price" from. (Abstract base class: a live market-data feed could be added as another subclass.)
class PriceSource {
public:
    virtual ~PriceSource() = default;
    virtual Price get(const std::string& symbol) = 0;            // cents
};

// Simulated market: a slow random walk that is pulled back towards the base price. Needs no internet.
class SimulatedPriceSource : public PriceSource {
public:
    SimulatedPriceSource() {
        for (const auto& s : TradingEngine::symbols()) p_[s] = base_[s] = (double)TradingEngine::basePrice(s);
    }
    Price get(const std::string& s) override {
        std::lock_guard<std::mutex> g(m_);
        return (Price)std::llround(p_[s]);
    }
    void step(std::mt19937& rng) {
        std::normal_distribution<double> n(0.0, 0.0004);
        std::lock_guard<std::mutex> g(m_);
        for (auto& kv : p_) {
            const double base = base_[kv.first];
            kv.second += kv.second * n(rng) + 0.02 * (base - kv.second);
            kv.second = std::max(base * 0.7, std::min(base * 1.3, kv.second));
        }
    }
private:
    std::mutex m_;
    std::map<std::string, double> p_, base_;
};

}  // namespace tx
