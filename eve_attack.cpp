//
// Created by Mike Zagar on 16/07/2026.
//

#include "eve_attack.h"


// ---------------------------------------------
// Active Phase Functions
// ---------------------------------------------

EveState initialise_eve(std::size_t key_length,
                        double      estimated_ber,
                        std::size_t num_passes,
                        unsigned    base_seed) {
    EveState state;
    state.key_length = key_length;
    state.estimated_ber = estimated_ber;
    state.num_passes = num_passes;
    state.base_seed = base_seed;
    state.information_leakage = 0.0;
    state.bits_known_active = 0;
    state.bits_known_passive = 0;
    state.initial_candidate_count = 0;

    state.qubit_records.clear();
    state.known_bits.clear();
    state.unknown_positions.clear();
    state.candidate_keys.clear();
    state.observations.clear();

    state.eve_pass_blocks.resize(num_passes);
    state.eve_permutations.resize(num_passes);

    // std::cout << "  eve_permutations size after resize: "
    //           << state.eve_permutations.size() << "\n" << std::flush;

    return state;
}

Qubit eve_partial_intercept(const Qubit& alice_qubit,
                            std::size_t  position,
                            double       intercept_prob,
                            EveState&    eve_state,
                            QuantumRNG&  rng) {

    EveQubitRecord record;
    record.position = position;
    record.basis_match = false;
    record.measured_bit = -1;

    if (rng.probability(intercept_prob)) {
        record.eve_basis = rng.random_basis();
        record.measured_bit = measure(alice_qubit, record.eve_basis, rng);

        record.known = false;

        eve_state.qubit_records.push_back(record);
        return encode(record.measured_bit, record.eve_basis);

    }else {
        record.eve_basis = Basis::Rectilinear;
        record.measured_bit = -1;
        record.known = false;

        eve_state.qubit_records.push_back(record);
        return alice_qubit;
    }

}

void eve_process_basis_reconciliation(
    EveState&                       eve_state,
    const std::vector<Basis>&       alice_bases,
    const std::vector<Basis>&       bob_bases,
    const std::vector<std::size_t>& sifted_positions) {

    std::unordered_map<std::size_t, std::size_t> raw_to_sifted;
    for (std::size_t s = 0; s < sifted_positions.size(); ++s) {
        raw_to_sifted[sifted_positions[s]] = s;
    }

    for (auto& record: eve_state.qubit_records) {
        std::size_t raw_pos = record.position;

        // Record Alice's public basis announcement
        record.alice_basis = alice_bases[raw_pos];
        // if (raw_pos < alice_bases.size()) {
        //     record.alice_basis = alice_bases[raw_pos];
        // }
        // Check if this qubit was sifted
        bool was_sifted = (alice_bases[raw_pos] == bob_bases[raw_pos]);
        if (!was_sifted) {
            record.known = false;
            continue;
        }

        // Was this intercepted by Eve
        std::size_t sifted_pos = raw_to_sifted[raw_pos];
        if (record.measured_bit == -1) {
            record.basis_match = false;
            record.known = false;
            eve_state.unknown_positions.push_back(sifted_pos);
            continue;
        }

        // Did Eve basis match Alice's
        if (record.eve_basis == record.alice_basis) {
            record.basis_match = true;
            record.known = true;
            eve_state.known_bits[sifted_pos] = record.measured_bit;

        }else {
            record.basis_match = false;
            record.known = false;
            eve_state.unknown_positions.push_back(sifted_pos);
        }
    }
    std::sort(eve_state.unknown_positions.begin(), eve_state.unknown_positions.end());
    eve_state.unknown_positions.erase(std::unique(eve_state.unknown_positions.begin(),
                                eve_state.unknown_positions.end()), eve_state.unknown_positions.end());

    eve_state.bits_known_active = eve_state.known_bits.size();
}

void eve_build_candidate_keys(EveState& eve_state) {
    std::size_t num_unknown = eve_state.unknown_positions.size();

    eve_state.log2_candidates              = static_cast<double>(num_unknown);
    eve_state.initial_candidate_count_log2 = num_unknown;
    eve_state.information_leakage          = 0.0;

    // std::cout << "Candidate set initialised:\n"
    //           << "  Unknown positions  : " << num_unknown         << "\n"
    //           << "  log2(candidates)   : " << eve_state.log2_candidates << "\n"
    //           << "  Equivalent to 2^"      << num_unknown
    //           << " candidates\n" << std::flush;

    // std::cout << "Unknown positions  : " << num_unknown << "\n";
    // std::cout << "Candidate count    : " << (std::size_t(1) << num_unknown) << "\n";
    // std::cout << "Memory estimate    : "
    //           << ((std::size_t(1) << num_unknown) *
    //                eve_state.key_length * 4) / (1024*1024)
    //           << " MB\n";
    //
    // std::size_t key_len     = eve_state.key_length;
    //
    // constexpr std::size_t MAX_UNKNOWN = 24;
    // if (num_unknown > MAX_UNKNOWN) {
    //     std::cerr << "Warning: " << num_unknown
    //               << " unknown positions exceeds practical limit of "
    //               << MAX_UNKNOWN << ". Capping at first "
    //               << MAX_UNKNOWN << " unknowns. \n";
    //     eve_state.unknown_positions.resize(MAX_UNKNOWN);
    //     num_unknown = MAX_UNKNOWN;
    // }
    //
    // std::size_t num_candidates = std::size_t(1) << num_unknown;
    // eve_state.candidate_keys.clear();
    // eve_state.candidate_keys.reserve(num_candidates);
    // eve_state.initial_candidate_count = num_candidates;
    //
    // for (std::size_t mask = 0; mask < num_candidates; ++mask) {
    //     EveKeyCandidate candidate;
    //     candidate.bits.resize(key_len, 0);
    //     candidate.probability = 1.0;
    //
    //     for (const auto& [pos, bit] : eve_state.known_bits) {
    //         if (pos < key_len) {
    //             candidate.bits[pos] = bit;
    //         }
    //     }
    //
    //
    //     for (std::size_t j = 0; j < num_unknown; ++j) {
    //         std::size_t pos = eve_state.unknown_positions[j];
    //         int bit = (mask >> j) & 1;
    //         if (pos < key_len) {
    //             candidate.bits[pos] = bit;
    //         }
    //     }
    // eve_state.candidate_keys.push_back(std::move(candidate));
    // }
}

