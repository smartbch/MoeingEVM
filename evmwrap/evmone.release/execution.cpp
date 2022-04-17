// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019-2020 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "execution.hpp"
#include "analysis.hpp"
#include "cache.hpp"
#include <memory>

namespace evmone
{
evmc_result execute(AdvancedExecutionState& state, const AdvancedCodeAnalysis& analysis) noexcept
{
    state.analysis = &analysis;  // Allow accessing the analysis by instructions.

    const auto* instr = &state.analysis->instrs[0];  // Start with the first instruction.
    while (instr != nullptr)
        instr = instr->fn(instr, state);

    const auto gas_left =
        (state.status == EVMC_SUCCESS || state.status == EVMC_REVERT) ? state.gas_left : 0;

    return evmc::make_result(
        state.status, gas_left, state.memory.data() + state.output_offset, state.output_size);
}

typedef Cache<AdvancedCodeAnalysis> AnalysisCache;
AnalysisCache CacheShards[AnalysisCache::SHARD_COUNT];

evmc_result execute(evmc_vm* /*unused*/, const evmc_host_interface* host, evmc_host_context* ctx,
    evmc_revision rev, const evmc_message* msg, const uint8_t* code, size_t code_size) noexcept
{
    auto state = std::make_unique<AdvancedExecutionState>(*msg, rev, *host, ctx, code, code_size);
    std::string key((const char*)(msg->destination.bytes), 20);
    int sid = int(msg->destination.bytes[19]) % AnalysisCache::SHARD_COUNT; //shard id
    const auto height = static_cast<uint32_t>(state->host.get_tx_context().block_number);
    const AdvancedCodeAnalysis& analysis = CacheShards[sid].borrow(key);
    evmc_result res;
    if(analysis.instrs.size() > 0) { // cache hit
            res = execute(*state, analysis);
	    CacheShards[sid].give_back(key, height);
    } else { //cache miss
	    auto new_analysis = analyze(rev, code, code_size);
            res = execute(*state, new_analysis);
	    CacheShards[sid].add(key, new_analysis, height);
    }
    return res;
}
}  // namespace evmone
