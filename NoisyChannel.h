//
// Created by Mike Zagar on 2026-03-25.
//

#ifndef BB84_NOISY_CHANNEL_H
#define BB84_NOISY_CHANNEL_H

#pragma once

#include <vector>
#include <random>
#include <stdexcept>
#include <string>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <memory>
#include <numeric>


struct ChannelMetrics {
    std::size_t      total_bits     = 0.0;
    std::size_t      error_count    = 0;
    double           expected_qber  = 0.0;
    double           actual_qber    = 0.0;
    std::vector<std::size_t> error_positions;

    ChannelMetrics()                                   = default;
    ChannelMetrics(const ChannelMetrics&)              = default;
    ChannelMetrics& operator = (const ChannelMetrics&) = default;
    ChannelMetrics(ChannelMetrics&&)                   = default;
    ChannelMetrics& operator = (ChannelMetrics&&)      = default;

    void print() const {
        std::cout << "\n-- Channel Metrics ---------------------\n";
        std::cout << "  Total bits transmitted : " << total_bits     << "\n";
        std::cout << "  Errors introduced      : " << error_count    << "\n";
        std::cout << "  Expected QBER          : " << std::fixed << std::setprecision(4)
         << expected_qber  * 100 << "\n";
        std::cout << "  Actual QBER            : " << std::fixed << std::setprecision(4)
         << actual_qber  * 100 << "\n";
        std::cout << " First 10 Error Positions: ";
        std::size_t show = std::min(error_positions.size(), std::size_t(10));
        for (std::size_t i = 0; i < show; ++i)
            std::cout << error_positions[i] << " ";
        if (error_positions.size() > 10) std::cout << "...";
        std::cout << "\n";


    }
};

class NoisyChannel {
protected:
    std::mt19937 rng_;
    double      error_rate_;
    ChannelMetrics last_metrics_;

    int flip(int bit) {return bit ^1;}

public:
    explicit NoisyChannel(double error_rate, unsigned seed = std::random_device{}())
        : rng_(seed), error_rate_(error_rate) {
        if (error_rate < 0.0 || error_rate > 0.5) {
            throw std::invalid_argument("error_rate must be in range [0.0, 0.5]");
        }

    }

    virtual ~NoisyChannel() = default;

    virtual std::vector<int> transmit(const std::vector<int>& key) = 0;
    virtual std::string name() const = 0;

    double error_rate() const {return error_rate_;}
    const ChannelMetrics& metrics() const {return last_metrics_;}

};

// ----------------------------------------
// Binary Symmetric Channel
// ----------------------------------------

class BinarySymmetricChannel : public NoisyChannel {
    std::uniform_real_distribution<> dist_{0.0, 1.0};

public:
    explicit BinarySymmetricChannel(double error_rate, unsigned seed = std::random_device{}())
        : NoisyChannel(error_rate, seed) {}

    std::vector<int> transmit(const std::vector<int>& key) override {
        std::vector<int> recieved = key;
        ChannelMetrics m{};
        m.total_bits = key.size();
        m.expected_qber = error_rate_;

        for (std::size_t i = 0; i < key.size(); ++i) {
            if (dist_(rng_) < error_rate_) {
                recieved[i] = flip(recieved[i]);
                m.error_positions.push_back(i);
            }
        }

        m.error_count = m.error_positions.size();
        m.actual_qber = static_cast<double>(m.error_count) / static_cast<double>(m.total_bits);
        last_metrics_ = m;
        return recieved;
    }

    std::string name() const override {return "BinarySymmetricChannel";}
};

// ----------------------------------------
// Burst Error Channel
// ----------------------------------------

/**
 * Models correlated errors that appear in bursts - common in real hardware
 * when a fiber connection degrades or a detector saturates
 *
 * Uses a two-state Gilbert-Elliot model
 * - Good state: low error probability
 * - Bad state: high error probability
 * Transitions between states
 */
class BurstErrorChannel : public NoisyChannel {
    double p_good_;
    double p_bad_;
    double p_good_to_bad_;
    double p_bad_to_good_;