// In eve_attack.h — add these structures

// Pre-computed lookup table for a block of given size
// Maps parity value to list of valid bit patterns
struct BlockLookupTable {
    std::size_t              block_size;
    std::vector<std::vector<std::vector<int>>> parity_to_patterns;
    // parity_to_patterns[parity] = list of bit patterns with that parity
    // parity is 0 or 1, so this has 2 entries

    void build(std::size_t k) {
        block_size = k;
        parity_to_patterns.resize(2);

        std::size_t num_patterns = std::size_t(1) << k;
        for (std::size_t mask = 0; mask < num_patterns; ++mask) {
            // Convert mask to bit pattern
            std::vector<int> pattern(k);
            int parity = 0;
            for (std::size_t bit = 0; bit < k; ++bit) {
                pattern[bit] = (mask >> bit) & 1;
                parity ^= pattern[bit];
            }
            parity_to_patterns[parity].push_back(pattern);
        }
    }
};

// Valid candidates for a single block position
struct BlockCandidates {
    std::size_t              block_index;
    std::size_t              start;
    std::size_t              end;
    std::vector<std::vector<int>> valid_patterns; // patterns consistent with all observations
};

// eve_attack.cpp — add these functions

// ── Step (d): Build lookup table for block size k1 ───────────────────────────
BlockLookupTable eve_build_lookup_table(std::size_t block_size)
{
    // Safety cap — 2^20 = 1 million patterns maximum
    if (block_size > 20) {
        throw std::invalid_argument(
            "Block size too large for lookup table: " +
            std::to_string(block_size));
    }

    BlockLookupTable table;
    table.build(block_size);

    std::cout << "Lookup table built:\n"
              << "  Block size    : " << block_size          << "\n"
              << "  Total patterns: " << (std::size_t(1) << block_size) << "\n"
              << "  Parity 0 count: "
              << table.parity_to_patterns[0].size()          << "\n"
              << "  Parity 1 count: "
              << table.parity_to_patterns[1].size()          << "\n"
              << std::flush;

    return table;
}

// ── Step (e): Vectorised block search ────────────────────────────────────────
// For each block in pass 1, find all valid bit patterns
// consistent with all parity observations for that block
BlockCandidates eve_search_block(
    std::size_t                           block_index,
    const Block&                          block,
    const BlockLookupTable&               lookup,
    const std::vector<EveParityObservation>& observations,
    const std::vector<std::size_t>&       permutation,
    const std::unordered_map<std::size_t, int>& known_bits)
{
    BlockCandidates result;
    result.block_index = block_index;
    result.start       = block.start;
    result.end         = block.end;

    std::size_t block_size = block.end - block.start;

    // Start with all patterns consistent with known bits
    // For positions Eve knows, filter immediately
    std::vector<std::vector<int>> candidates;

    // Find parity observations relevant to this block
    std::vector<int> relevant_parities;
    for (const auto& obs : observations) {
        if (obs.pass == block.pass &&
            obs.block_id == block_index) {
            relevant_parities.push_back(obs.parity);
        }
    }

    if (relevant_parities.empty()) {
        // No observations for this block — all patterns valid
        // Use patterns from lookup table
        for (int p : {0, 1}) {
            for (const auto& pattern : lookup.parity_to_patterns[p]) {
                candidates.push_back(pattern);
            }
        }
    } else {
        // Start with patterns matching first observation
        int first_parity = relevant_parities[0];
        candidates = lookup.parity_to_patterns[first_parity];
    }

    // Filter by known bit values at specific positions
    for (std::size_t local_pos = 0; local_pos < block_size; ++local_pos) {
        std::size_t global_pos = permutation[block.start + local_pos];

        auto it = known_bits.find(global_pos);
        if (it != known_bits.end()) {
            int known_val = it->second;

            // Remove patterns where this position has wrong value
            candidates.erase(
                std::remove_if(
                    candidates.begin(), candidates.end(),
                    [&](const std::vector<int>& pattern) {
                        return pattern[local_pos] != known_val;
                    }
                ),
                candidates.end()
            );
        }
    }

    // Apply binary search parity observations (sub-block parities)
    // These come from the BINARY subprotocol during Cascade
    for (const auto& obs : observations) {
        if (obs.block_id != block_index) continue;
        if (obs.block.start < block.start ||
            obs.block.end   > block.end) continue;

        // This observation covers a sub-range of our block
        std::size_t local_start = obs.block.start - block.start;
        std::size_t local_end   = obs.block.end   - block.start;

        candidates.erase(
            std::remove_if(
                candidates.begin(), candidates.end(),
                [&](const std::vector<int>& pattern) {
                    int parity = 0;
                    for (std::size_t i = local_start; i < local_end; ++i) {
                        parity ^= pattern[i];
                    }
                    return parity != obs.parity;
                }
            ),
            candidates.end()
        );
    }

    result.valid_patterns = std::move(candidates);
    return result;
}

