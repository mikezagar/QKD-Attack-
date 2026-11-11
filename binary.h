//
// Created by Mike Zagar on 2026-06-03.
//

#ifndef BB84_BINARY_H
#define BB84_BINARY_H

#pragma once

#include <vector>
#include <utility>
#include <cstddef>
#include <random>
#include <iostream>
#include <string>

struct Block {
    std::size_t start;   // inclusive start index into the full key
    std::size_t end;     // exclusive end index
    std::size_t pass;    // which Cascade pass this block belongs to
    int parity;     // parity of block
};

struct BinarySearch {
    Block block;    // current block being bisected
    std::size_t error_index; // resolved error position
    bool resolved;  // true once error is pinpointed to a single bit
};

// compute parity for a subrange of a key
int compute_parity(const std::vector<int>& key, std::size_t start, std::size_t end) {
    int parity = 0;
    for (std::size_t i = start; i < end; i++) {
        parity ^= key[i];
    }
    return parity;
}
/**
int compute_block_parity(const std::vector<int>& key, const Block& block) {
    int parity = 0;
    for (std::size_t i = block.start; i < block.end; ++i) {
        parity ^= key[i];
    }
    return parity;
}
*/
// compute parity of entire block
int compute_block_parity(const std::vector<int>& key, const Block& block,
    const std::vector<std::size_t>& permutation = {}) {
    /**if (permutation.size() != key.size()) {
        throw std::invalid_argument("Permutation size" + std::to_string(permutation.size())
            + "does not match key size " + std::to_string(key.size()));
    }*/
    if (block.end > key.size()) {
        throw std::invalid_argument("Block end" + std::to_string(block.end)
            + "exceeds key size " + std::to_string(key.size()));
    }
    int parity = 0;
    for (std::size_t i = block.start; i < block.end; ++i) {
        std::size_t index = permutation.empty() ? i : permutation[i];
        parity ^= key[index];
        //parity ^= key[permutation[i]];
    }
    return parity;
}

// Partition a key of some length n into blocks for a given pass
// Returns a vector of Block structs covering the full key
std::vector<Block> create_block(std::size_t key_length, std::size_t block_size, std::size_t pass) {
    std::size_t num_blocks = (key_length + block_size - 1) / block_size;
    std::vector<Block> blocks;
    blocks.reserve(num_blocks);

    std::size_t pos = 0;
    while (pos < key_length) {
        std::size_t start = pos;
        std::size_t end = std::min(pos + block_size, key_length);

        Block block;
        block.start = start;
        block.end = end;
        block.pass = pass;
        block.parity = 0;

        blocks.push_back(block);

        pos += block_size;
    }
    return blocks;
}

// Splits a block into its left and right halves for binary search
// ONLY FOR EVEN BLOCKS
std::pair<Block,Block> bisect_block(const Block& block) {
    if (block.end - block.start < 2) {
        throw std::invalid_argument("Block too small to bisect: start="
            + std::to_string(block.start) + "end=" + std::to_string(block.end));
    }
    std::size_t midpoint = block.start + (block.end - block.start) / 2;

    Block left;
    left.start = block.start;
    left.end = midpoint;
    left.pass = block.pass;
    left.parity = 0;

    Block right;
    right.start = midpoint;
    right.end = block.end;
    right.pass = block.pass;
    right.parity = 0;

    return {left,right};
}


// Calculated the recommended block size for a given pass and BER, using the formula
// k1 = ceil(0.73 / BER), where each pass doubles the block size
std::size_t recommended_block_size(std::size_t pass, double estimated_ber) {

    if (estimated_ber <= 0.0 || estimated_ber >= 0.5) {
        throw std::invalid_argument("BER must be in range of (0.0, 0.5) for Cascade");
    }

    std::size_t k1 = static_cast<std::size_t>(std::ceil(0.73 / estimated_ber));
    k1 = std::max(k1, std::size_t{1});

    std::size_t block_size = k1 << pass;
    block_size = std::max(block_size, std::size_t{2});

    return block_size;
}

// Single step of binary search
// Alice and Bob measure the parity of the left half
// The error is whichever half has odd parity
BinarySearch binary_search_step(const std::vector<int>& alice_key,
    const std::vector<int>& bob_key, const BinarySearch& search) {
    auto [left, right] = bisect_block(search.block);

    int alice_parity = compute_block_parity(alice_key, left);
    int bob_parity = compute_block_parity(bob_key, left);

    Block next_block = (alice_parity != bob_parity) ? left : right;

    BinarySearch next_search;
    next_search.block = next_block;
    next_search.resolved = (next_block.end - next_block.start) == 1;
    next_search.error_index = next_search.resolved ? next_block.start : 0;

    return next_search;
}

// Run binary search in completion on a block known to contain one error
// Returns the index of the error
std::size_t binary_search_full(const std::vector<int>& alice_key,
    std::vector<int>& bob_key, const Block& block, const std::vector<std::size_t>& permutation) {

    std::size_t len = block.end - block.start;

    std::vector<int> alice_permuted(len);
    std::vector<int> bob_permuted(len);

    for (std::size_t i = 0;  i < len; ++i) {
        std::size_t original = permutation[block.start + i];
        alice_permuted[i] = alice_key[original];
        bob_permuted[i] = bob_key[original];
    }

    Block local_block{0, len, block.pass, block.parity};

    BinarySearch search;
    search.block = local_block;
    search.resolved = (len == 1);
    search.error_index = 0;

    while (!search.resolved) {
        search = binary_search_step(alice_permuted, bob_permuted, search);
    }

    std::size_t permuted_pos = block.start + search.error_index;

    return permuted_pos;

}

#endif //BB84_BINARY_H