    std::uniform_real_distribution<> dist_{0.0, 1.0};

public:
    BurstErrorChannel(double avg_error_rate,
        double burst_intensity, unsigned seed = std::random_device{}())
            : NoisyChannel(avg_error_rate, seed) {
        p_good_ = avg_error_rate * (1.0 - burst_intensity);
        p_bad_ = std::min(0.5, avg_error_rate * (1.0 + burst_intensity * 9.0));
        p_good_to_bad_ = avg_error_rate * burst_intensity * 0.1;
        p_bad_to_good_ = 0.3; // recover relatively quickly
    }

    std::vector<int> transmit(const std::vector<int>& key) override {
        std::vector<int> received = key;
        ChannelMetrics m{};
        m.total_bits = key.size();
        m.expected_qber = error_rate_;

        bool in_bad_state = false; // allows us to track the states

        for (std::size_t i = 0; i < key.size(); i++) {
            if (in_bad_state) {
                if (dist_(rng_) < p_bad_to_good_) in_bad_state = false;
            }else {
                if (dist_(rng_)< p_good_to_bad_) in_bad_state = true;
            }

            double p_error = in_bad_state ? p_bad_ : p_good_;
            if (dist_(rng_) < p_error) {
                received[i] = flip(received[i]);
                m.error_positions.push_back(i);
            }
        }

        m.error_count = m.error_positions.size();
        m.actual_qber = static_cast<double>(m.error_count) / m.total_bits;
        last_metrics_ = m;
        return received;

    }
    std::string name() const override {return "BurstErrorChannel";}
};

// ----------------------------------------
// Depolarizing Channel
// ----------------------------------------

/**
 * Quantum-physically motivated noise model
 *
 * For a classical bit extracted after measurement:
 * - With prob (1 - p): bit is correct
 * - With prob (2p/3): bit flips (X or Y error)
 * - With prob (p/3): no observable effects from Z error
 *
 * This gives an effective classical bit-flip rate of 2p/3.
 */
class DepolarizingChannel : public NoisyChannel {
    double depolarizing_prob_;
    std::uniform_real_distribution<> dist_{0.0, 1.0};

public:
    explicit DepolarizingChannel(double depolarizing_prob,
        unsigned seed = std::random_device{}()) :
    NoisyChannel(2.0 * depolarizing_prob / 3.0, seed),
    depolarizing_prob_(depolarizing_prob) {
        if (depolarizing_prob < 0.0 || depolarizing_prob > 0.75)
            throw std::invalid_argument("Depolarizing prob must be in [0.0, 0.75]");
    }

    std::vector<int> transmit(const std::vector<int>& key) override {
        std::vector<int> received = key;
        ChannelMetrics m{};
        m.total_bits = key.size();
        m.expected_qber = error_rate_;

        for (std::size_t i = 0; i < key.size(); ++i) {
            double r = dist_(rng_);
            if (r < depolarizing_prob_) {
                // Apply X, , or Z with equal probability p/3 each
                double error_type = dist_(rng_);
                if (error_type < 2.0 / 3.0) {
                    // X or Y error -> observable bit flip
                    received[i] = flip(received[i]);
                    m.error_positions.push_back(i);
                }
                // Z error (prob 1/3): phase flip - no effect on classical bit
            }
        }
        m.error_count = m.error_positions.size();
        m.actual_qber = static_cast<double>(m.error_count) / m.total_bits;
        last_metrics_ = m;
        return received;
    }

    std::string name() const override {return "DepolarizingChannel";}
    double depolarizing_prob() const {return depolarizing_prob_;}
};

// ----------------------------------------
// Channel Factory
// ----------------------------------------

enum class ChannelType { BSC, Burst, Depolarizing };
std::unique_ptr<NoisyChannel> make_channel(ChannelType type,
    double error_rate, unsigned seed = std::random_device{}()) {
    switch (type) {
        case ChannelType::BSC:
            return std::make_unique<BinarySymmetricChannel>(error_rate, seed);
        case ChannelType::Burst:
            return std::make_unique<BurstErrorChannel>(error_rate, seed);
        case ChannelType::Depolarizing:
            return std::make_unique<DepolarizingChannel>(error_rate * 1.5, seed);
        default:
            throw std::invalid_argument("Unknown channel type");
    }
}

#endif //BB84_NOISY_CHANNEL_H