// ── Step (f): Combine block candidates into full key candidates ───────────────
// This is the expensive step — combines valid patterns from each block
// into complete key candidates
std::vector<std::vector<int>> eve_combine_blocks(
    const std::vector<BlockCandidates>&   block_results,
    const std::vector<std::size_t>&       permutation,
    std::size_t                           key_length,
    std::size_t                           max_candidates = 1000000)
{
    std::vector<std::vector<int>> keys = {std::vector<int>(key_length, 0)};

    for (const auto& block : block_results) {
        if (block.valid_patterns.empty()) {
            std::cout << "  Block " << block.block_index
                      << " has no valid patterns — contradiction\n";
            continue;
        }

        if (block.valid_patterns.size() == 1) {
            // Only one valid pattern — fill it in directly
            const auto& pattern = block.valid_patterns[0];
            std::size_t block_size = block.end - block.start;
            for (auto& key : keys) {
                for (std::size_t local = 0; local < block_size; ++local) {
                    std::size_t global = permutation[block.start + local];
                    if (global < key_length) {
                        key[global] = pattern[local];
                    }
                }
            }
            continue;
        }

        // Multiple valid patterns — expand key set
        std::vector<std::vector<int>> expanded;
        expanded.reserve(
            std::min(keys.size() * block.valid_patterns.size(),
                     max_candidates));

        for (const auto& key : keys) {
            for (const auto& pattern : block.valid_patterns) {
                if (expanded.size() >= max_candidates) break;

                std::vector<int> new_key = key;
                std::size_t block_size   = block.end - block.start;

                for (std::size_t local = 0; local < block_size; ++local) {
                    std::size_t global = permutation[block.start + local];
                    if (global < key_length) {
                        new_key[global] = pattern[local];
                    }
                }
                expanded.push_back(std::move(new_key));
            }
            if (expanded.size() >= max_candidates) break;
        }

        keys = std::move(expanded);

        std::cout << "  After block " << block.block_index
                  << ": " << keys.size() << " candidate keys\n"
                  << std::flush;

        if (keys.size() == 1) {
            std::cout << "  Single candidate found!\n";
            break;
        }
    }

    return keys;
}

// ── Steps (g)(h)(i): Chunked brute force filter ───────────────────────────────
// Split candidates into chunks and filter each chunk in parallel
// against ALL parity observations across all passes
std::vector<std::vector<int>> eve_brute_force_filter(
    std::vector<std::vector<int>>         candidates,
    const std::vector<EveParityObservation>& all_observations,
    const std::vector<std::vector<std::size_t>>& permutations,
    std::size_t                           chunk_size = 10000)
{
    std::vector<std::vector<int>> valid;
    valid.reserve(candidates.size());

    std::size_t total     = candidates.size();
    std::size_t processed = 0;

    // Process in chunks
    for (std::size_t chunk_start = 0;
         chunk_start < total;
         chunk_start += chunk_size)
    {
        std::size_t chunk_end = std::min(chunk_start + chunk_size, total);

        for (std::size_t i = chunk_start; i < chunk_end; ++i) {
            const auto& candidate = candidates[i];
            bool consistent = true;

            // Check ALL parity observations
            for (const auto& obs : all_observations) {
                if (obs.pass >= permutations.size()) continue;
                const auto& perm = permutations[obs.pass];

                int parity = 0;
                for (std::size_t pos = obs.block.start;
                     pos < obs.block.end && pos < perm.size(); ++pos) {
                    std::size_t original = perm[pos];
                    if (original < candidate.size()) {
                        parity ^= candidate[original];
                    }
                }

                if (parity != obs.parity) {
                    consistent = false;
                    break;
                }
            }

            if (consistent) {
                valid.push_back(candidate);
            }
        }

        processed = chunk_end;
        if (processed % 100000 == 0) {
            std::cout << "  Brute force progress: "
                      << processed << "/" << total
                      << " | Valid so far: " << valid.size()
                      << "\n" << std::flush;
        }

        if (valid.size() == 1) {
            std::cout << "  Single key found — stopping early\n";
            break;
        }
    }

    return valid;
}

