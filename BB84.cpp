//
// Created by Mike Zagar on 2026-02-26.
//

#include <iomanip>
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <chrono>
#include <cstddef>
#include <fstream>
#include "noisy_channel.h"
#include "cascade.h"

// ------------------------------------
// Types and Constants
//-------------------------------------

enum class Basis {Rectilinear, Diagonal};

struct Qubit {
    int bit;        // 0 or 1
    Basis basis;    // encoding basis
};

// ------------------------------------
// Random Number generators
//-------------------------------------

class QuantumRNG {
    std::mt19937                        engine;                    // Mersenne Twister - pseudo-random number generator
    std::uniform_int_distribution<>     bit_dist{0, 1};       // uniform distribution over {0,1}, generating bits
    std::uniform_real_distribution<>    real_dist{0, 1};      // uniform distribution over {0.0, 1.0}, probability

public:
    explicit QuantumRNG(unsigned seed = std::random_device{}())
        :engine(seed){}

    int random_bit() {
        return bit_dist(engine);
    }

    Basis random_basis() {
        return random_bit()? Basis::Rectilinear : Basis::Diagonal;
        // maps bit generated from random_bit to a random basis: Rectilinear (true), Diagonal (false)
    }

    bool probability(double p) {
        return real_dist(engine) < p;
    }

};

// ------------------------------------
// Core Quantum Operators
//-------------------------------------

// Alice encodes a bit in a random basis
Qubit encode(int bit, Basis basis) {
    return {bit, basis};
}


/**
 * measuring a qubit in a chosen basis.
 *
 * If bases match -> correct bit is returned
 * If bases do not match -> quantum measurement projects onto a random
 * eigenstate.
 */
int measure(const Qubit& q, Basis measurement_basis, QuantumRNG& rng) {
    if (q.basis == measurement_basis) {
        return q.bit;
    }else {
        return rng.random_bit();
    }
}

/**
 * Eve intercepts-and-resends qubit
 *
 * She picks a random basis, measures, then re-encodes and forwards.
 * When her basis differs from Alice's, she resends the wrong state
 * introducing detectable error in the sifted key.
 */
Qubit eve_intercept(const Qubit& q, QuantumRNG& rng) {
    Basis eve_basis = rng.random_basis();
    int eve_bit = measure(q, eve_basis, rng);
    return encode(eve_bit, eve_basis);
}

Qubit eve_partial_intercept(const Qubit& q, QuantumRNG& rng, double intercept_probability) {

    if (rng.probability(intercept_probability)) {
        Basis eve_basis = rng.random_basis();
        int eve_bit = measure(q, eve_basis, rng);
        return encode(eve_bit, eve_basis);
    }
    return q;
}

// ------------------------------------
// BB84 Protocol
//-------------------------------------

struct BB84Result {
    std::vector<int>    alice_sifted_key;
    std::vector<int>    bob_sifted_key;
    std::vector<int>    bob_noisy_key;
    ChannelMetrics      channel_metrics;
    double              qber;               //Quantum Bit Error Rate
    bool                eve_detected;
    std::size_t              raw_key_length;
    std::size_t              sifted_key_length;
    std::size_t              check_bits_used;
};

