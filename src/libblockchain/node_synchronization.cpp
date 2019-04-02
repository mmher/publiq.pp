#include "node_synchronization.hpp"
#include "common.hpp"
#include "node_internals.hpp"

#include <belt.pp/meta.hpp>

#include <chrono>

using namespace BlockchainMessage;
namespace chrono = std::chrono;
using system_clock = chrono::system_clock;

namespace publiqpp
{
node_synchronization::node_synchronization(detail::node_internals& impl)
    : pimpl(&impl)
    , blockchain_sync_in_progress(false)
{

}
BlockHeaderExtended node_synchronization::net_sync_info() const
{
    BlockHeaderExtended result;
    BlockHeaderExtended my_sync_info = own_sync_info();

    result.c_sum = 0;
    result.block_number = 0;

    for (auto const& item : sync_responses)
    {
        BlockHeaderExtended const& sync_info = item.second.promised_header;

        if (sync_info.block_number == my_sync_info.block_number &&
            sync_info.c_sum > result.c_sum)
        {
            result = sync_info;
        }
    }

    return result;
}

BlockHeaderExtended node_synchronization::own_sync_info() const
{
    BlockHeaderExtended result = pimpl->m_blockchain.header_ex_at(pimpl->m_blockchain.length() - 1);

    // calculate delta for next block for the case if I will mine it
    if (pimpl->is_miner())
    {
        uint64_t delta = pimpl->calc_delta(pimpl->m_pb_key.to_string(),
                                           pimpl->get_balance().whole,
                                           result.block_hash,
                                           result.c_const);

        result.prev_hash = result.block_hash;
        result.block_hash.clear();
        result.delta = delta;
        result.c_sum += delta;
        auto time_signed = system_clock::from_time_t(result.time_signed.tm) + chrono::minutes(10);
        result.time_signed.tm = system_clock::to_time_t(time_signed);
        ++result.block_number;
        //
        //  c_const and block_hash are incomplete at this moment
    }

    return result;
}
}