// ── Top level: Full block-based passive phase ─────────────────────────────────
std::vector<std::vector<int>> eve_passive_phase_brute_force(
    EveState&    eve_state,
    std::size_t  pass = 0)   // start with pass 0 (smallest blocks)
{
    if (pass >= eve_state.eve_pass_blocks.size()) {
        std::cerr << "Pass " << pass << " not available\n";
        return {};
    }

    const auto& blocks      = eve_state.eve_pass_blocks[pass];
    const auto& permutation = eve_state.eve_permutations[pass];
    std::size_t block_size  = blocks.empty() ? 0
                              : (blocks[0].end - blocks[0].start);

    std::cout << "\nPassive phase brute force:\n"
              << "  Pass        : " << pass       << "\n"
              << "  Blocks      : " << blocks.size() << "\n"
              << "  Block size  : " << block_size    << "\n"
              << std::flush;

    // Step (d): Build lookup table
    if (block_size > 20) {
        std::cout << "  Block size " << block_size
                  << " too large for brute force lookup\n"
                  << "  Falling back to logarithmic tracking\n";
        return {};
    }

    BlockLookupTable lookup = eve_build_lookup_table(block_size);

    // Step (e): Search each block independently
    std::vector<BlockCandidates> block_results;
    block_results.reserve(blocks.size());

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        BlockCandidates bc = eve_search_block(
            i,
            blocks[i],
            lookup,
            eve_state.observations,
            permutation,
            eve_state.known_bits
        );

        std::cout << "  Block " << i << ": "
                  << bc.valid_patterns.size()
                  << " valid patterns\n" << std::flush;

        block_results.push_back(std::move(bc));
    }

    // Step (f): Combine blocks into full key candidates
    std::cout << "\nCombining block candidates...\n" << std::flush;
    auto candidates = eve_combine_blocks(
        block_results,
        permutation,
        eve_state.key_length
    );

    std::cout << "After combining: " << candidates.size()
              << " candidates\n" << std::flush;

    if (candidates.size() <= 1) return candidates;

    // Steps (g)(h)(i): Brute force filter with all observations
    std::cout << "Running brute force filter...\n" << std::flush;
    auto filtered = eve_brute_force_filter(
        std::move(candidates),
        eve_state.observations,
        eve_state.eve_permutations
    );

    std::cout << "After brute force: " << filtered.size()
              << " candidates remaining\n" << std::flush;

    return filtered;
}

// ---------------------------------------------
// Passive Phase Functions
// ---------------------------------------------

void eve_reconstruct_blocks(EveState&   eve_state,
                            std::size_t pass) {

    if (pass >= eve_state.eve_permutations.size()) {
        std::cerr << "ERROR: pass " << pass
                  << " >= eve_permutations.size() "
                  << eve_state.eve_permutations.size() << "\n";
        return;
    }
    if (eve_state.key_length == 0) {
        std::cerr << "ERROR: eve_state.key_length is 0 "
                  << "— update it before calling eve_reconstruct_blocks\n";
        return;
    }

    // Guard 3 — estimated_ber must be valid
    if (eve_state.estimated_ber <= 0.0 ||
        eve_state.estimated_ber >= 0.5) {
        std::cerr << "ERROR: invalid estimated_ber: "
                  << eve_state.estimated_ber << "\n";
        return;
        }

    // std::cout << "  Reconstructing pass " << pass
    //           << " key_length=" << eve_state.key_length
    //           << " ber="        << eve_state.estimated_ber << "\n"
    //           << std::flush;

    std::vector<std::size_t> permutation = generate_permutations(
        eve_state.key_length,
        pass,
        eve_state.base_seed);

    eve_state.eve_permutations[pass] = permutation;
    std::size_t block_size = recommended_block_size(pass,
                                                    eve_state.estimated_ber);
    std::vector<Block> blocks = create_block(eve_state.key_length,
                                              block_size,
                                              pass);
    eve_state.eve_pass_blocks[pass] = blocks;
}

void eve_observe_parity(EveState&       eve_state,
                        std::size_t     pass,
                        std::size_t     block_id,
                        const Block&    block,
                        int             parity) {
    EveParityObservation obs;
    obs.pass        = pass;
    obs.block_id    = block_id;
    obs.block       = block;
    obs.parity      = parity;

    eve_state.observations.push_back(obs);
    eve_filter_candidates(eve_state, block, pass, parity);
}

void eve_filter_candidates(EveState& eve_state,
                          const Block& block,
                          std::size_t     pass,
                          int observed_parity) {

    if (pass >= eve_state.eve_permutations.size()) return;
    if (eve_state.eve_permutations[pass].empty())  return;
    if (eve_state.log2_candidates <= 0.0)          return;

    const auto& permutation = eve_state.eve_permutations[pass];

    // ── Check if this block covers any unknown positions ──────────────────────
    // An observation is only informative if it covers at least one
    // position Eve does not already know
    // Build a set of unknown positions for fast lookup
    std::unordered_set<std::size_t> unknown_set(
        eve_state.unknown_positions.begin(),
        eve_state.unknown_positions.end());

    bool covers_unknown = false;
    for (std::size_t i = block.start;
         i < block.end && i < permutation.size(); ++i)
    {
        std::size_t original_pos = permutation[i];
        if (unknown_set.count(original_pos) > 0) {
            covers_unknown = true;
            break;
        }
    }

    if (!covers_unknown) return;  // observation tells Eve nothing new

    // ── This observation halves the candidate set ─────────────────────────────
    // Each parity observation covering unknown bits eliminates exactly
    // half the candidates — those whose parity disagrees with observation
    eve_state.log2_candidates = std::max(0.0,
                                          eve_state.log2_candidates - 1.0);
    eve_state.information_leakage += 1.0;

    // Mark any positions in this block as partially resolved
    // Once log2_candidates reaches 0 the key is fully determined
    // if (eve_state.candidate_keys.empty()) return;
    // if (pass >= eve_state.eve_permutations.size()) return;
    // if (eve_state.eve_permutations[pass].empty()) {
    //     eve_reconstruct_blocks(eve_state, pass);
    // }
    //
    // const auto& permutation = eve_state.eve_permutations[pass];
    //
    // eve_state.candidate_keys.erase(
    //     std::remove_if(
    //         eve_state.candidate_keys.begin(),
    //         eve_state.candidate_keys.end(),
    //         [&](const EveKeyCandidate& candidate) {
    //             int candidate_parity = 0;
    //             for (std::size_t i = block.start; i < block.end; ++i) {
    //                 if (i < permutation.size()) {
    //                     std::size_t original_pos = permutation[i];
    //                     if (original_pos < candidate.bits.size()) {
    //                         candidate_parity ^= candidate.bits[original_pos];
    //                     }
    //                 }
    //             }
    //             return candidate_parity != observed_parity;
    //         }
    //         ),
    //         eve_state.candidate_keys.end());
    //
    // if (eve_state.initial_candidate_count > 0 && !eve_state.candidate_keys.empty()) {
    //     eve_state.information_leakage = std::log2(
    //         static_cast<double>(eve_state.initial_candidate_count) /
    //         static_cast<double>(eve_state.candidate_keys.size()));
    // }
}

