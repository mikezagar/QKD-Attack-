//
// Created by Mike Zagar on 2026-07-15.
//

#ifndef BB84_EVE_ATTACK_H
#define BB84_EVE_ATTACK_H

#pragma once

#include <vector>
#include <cstddef>
#include <unordered_map>
#include <cmath>
#include <functional>
#include <unordered_set>
#include <numeric>
#include <set>
#include <iostream>
#include <chrono>
#include <random>
#include <string>
#include <algorithm>
#include "cascade.h"
#include "noisy_channel.h"

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

struct EveQubitRecord {
    std::size_t     position;
    int             measured_bit;
    Basis           eve_basis;
    Basis           alice_basis;
    bool            basis_match;
    bool            known;
};

struct EveKeyCandidate {
    std::vector<int>     bits;
    double               probability;
};

struct EveParityObservation {
    std::size_t             pass;
    std::size_t             block_id;
    Block                   block;
    int                     parity;
};

struct EveState {
    std::vector<EveQubitRecord>           qubit_records;
    std::unordered_map<std::size_t, int>  known_bits;
    std::vector<std::size_t>              unknown_positions;

    double                                log2_candidates;
    std::size_t                           initial_candidate_count_log2;

    std::vector<EveKeyCandidate>          candidate_keys;
    std::vector<EveParityObservation>     observations;
    std::vector<std::vector<Block>>       eve_pass_blocks;
    std::vector<std::vector<std::size_t>> eve_permutations;

    double                                information_leakage;

    std::size_t                           key_length;
    double                                estimated_ber;
    std::size_t                           num_passes;
    unsigned                              base_seed;

    std::size_t                           initial_candidate_count;
    std::size_t                           bits_known_active;
    std::size_t                           bits_known_passive;
};


struct AttackResult {
    std::size_t                 key_length;
    std::vector<int>            alice_key;
    std::vector<int>            corrected;
    double                      log2_initial_candidates;
    double                      log2_final_candidates;
    double                      information_gained;
    bool                        key_compromised;
    std::vector<int>            eve_key_guess;
    std::size_t                 bits_known_active;
    std::size_t                 bits_known_passive;
    double                      qber_introduced;
    std::size_t                 observations_made;
    std::size_t                 informative_observations;
};

// ---------------------------------------------
// Active Phase Functions
// ---------------------------------------------

EveState initialise_eve(std::size_t key_length,
                        double      estimated_ber,
                        std::size_t num_passes,
                        unsigned    base_seed);

Qubit eve_partial_intercept(const Qubit& alice_qubit,
                            std::size_t  position,
                            double       intercept_prob,
                            EveState&    eve_state,
                            QuantumRNG&  rng);

void eve_process_basis_reconciliation(
    EveState&                       eve_state,
    const std::vector<Basis>&       alice_bases,
    const std::vector<Basis>&       bob_bases,
    const std::vector<std::size_t>& sifted_positions);

void eve_build_candidate_keys(EveState& eve_state);

// ---------------------------------------------
// Passive Phase Functions
// ---------------------------------------------

void eve_reconstruct_blocks(EveState&   eve_state,
                            std::size_t pass);

void eve_observe_parity(EveState&       eve_state,
                        std::size_t     pass,
                        std::size_t     block_id,
                        const Block&    block,
                        int             parity);

void eve_filter_candidates(EveState& eve_state,
                          const Block& block,
                          std::size_t     pass,
                          int observed_parity);

void eve_observe_cascade_pass(EveState&                eve_state,
                             std::size_t               pass,
                             const std::vector<Block>& public_blocks,
                             const std::vector<int>&   public_parities);

std::size_t eve_count_candidates(const EveState& eve_state);
double eve_information_gained(const EveState& eve_state);
bool eve_has_full_key(const EveState& eve_state);
std::vector<int> eve_best_key_guess(const EveState& eve_state);

// ---------------------------------------------
// Attack Result
// ---------------------------------------------

AttackResult run_manipulate_observe_attack(
    std::size_t     num_qubits,
    double          intercept_prob,
    double          qber_threshold,
    double          check_fraction,
    double          channel_ber,
    std::size_t     num_passes = 5,
    unsigned        base_seed = 42,
    unsigned        rng_seed  = 0);

#endif //BB84_EVE_ATTACK_H