BB84Result run_bb84(std::size_t       num_qubits,
                    bool              simulate_eve,
                    double            qber_threshold,
                    double            check_fraction,
                    NoisyChannel&     channel,
                    unsigned          seed = 0) {

    QuantumRNG rng(seed == 0? std::random_device{}() : seed);

    // -- Step 1 Alice prepares random bits & basis -----------------
    std::vector<int>        alice_bits (num_qubits);
    std::vector<Basis>      alice_basis (num_qubits);

    for (std::size_t i = 0; i < num_qubits; i++) {
        alice_bits[i] = rng.random_bit();
        alice_basis[i] = rng.random_basis();
    }

    // -- Step 2 Alice encodes and sends qubits ----------------------
    std::vector<Qubit>     quantum_channel(num_qubits);

    for (std::size_t i = 0; i < num_qubits; i++) {
        quantum_channel[i] = encode(alice_bits[i], alice_basis[i]);
    }

    // -- Step 3 Eve intercepts --------------------------------------

    //std::vector<int>        eve_bits;
    //std::vector<Basis>      eve_basis;
    if (simulate_eve) {
        for (std::size_t i = 0; i < num_qubits; i++) {
            //quantum_channel[i] = eve_intercept(quantum_channel[i], rng);
            Qubit q = eve_partial_intercept(quantum_channel[i], rng, 0.40);
            quantum_channel[i] = q;
            //eve_bits.push_back(q.bit);
            //eve_basis.push_back(q.basis);
        }
    }

    // -- Step 4 Bob chooses random bases and measures ----------------
    std::vector<Basis>       bob_basis (num_qubits);
    std::vector<int>         bob_bits (num_qubits);

    for (std::size_t i = 0; i < num_qubits; i++) {
        bob_basis[i] = rng.random_basis();
        bob_bits[i] = measure(quantum_channel[i], bob_basis[i], rng);
    }

    //-- Step 5 Public Basis reconciliation ---------------------------
    std::vector<int>        alice_sifted, bob_sifted;

    for (std::size_t i = 0; i < num_qubits; i++) {
        if (bob_basis[i] == alice_basis[i]) {
            alice_sifted.push_back(alice_bits[i]);
            bob_sifted.push_back(bob_bits[i]);
        }
    }

    std::vector<int> bob_noisy = channel.transmit(bob_sifted);
    ChannelMetrics     metrics = channel.metrics();

    //-- Step 6 Error Estimations -------------------------------------
    std::size_t sifted_len = alice_sifted.size();
    std::size_t check_count = std::max(std::size_t(1), static_cast<std::size_t>(sifted_len * check_fraction));
    check_count = std::min(check_count, sifted_len);

    // Shuffle indices to pick check bits randomly
    std::vector<std::size_t> indices(sifted_len);
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), std::mt19937{std::random_device{}()});

    std::size_t errors = 0;
    std::vector<bool> used_for_check(sifted_len, false);

    for (std::size_t k = 0; k < check_count; ++k) {
        std::size_t idx = indices[k];
        used_for_check[idx] = true;
        if (alice_sifted[idx] != bob_noisy[idx]) ++errors;
    }

    double qber = static_cast<double>(errors)/check_count;
    bool eve_detected = (qber > qber_threshold);

    //-- Step 7 Discard check bits -- remaining bits form the raw secret key
    std::vector<int> alice_key, bob_sifted_key, bob_noisy_key;
    for (std::size_t k = 0; k < sifted_len; ++k) {
        if (!used_for_check[k]) {
            alice_key.push_back(alice_sifted[k]);
            bob_sifted_key.push_back(bob_sifted[k]);
            bob_noisy_key.push_back(bob_noisy[k]);

        }
    }
    return BB84Result{
    alice_key,
    bob_sifted_key,
        bob_noisy_key,
        metrics,
        qber,
        eve_detected,
    num_qubits,
    sifted_len,
    check_count};
}

// ---------------------------------------------
// Utility: print helpers
// ---------------------------------------------

std::string basis_str(Basis b) {
    return b == Basis::Rectilinear ? "+" : "x";
}

void print_key(const std::string& label, const std::vector<int>& key, std::size_t max_show = 40) {
    std::cout << label << " [" << key.size() << " bits ]: ";
    for (std::size_t i = 0; i < std::min(key.size(), max_show); ++i) {
        std::cout << key[i];
    }
    if (key.size() > max_show) std::cout << "...";
    std::cout << "\n";
}

bool keys_match(const std::vector<int>& a, const std::vector<int>& b) {
    return a == b;
}
using namespace std::chrono;