void eve_observe_cascade_pass(EveState&                eve_state,
                             std::size_t               pass,
                             const std::vector<Block>& public_blocks,
                             const std::vector<int>&   public_parities) {

    eve_reconstruct_blocks(eve_state, pass);

    for (std::size_t i = 0;
         i < public_blocks.size() && i < public_parities.size(); ++i)
    {
        eve_observe_parity(eve_state,
                            pass,
                            i,
                            public_blocks[i],
                            public_parities[i]);

        // FIXED — use log2_candidates not candidate_keys.size()
        if (i % 10 == 0) {
            // std::cout << "  Pass " << pass
            //           << " block " << i << "/"
            //           << public_blocks.size()
            //           << " | log2(candidates) remaining: "
            //           << eve_state.log2_candidates << "\n";
        }

        // FIXED — check log2 not size
        if (eve_state.log2_candidates <= 0.0) {
            // std::cout << "  Key fully determined after "
            //           << eve_state.observations.size()
            //           << " observations!\n";
            break;
        }
    }

    // FIXED — compute passive bits from log2 tracking
    double initial = static_cast<double>(
        eve_state.initial_candidate_count_log2);
    eve_state.bits_known_passive = static_cast<std::size_t>(
        initial - eve_state.log2_candidates);

    // eve_reconstruct_blocks(eve_state, pass);
    //
    // for (std::size_t i = 0; i < public_blocks.size() && i < public_parities.size(); ++i) {
    //     eve_observe_parity(eve_state,
    //                        pass,
    //                        i,
    //                        public_blocks[i],
    //                        public_parities[i]);
    //     if (i % 10 == 0) {
    //         std::cout << " Pass " << pass
    //                   << " block " << i << "/" << public_blocks.size()
    //                   << " | Candidates remaining: "
    //                   << eve_state.candidate_keys.size() << "\n";
    //     }
    //
    //     if (eve_state.candidate_keys.size() == 1) {
    //         std::cout << " Key fully determined after "
    //                   << eve_state.observations.size()
    //                   << " observations!\n";
    //         break;
    //     }
    // }
    //
    // if (eve_state.initial_candidate_count > 0) {
    //     double remaining = static_cast<double>(
    //         eve_state.candidate_keys.size());
    //     double initial = static_cast<double>(
    //         eve_state.initial_candidate_count);
    //     eve_state.bits_known_passive = static_cast<std::size_t>(
    //         std::log2(initial / std::max(remaining, 1.0)));
    // }
}

double eve_log2_candidates(const EveState& eve_state)
{
    return eve_state.log2_candidates;
}

// std::size_t eve_count_candidates(const EveState& eve_state) {
//     return eve_state.candidate_keys.size();
// }

double eve_information_gained(const EveState& eve_state)
{
    return eve_state.information_leakage
         + static_cast<double>(eve_state.bits_known_active);
}

// double eve_information_gained(const EveState& eve_state) {
//     if (eve_state.initial_candidate_count == 0) return 0.0;
//     if (eve_state.candidate_keys.empty())       return 0.0;
//
//     return std::log2(
//         static_cast<double>(eve_state.initial_candidate_count) /
//         static_cast<double>(eve_state.candidate_keys.size()));
// }

bool eve_has_full_key(const EveState& eve_state) {
    return eve_state.log2_candidates <= 0.0;
}

double eve_candidate_count(const EveState& eve_state)
{
    return std::pow(2.0, eve_state.log2_candidates);
}

std::vector<int> eve_best_key_guess(const EveState& eve_state) {

    if (eve_state.candidate_keys.empty()) {
        return std::vector<int>(eve_state.key_length, 0);
    }

    auto best = std::max_element(
        eve_state.candidate_keys.begin(),
        eve_state.candidate_keys.end(),
        [](const EveKeyCandidate& a, const EveKeyCandidate& b) {
            return a.probability < b.probability;
        });

    return best -> bits;
}

// ---------------------------------------------
// Attack Result
// ---------------------------------------------

