//
// Created by Mike Zagar on 2026-06-09.
//

#ifndef BB84_CASCADE_H
#define BB84_CASCADE_H

#include <iostream>
#include <vector>
#include <queue>
#include <random>
#include <numeric>
#include <algorithm>
#include <functional>

#include "binary.h"
#include "noisy_channel.h"

struct CorrectionState {
    std::vector<int> bob_key;
    std::vector<std::vector<std::size_t>> permutations;
    std::vector<std::vector<Block>> pass_blocks;
    std::vector<std::size_t> corrected_positions;
    std::size_t num_passes;
    double estimated_ber;
    unsigned base_seed;
};

CorrectionState initialise_correction_state(const std::vector<int>& bob_key,
    const ChannelMetrics& metrics, std::size_t num_passes, unsigned base_seed) {
    CorrectionState state;
    state.bob_key = bob_key;
    state.num_passes = num_passes;
    state.base_seed = base_seed;
    state.estimated_ber = metrics.actual_qber;

    state.pass_blocks.resize(num_passes);
    state.permutations.resize(num_passes);

    state.corrected_positions.clear();

    return state;
}
// Flip the bit at a given position in Bob's key and record it
void apply_correction(CorrectionState& state,std::size_t position) {
    for (auto i : state.corrected_positions) {
        if (i == position) {
            return;
        }
    }
    state.bob_key[position] ^= 1;
    state.corrected_positions.push_back(position);
}

// After a correction, find all blocks from previous passes that contain that position which need
// re-checking
std::vector<Block> find_affected_blocks(const CorrectionState& state, std::size_t corrected_position) {
    std::vector<Block> affected_blocks;
    for (int i = 0; i < state.pass_blocks.size(); ++i) {
        for (int j = 0; j < state.pass_blocks[i].size(); ++j) {
            if (corrected_position >= state.pass_blocks[i][j].start &&
                corrected_position < state.pass_blocks[i][j].end) {
                affected_blocks.push_back(state.pass_blocks[i][j]);
            }
        }
    }
    return affected_blocks;
}

// Shuffle all block indices for a given pass
// Cascade randomises bit ordering to decorrelate errors
std::vector<std::size_t> generate_permutations(std::size_t key_length, std::size_t pass, unsigned base_seed) {

    // create ordered vector with [0, 1, 2, ..., key_length - 1] representing
    // original unshuffled ordering
    std::vector<std::size_t> indices(key_length);
    std::iota(indices.begin(), indices.end(), 0);

    // derive a unique seed using Knuth's multiplicative hash constant
    // chosen since this multiplies any integer by its visits every value in [0, 2^32)
    // before repeating
    constexpr unsigned KNUTH_CONSTANT = 2654435761u;

    // XOR base_seed with hashed pass number to ensure even pass = 0 contributes
    unsigned pass_seed = base_seed ^ (static_cast<unsigned>(pass) * KNUTH_CONSTANT);

    // Seeding with pass_seed ensures deterministic output
    // the same pass_seed will always produce same shuffle
    std::mt19937 rng(pass_seed);

    // shuffle indices around
    std::shuffle(indices.begin(), indices.end(), rng);

    return indices;
}

using ParityObserver = std::function<void(
    std::size_t pass,
    std::size_t block_id,
    int parity)>;

// Run a single pass of Cascade over a full key
// Identifies all blocks with odd parity and launches a binary search on them
void cascade_pass(const std::vector<int>& alice_key,
    CorrectionState& state, std::size_t pass, ParityObserver observer = nullptr) {

    std::vector<std::size_t> permutation = generate_permutations(alice_key.size(),
        pass, state.base_seed);
    state.permutations[pass] = permutation;
    std::size_t block_size = recommended_block_size(pass, state.estimated_ber);

    std::vector<Block> blocks = create_block(alice_key.size(), block_size, pass);
    state.pass_blocks[pass] = blocks;

    for (std::size_t block_idx = 0; block_idx < blocks.size(); ++block_idx) {
        const Block& block = blocks[block_idx];
        int alice_parity = compute_block_parity(alice_key, block, permutation);
        int bob_parity = compute_block_parity(state.bob_key, block, permutation);

        if (observer) {
             observer(pass, block_idx, alice_parity);
        }

        if (alice_parity == bob_parity) continue;
        std::size_t permuted_pos = binary_search_full(alice_key, state.bob_key ,block, permutation);
        std::size_t original_pos = permutation[permuted_pos];

        auto already_corrected = std::find(state.corrected_positions.begin(),
            state.corrected_positions.end(), original_pos);

        if (already_corrected != state.corrected_positions.end()) continue;
        apply_correction(state, original_pos);
        std::queue<Block> to_check;
        std::vector<Block> affected = find_affected_blocks(state, original_pos);

        for (const Block& b : affected) to_check.push(b);

        constexpr std::size_t MAX_BACKTRACK = 10000;
        std::size_t backtrack_count =0;

        while (!to_check.empty() && backtrack_count < MAX_BACKTRACK) {
            backtrack_count++;
            Block current = to_check.front();
            to_check.pop();

            const std::vector<std::size_t>& current_perm = state.permutations[current.pass];
            int a_parity = compute_block_parity(alice_key, current, current_perm);
            int b_parity = compute_block_parity(state.bob_key, current, current_perm);

            if (a_parity == b_parity) continue;

            std::size_t new_permuted_pos = binary_search_full(alice_key, state.bob_key , current, current_perm);
            std::size_t new_original_pos = current_perm[new_permuted_pos];

            auto already = std::find(state.corrected_positions.begin(),
                state.corrected_positions.end(), new_original_pos);

            if (already != state.corrected_positions.end()) continue;
            apply_correction(state, new_original_pos);
            std::vector<Block> newly_affected = find_affected_blocks(state, new_original_pos);

            for (const Block& b : newly_affected) to_check.push(b);
        }
        /**
        if (backtrack_count >= MAX_BACKTRACK) {
            std::cout << "Warning: backtracking limit reached\n" << std::endl;
        }
        */
    }
}

// Full cascade error correction
// Takes Alice's key, Bob's noisy key, and channel metrics
// Returns Bob's corrected key
std::vector<int> cascade_correct(std::vector<int>& alice_key,
    std::vector<int>& bob_key, const ChannelMetrics& metrics,
    std::size_t num_passes, unsigned base_seed,
    ParityObserver observer = nullptr) {

    // std::cout << "cascade_correct called:\n"
    //           << "  key length   : " << alice_key.size()    << "\n"
    //           << "  actual_ber   : " << metrics.actual_qber  << "\n"
    //           << "  error_count  : " << metrics.error_count << "\n"
    //           << "  num_passes   : " << num_passes          << "\n"
    //           << std::flush;

    if (alice_key.empty()) {
        throw std::invalid_argument("Alice key is empty");
    }

    if (alice_key.size() != bob_key.size()) {
        throw std::invalid_argument("Alice and Bob keys must be the same length");
    }

    // Only early return if no observer is watching
    // When Eve is observing we must run all passes regardless
    if (metrics.actual_qber <= 0.0 && observer == nullptr) {
        return bob_key;
    }

    if (metrics.actual_qber >= 0.5) {
        throw std::invalid_argument("BER must be below 0.5 for cascade to work");
    }

    CorrectionState state = initialise_correction_state(bob_key,
        metrics, num_passes, base_seed);

    for (std::size_t pass = 0; pass < num_passes; ++pass) {
        cascade_pass(alice_key, state, pass, observer);
    }

    return state.bob_key;
};

#endif //BB84_CASCADE_H