int main() {

    constexpr std::size_t NUM_QUBITS    = 1000000;
    constexpr double QBER_THRESHOLD     = 0.11;
    constexpr double CHECK_FRACTION     = 0.10;
    constexpr double CHANNEL_BER        = 0.11;
    constexpr double CHANNEL_ERROR_RATE = 0.05;
    constexpr std::size_t NUM_RUNS = 50;

    std::ofstream file("/Users/mikezagar/CLionProjects/QKD/bb84_intercept_results.txt");

    file << "Round #, QBER, Secure Bits, Eve Detected, Errors Before, Errors After, Time\n";

         for (std::size_t run = 0; run < NUM_RUNS; ++run) {
             //auto start = high_resolution_clock::now();
             double target_ber = 0.01 + (run * (QBER_THRESHOLD - 0.01) / static_cast<double>(NUM_RUNS - 1));
             //
             // BinarySymmetricChannel channel(target_ber);

             BinarySymmetricChannel channel(target_ber);

             auto result = run_bb84(NUM_QUBITS,
                                 true,
                                 QBER_THRESHOLD,
                                 CHECK_FRACTION,
                                 channel);

             std::size_t errors_after = 0;
             //if (!result.eve_detected) {
             auto corrected = cascade_correct(result.alice_sifted_key,
                                              result.bob_noisy_key,
                                              result.channel_metrics,
                                              5,
                                              42);

             for (std::size_t i = 0; i < result.alice_sifted_key.size(); ++i) {
                 if (result.alice_sifted_key[i] != corrected[i]) {
                     ++errors_after;
                 }
             }
                     // auto stop = high_resolution_clock::now();
                     // auto duration = duration_cast<microseconds>(stop - start);

             file << run << "," << result.qber << ","
                     << result.alice_sifted_key.size() << ","
                     << result.eve_detected << ","
                     << result.channel_metrics.error_count << ","
                     << errors_after << "\n";


         }

     file.close();

    // In your BB84 main loop — add a repetition loop
    // constexpr std::size_t NUM_REPS = 20;  // repetitions per parameter value
    //
    // std::cout << "Run,Rep,Theta,QBER,SecureBits,ErrorsAfter\n";
    //
    // for (std::size_t run = 0; run < NUM_RUNS; ++run) {
    //     double intercept = run * (0.40 / NUM_RUNS);
    //
    //     for (std::size_t rep = 0; rep < NUM_REPS; ++rep) {
    //         // Different seed each repetition
    //         unsigned seed = run * 1000 + rep;
    //
    //         BinarySymmetricChannel channel (CHANNEL_BER);
    //
    //         auto result = run_bb84(NUM_QUBITS,
    //                            intercept,
    //                            QBER_THRESHOLD,
    //                            CHECK_FRACTION,
    //                            channel,
    //                            seed);
    //
    //         auto corrected = cascade_correct(result.alice_sifted_key,
    //                                  result.bob_noisy_key,
    //                                  result.channel_metrics,
    //                                  5,
    //                                  42);
    //
    //         std::size_t errors_after = 0;
    //         for (std::size_t i = 0; i < result.alice_sifted_key.size(); ++i) {
    //             if (result.alice_sifted_key[i] != corrected[i]) {
    //                 ++errors_after;
    //             }
    //         }
    //
    //         std::cout << run        << ","
    //                 << rep          << ","
    //                 << intercept    << ","
    //                 << result.qber  << ","
    //                 << result.alice_sifted_key.size() << ","
    //                 << errors_after << "\n";
    //     }
    // }

    //file.close();

    // std::cout << "Time taken to simulate " << std::to_string(NUM_QUBITS) << " : "
    //               << duration.count() << " microseconds \n";

    /**
    BinarySymmetricChannel channel(CHANNEL_ERROR_RATE);

    auto result = run_bb84(NUM_QUBITS,
                            true,
                            QBER_THRESHOLD,
                            CHECK_FRACTION,
                            channel);

    if (result.eve_detected) {
        std::cout << "Eve detected - aborting.\n";
        return 1;
    }

    std::size_t errors_before = 0;
    for (std::size_t i = 0; i < result.alice_sifted_key.size(); ++i) {
        if (result.alice_sifted_key[i] != result.bob_noisy_key[i])
            ++errors_before;
    }

    std::cout << "Sifted key length     : "
              << result.alice_sifted_key.size() << "\n";
    std::cout << "Channel BER           : "
              << result.channel_metrics.actual_qber * 100 << "%\n";
    std::cout << "Errors before Cascade: " << errors_before << "\n";

    std::vector<int> corrected = cascade_correct(
        result.alice_sifted_key,
        result.bob_noisy_key,
        result.channel_metrics,
        4, 42);

    std::size_t errors_after = 0;
    for (std::size_t i = 0; i < result.alice_sifted_key.size(); ++i) {
        if (result.alice_sifted_key[i] != corrected[i])
            ++errors_after;
    }

    std::cout << "Errors after Cascade: " << errors_after << "\n";
    std::cout << "keys match          : "
              << (errors_after == 0 ? "YES" : "NO") << "\n";

    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(stop - start);

    std::cout << "Time taken to simulate " << std::to_string(NUM_QUBITS) << " : "
              << duration.count() << " microseconds \n";
    */

    return 0;
}