AttackResult run_manipulate_observe_attack(
    std::size_t     num_qubits,
    double          intercept_prob,
    double          qber_threshold,
    double          check_fraction,
    double          channel_ber,
    std::size_t     num_passes,
    unsigned        base_seed,
    unsigned        rng_seed) {

    QuantumRNG rng(rng_seed == 0 ? std::random_device{}() : rng_seed);

    std::size_t est_sifted = static_cast<std::size_t>(
        num_qubits * 0.5 * (1.0 - check_fraction));

    EveState eve = initialise_eve(est_sifted,
                                 channel_ber,
                                 num_passes,
                                 base_seed);

    std::vector<int>    alice_bits (num_qubits);
    std::vector<Basis>  alice_bases(num_qubits);
    std::vector<Qubit>  channel    (num_qubits);

    for (std::size_t i = 0; i < num_qubits; ++i) {
        alice_bits[i]  = rng.random_bit();
        alice_bases[i] = rng.random_basis();
        channel[i]     = encode(alice_bits[i], alice_bases[i]);
    }

    for (std::size_t i = 0; i < num_qubits; ++i) {
        channel[i] = eve_partial_intercept(channel[i],
                                           i,
                                           intercept_prob,
                                           eve,
                                           rng);
    }

    std::cout << "  Qubit records size: "
              << eve.qubit_records.size() << "\n" << std::flush;

    std::vector<Basis> bob_bases(num_qubits);
    std::vector<int>   bob_bits(num_qubits);
    for (std::size_t i = 0; i < num_qubits; ++i) {
        bob_bases[i] = rng.random_basis();
        bob_bits[i] = measure(channel[i], bob_bases[i], rng);
    }

    std::vector<int>         alice_sifted;
    std::vector<int>         bob_sifted;
    std::vector<std::size_t> sifted_positions;

    for (std::size_t i = 0; i < num_qubits; ++i) {
        if (alice_bases[i] == bob_bases[i]) {
            alice_sifted    .push_back(alice_bits[i]);
            bob_sifted      .push_back(bob_bits[i]);
            sifted_positions.push_back(i);
        }
    }
    std::cout << "  Sifted length: " << alice_sifted.size() << "\n"
             << std::flush;

    eve_process_basis_reconciliation(eve,
                           alice_bases,
                           bob_bases,
                           sifted_positions);

    std::cout << "  Known bits    : " << eve.known_bits.size()        << "\n"
              << "  Unknown bits  : " << eve.unknown_positions.size() << "\n"
              << std::flush;

    eve.key_length = alice_sifted.size();

    BinarySymmetricChannel noisy_channel(channel_ber);
    std::vector<int> bob_noisy = noisy_channel.transmit(bob_sifted);
    ChannelMetrics metrics     = noisy_channel.metrics();

    std::size_t sifted_len = alice_sifted.size();
    std::size_t check_count = std::max(std::size_t(1),
                                static_cast<std::size_t>(
                                    sifted_len * check_fraction));
    check_count = std::min(check_count, sifted_len);

    std::vector<std::size_t> indices(sifted_len);
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(),
        std::mt19937{std::random_device{}()});

    std::size_t errors = 0;
    std::vector<bool> check_used(sifted_len, false);
    for (std::size_t k = 0; k < check_count; ++k) {
        check_used[indices[k]] = true;
        if (alice_sifted[indices[k]] != bob_noisy[indices[k]]) ++errors;
    }

    double qber = static_cast<double>(errors) / static_cast<double>(check_count);
    bool eve_detected = (qber > qber_threshold);

    std::vector<int> alice_key, bob_key;
    for (std::size_t k = 0; k < sifted_len; ++k) {
        if (!check_used[k]) {
            alice_key.push_back(alice_sifted[k]);
            bob_key.push_back(bob_noisy[k]);
        }
    }

    eve_build_candidate_keys(eve);

        std::cout << "Active phase complete:\n"
              << "  Sifted key length  : " << alice_key.size()   << "\n"
              << "  Known bits (Eve)   : " << eve.known_bits.size() << "\n"
              << "  Unknown positions  : " << eve.unknown_positions.size() << "\n"
              << "  Initial candidates : " << eve.log2_candidates << "\n"
              << "  QBER introduced    : " << qber * 100 << "%\n"
              << "  Eve detected       : " << (eve_detected ? "YES" : "NO") << "\n";

    // If Eve was detected protocol would abort — but we continue to measure
    // passive phase information gain anyway for research purposes
    if (eve_detected) {
        std::cout << "  Note: Eve detected — protocol would abort in practice\n";
    }

    // ── Step 4: Passive phase — observe Cascade ───────────────────────────────
    // Collect public parity announcements during each Cascade pass
    // and use them to filter Eve's candidate keys

    // Storage for public announcements Eve intercepts
    std::vector<std::vector<Block>> public_blocks_per_pass  (num_passes);
    std::vector<std::vector<int>>   public_parities_per_pass(num_passes);

    // Run Cascade with a parity observer callback
    // The lambda captures Eve's state and the public announcement vectors
    auto parity_observer = [&](std::size_t pass,
                                std::size_t block_id,
                                int         parity) {
        // Simply record pass and parity — no block lookup needed
        // Eve reconstructs the block from her own pass structure
        if (pass < num_passes) {
            if (pass < eve.eve_pass_blocks.size() &&
                block_id < eve.eve_pass_blocks[pass].size())
            {
                public_blocks_per_pass  [pass].push_back(
                    eve.eve_pass_blocks[pass][block_id]);
                public_parities_per_pass[pass].push_back(parity);
            } else {
                // Block not in Eve's reconstruction yet — just record parity
                // Eve will use her reconstructed block for filtering
                public_parities_per_pass[pass].push_back(parity);
            }
        }
    };

    eve.key_length      = alice_key.size();
    eve.estimated_ber   = channel_ber;  // also explicitly reset in case struct had wrong type

    std::cout << "Eve key_length set to: " << eve.key_length    << "\n";
    std::cout << "Eve estimated_ber    : " << eve.estimated_ber << "\n";

    // Cascade reconstructs blocks internally — Eve pre-reconstructs
    // her copy of each pass's blocks before Cascade runs
    for (std::size_t pass = 0; pass < num_passes; ++pass) {
        std::cout << "Reconstructing blocks for pass " << pass << "\n"
              << std::flush;
        eve_reconstruct_blocks(eve, pass);
        std::cout << "  Pass " << pass << " blocks: "
             << eve.eve_pass_blocks[pass].size() << "\n"
             << std::flush;
    }

    // Run Cascade with Eve observing
    std::vector<int> corrected = cascade_correct(
        alice_key,
        bob_key,
        metrics,
        num_passes,
        base_seed,
        parity_observer
    );

    // ── Step 5: Process all passive phase observations ────────────────────────
    for (std::size_t pass = 0; pass < num_passes; ++pass) {
        std::cout << "\nPassive phase — Pass " << pass << ":\n"
                  << "  Blocks observed    : "
                  << public_blocks_per_pass[pass].size()  << "\n"
                  << "  Candidates before  : "
                  << eve.log2_candidates            << "\n";

        eve_observe_cascade_pass(eve,
                                  pass,
                                  public_blocks_per_pass[pass],
                                  public_parities_per_pass[pass]);

        std::cout << "  log2(candidates) after : "
          << eve.log2_candidates                    << "\n"  // AFTER
          << "  Info gained so far     : "
          << eve_information_gained(eve)            << " bits\n";

        std::cout << "  Candidates after   : "
                  << eve.candidate_keys.size() << "\n"
                  << "  Info gained so far : "
                  << eve_information_gained(eve) << " bits\n";

        if (eve_has_full_key(eve)) {
            std::cout << "  Key fully compromised after pass "
                      << pass << "!\n";
            break;
        }
    }


    // ── Step 6: Measure attack success ────────────────────────────────────────
    std::vector<int> eve_guess = eve_best_key_guess(eve);

    std::size_t matching_bits = 0;
    for (std::size_t i = 0;
         i < std::min(eve_guess.size(), alice_key.size()); ++i) {
        if (eve_guess[i] == alice_key[i]) ++matching_bits;
    }

    AttackResult result;
    result.key_length               = eve.key_length;
    result.alice_key                = alice_sifted;
    result.corrected                = corrected;
    result.log2_initial_candidates  = static_cast<double>(eve.initial_candidate_count_log2);
    result.log2_final_candidates    = eve.log2_candidates;
    result.information_gained  = eve_information_gained(eve);
    result.key_compromised     = eve_has_full_key(eve);
    result.eve_key_guess       = eve_guess;
    result.bits_known_active   = eve.bits_known_active;
    result.bits_known_passive  = eve.bits_known_passive;
    result.qber_introduced     = qber;
    result.observations_made   = eve.observations.size();

    // // After passive phase observations are complete
    // std::cout << "\nStarting brute force phase...\n" << std::flush;
    //
    // auto final_candidates = eve_passive_phase_brute_force(eve, 0);
    //
    // if (final_candidates.size() == 1) {
    //     std::cout << "KEY FULLY COMPROMISED\n" << std::flush;
    //     result.key_compromised = true;
    //     result.eve_key_guess   = final_candidates[0];
    //
    //     // Verify against Alice's key
    //     std::size_t correct = 0;
    //     for (std::size_t i = 0; i < alice_key.size(); ++i) {
    //         if (final_candidates[0][i] == alice_key[i]) ++correct;
    //     }
    //     std::cout << "Verification: " << correct << "/"
    //               << alice_key.size() << " bits correct\n";
    //
    // } else {
    //     std::cout << "Candidates remaining: "
    //               << final_candidates.size() << "\n";
    //     result.key_compromised = false;
    //     if (!final_candidates.empty()) {
    //         result.eve_key_guess = final_candidates[0];
    //     }
    // }

    // Print final summary
    std::cout << "\n═══════════════════════════════════════════════════\n"
              << "  Attack Complete\n"
              << "═══════════════════════════════════════════════════\n"
              << "  Initial candidates : " << result.log2_initial_candidates << "\n"
              << "  Final candidates   : " << result.log2_final_candidates   << "\n"
              << "  Information gained : " << result.information_gained << " bits\n"
              << "  Bits from active   : " << result.bits_known_active  << "\n"
              << "  Bits from passive  : " << result.bits_known_passive << "\n"
              << "  Observations made  : " << result.observations_made  << "\n"
              << "  Key compromised    : "
              << (result.key_compromised ? "YES" : "NO") << "\n"
              << "  QBER introduced    : "
              << result.qber_introduced * 100 << "%\n"
              << "  Key bits matching  : "
              << matching_bits << "/" << alice_key.size() << "\n";

    // std::cout << "\n═══════════════════════════════════════════════════\n"
    //       << "  Attack Complete\n"
    //       << "═══════════════════════════════════════════════════\n"
    //       << "  Initial uncertainty : 2^"
    //       << eve.initial_candidate_count_log2 << " candidates\n"
    //       << "  Final uncertainty   : 2^"
    //       << eve.log2_candidates << " candidates\n"
    //       << "  Information gained  : "
    //       << eve_information_gained(eve) << " bits\n"
    //       << "  Bits from active    : " << eve.bits_known_active   << "\n"
    //       << "  Bits from passive   : " << eve.bits_known_passive  << "\n"
    //       << "  Observations made   : " << eve.observations.size() << "\n"
    //       << "  Key compromised     : "
    //       << (eve_has_full_key(eve) ? "YES" : "NO") << "\n"
    //       << "  QBER introduced     : "
    //       << result.qber_introduced * 100 << "%\n";

    return result;
}

int main() {

    constexpr std::size_t       NUM_QUBITS      = 1000;
    constexpr double            INTERCEPT_PROB  = 0.39;
    constexpr double            CHANNEL_BER     = 0.02;
    constexpr double            QBER_THRESHOLD  = 0.11;
    constexpr double            CHECK_FRACTION  = 0.10;
    constexpr std::size_t       NUM_RUNS        = 30;

    // AttackResult result = run_manipulate_observe_attack(
    //     NUM_QUBITS,
    //     INTERCEPT_PROB,
    //     QBER_THRESHOLD,
    //     CHECK_FRACTION,
    //     CHANNEL_BER);
    //
    // std::cout << result.qber_introduced << std::endl;
    // std::cout << result.log2_final_candidates << std::endl;
    // std::cout << result.key_compromised << std::endl;

    // Without varying anything, testing how much the QBER and the Info gained varies
    // std::cout << "===============================================================               \n"
    //           << "Testing the standard deviation per signal sent for " << NUM_QUBITS << " qubits\n"
    //           << "===============================================================               \n";
    // std::cout << "Intercept rates, Initial candidates, Final candidates, Information gained, Bits active, Bits passive, "
    //           << "qber, Errors after, Secure bits\n";

    std::vector<double> total_qber;
    std::vector<double> total_known;
    std::vector<std::size_t> key_length;
    std::vector<double> standard_deviation;

    AttackResult result = run_manipulate_observe_attack(
        NUM_QUBITS,
        INTERCEPT_PROB,
        QBER_THRESHOLD,
        CHECK_FRACTION,
        CHANNEL_BER);


    // for (std::size_t run = 0; run < NUM_RUNS; ++run) {
    //     double intercept = 0.10 + run * (0.5 / NUM_RUNS);
    //     // double target_ber = 0.01 + (run * (QBER_THRESHOLD - 0.01) / static_cast<double>(NUM_RUNS - 1));
    //
    //     AttackResult result = run_manipulate_observe_attack(
    //     NUM_QUBITS,
    //     intercept,
    //     QBER_THRESHOLD,
    //     CHECK_FRACTION,
    //     CHANNEL_BER);
    //
    //     std::size_t errors_after = 0;
    //     for (std::size_t i = 0; i < result.key_length; ++i) {
    //         if (result.alice_key[i] != result.corrected[i]) ++errors_after;
    //     }
    //
    //     std::cout << intercept                      << ","
    //               << result.log2_initial_candidates << ","
    //               << result.log2_final_candidates   << ","
    //               << result.information_gained      << ","
    //               << result.bits_known_active       << ","
    //               << result.bits_known_passive      << ","
    //               << result.qber_introduced         << ","
    //               << errors_after                   << ","
    //               << result.key_length              << "\n";
    // }

    //
    //     total_qber.push_back(result.qber_introduced);
    //     total_known.push_back(result.information_gained);
    //     key_length.push_back(result.key_length);
    //     }
    //
    //
    // double mean_qber = std::accumulate(total_qber.begin(), total_qber.end(), 0.0) / total_qber.size();
    // double mean_known = std::accumulate(total_known.begin(), total_known.end(), 0.0) / total_known.size();
    // double mean_length = std::accumulate(key_length.begin(), key_length.end(), 0.0) / key_length.size();
    // double sum = 0;
    //
    // for (int j = 0; j < total_qber.size(); ++j) {
    //     sum += std::pow((total_qber[j] - mean_qber), 2);
    // }
    // standard_deviation.push_back(std::sqrt(sum / total_qber.size()));
    // }
    // for 100 bits  : QBER Mean = 0.12   & Info Gained Mean = 21.7667
    // for 1000 bits : QBER Mean = 0.1963 & Info Gained Mean = 205.933
    // for 10000 bits: QBER Mean = 0.1802 & Info Gained Mean = 2053.47
    // ...



    // std::cout << "=======================================\n";
    // std::cout << " Manipulate and Observe Attack Results \n";
    // std::cout << "=======================================\n";
    // std::cout << "Initial candidates : "
    //           << attack.initial_candidates << "\n";
    // std::cout << "Final candidates   : "
    //           << attack.final_candidates   << "\n";
    // std::cout << "Information gained : "
    //           << attack.information_gained << "\n";
    // std::cout << "Bits from active   : "
    //           << attack.bits_known_active <<  "\n";
    // std::cout << "Bits from passive  : "
    //           << attack.bits_known_passive << "\n";
    // std::cout << "Key compromised    : "
    //           << (attack.key_compromised ? "YES" : "NO") << "\n";
    // std::cout << "QBER introduced    : "
    //           << attack.qber_introduced * 100 << "%\n";

    return 0;
}
