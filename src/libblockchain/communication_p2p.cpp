#include "communication_p2p.hpp"
#include "communication_rpc.hpp"
#include "transaction_handler.hpp"

#include "coin.hpp"
#include "common.hpp"
#include "exception.hpp"

#include <mesh.pp/cryptoutility.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

using namespace BlockchainMessage;

using std::map;
using std::set;
using std::unordered_map;
using std::unordered_set;

namespace publiqpp
{
///////////////////////////////////////////////////////////////////////////////////
//                            Internal Functions
bool apply_transaction(SignedTransaction const& signed_transaction,
                       publiqpp::detail::node_internals& impl,
                       string const& key/* = string()*/)
{
    if (false == action_can_apply(impl, signed_transaction.transaction_details.action))
        return false;

    action_apply(impl, signed_transaction.transaction_details.action);

    if (false == fee_can_apply(impl, signed_transaction))
        return false;

    fee_apply(impl, signed_transaction, key);

    return true;
}

void revert_transaction(SignedTransaction const& signed_transaction,
                        publiqpp::detail::node_internals& impl,
                        string const& key/* = string()*/)
{
    fee_revert(impl, signed_transaction, key);

    action_revert(impl, signed_transaction.transaction_details.action);
}

void validate_delations(map<string, StatInfo> const& right_delations,
                        map<string, StatInfo> const& check_delations,
                        map<string, uint64_t>& penals)
{
    penals.clear();
    // to check an algorithm for now

    for (auto const& right_item_it : right_delations)
    {
        for (auto const& right_info_item : right_item_it.second.items)
        {
            bool compare_failed = true;
            auto check_item_it = check_delations.find(right_info_item.node_address);

            if(check_item_it != check_delations.end())
                for (auto const& check_info_item : check_item_it->second.items)
                {
                    if (check_info_item.node_address == right_item_it.first)
                    {
                        if (check_info_item.passed == right_info_item.passed)
                            compare_failed = false;

                        break;
                    }
                }

            if (compare_failed)
                ++penals[right_info_item.node_address];
        }
    }
}

void filter_penals(map<string, uint64_t> const& penals, set<string>& result)
{
    result.clear();
    // to check an algorithm for now

    if (penals.empty())
        return;

    uint64_t total_count = penals.size();
    uint64_t remove_count = total_count * 50 / 100; // 50%
    remove_count = remove_count > 0 ? remove_count : 1;

    map<uint64_t, uint64_t> filter_map;

    for (auto const& it : penals)
        ++filter_map[it.second];

    uint64_t threshold = filter_map.cbegin()->first + 1;

    if (filter_map.size() > 1)
    {
        threshold = filter_map.cbegin()->first;

        while (remove_count > filter_map.cbegin()->second)
        {
            filter_map.erase(threshold);
            threshold = filter_map.cbegin()->first;
        }
    }

    for (auto const& it : penals)
        if(it.second < threshold)
            result.insert(it.first);
}

void grant_rewards(vector<SignedTransaction> const& signed_transactions, 
                   vector<Reward>& rewards, 
                   string const& address,
                   uint64_t block_number,
                   publiqpp::detail::node_internals& impl)
{
    rewards.clear();

    coin fee = coin(0, 0);
    map<string, uint64_t> channel_penals;
    map<string, uint64_t> storage_penals;
    map<string, StatInfo> channel_delations;
    map<string, StatInfo> storage_delations;

    for (auto it = signed_transactions.begin(); it != signed_transactions.end(); ++it)
    {
        fee += it->transaction_details.fee;

        if (it->transaction_details.action.type() == StatInfo::rtt)
        {
            StatInfo stat_info;
            it->transaction_details.action.get(stat_info);

            NodeType node_type;
            if (impl.m_state.get_role(it->authority, node_type))
            {
                if (node_type == NodeType::channel)
                {
                    for (auto const& item : stat_info.items)
                    {
                        storage_penals[item.node_address] = 0;
                        channel_delations[item.node_address] = stat_info;
                    }
                }
                else if (node_type == NodeType::storage)
                {
                    for (auto const& item : stat_info.items)
                    {
                        channel_penals[item.node_address] = 0;
                        storage_delations[item.node_address] = stat_info;
                    }
                }
            }
        }
    }

    size_t year_index = block_number / 50000;
    coin miner_reward, channel_reward, storage_reward;

    if (year_index < 60)
    {
        miner_reward += BLOCK_REWARD_ARRAY[year_index] * MINER_REWARD_PERCENT / 100;
        channel_reward += BLOCK_REWARD_ARRAY[year_index] * CHANNEL_REWARD_PERCENT / 100;
        storage_reward += BLOCK_REWARD_ARRAY[year_index] - miner_reward - channel_reward;
    }

    // grant channel rewards
    set<string> channels;
    validate_delations(channel_delations, storage_delations, storage_penals);
    filter_penals(channel_penals, channels);

    if (channels.size() && !channel_reward.empty())
    {
        Reward reward;
        reward.reward_type = RewardType::channel;
        coin channel_portion = channel_reward / channels.size();

        for (auto const& item : channels)
        {
            reward.to = item;
            channel_portion.to_Coin(reward.amount);

            rewards.push_back(reward);
        }

        (channel_reward - channel_portion * (channels.size() - 1)).to_Coin(rewards.back().amount);
    }
    else
        miner_reward += channel_reward;

    // grant storage rewards
    set<string> storages;
    validate_delations(storage_delations, channel_delations, channel_penals);
    filter_penals(storage_penals, storages);

    if (storages.size() && !storage_reward.empty())
    {
        Reward reward;
        reward.reward_type = RewardType::storage;
        coin storage_portion = storage_reward / storages.size();

        for (auto const& item : storages)
        {
            reward.to = item;
            storage_portion.to_Coin(reward.amount);

            rewards.push_back(reward);
        }

        (storage_reward - storage_portion * (storages.size() - 1)).to_Coin(rewards.back().amount);
    }
    else
        miner_reward += storage_reward;

    // grant miner reward himself
    if (!miner_reward.empty())
    {
        Reward reward;
        reward.to = address;
        miner_reward.to_Coin(reward.amount);
        reward.reward_type = RewardType::miner;

        rewards.push_back(reward);
    }
}

//  this has opposite bool logic - true means error :)
bool check_headers(BlockHeader const& next_header, BlockHeader const& header)
{
    bool t = next_header.block_number != header.block_number + 1;
    t = t || next_header.c_sum <= header.c_sum;
    t = t || next_header.c_sum != next_header.delta + header.c_sum;
    t = t || (next_header.c_const != header.c_const &&
              next_header.c_const != header.c_const * 2 && 
              next_header.c_const != header.c_const / 2);

    system_clock::time_point time_point1 = system_clock::from_time_t(header.time_signed.tm);
    system_clock::time_point time_point2 = system_clock::from_time_t(next_header.time_signed.tm);
    chrono::seconds diff_seconds = chrono::duration_cast<chrono::seconds>(time_point2 - time_point1);

    return t || time_point1 > time_point2 || diff_seconds.count() < BLOCK_MINE_DELAY;
}

bool check_rewards(Block const& block, string const& authority,
                   publiqpp::detail::node_internals& impl)
{
    vector<Reward> rewards;
    grant_rewards(block.signed_transactions, rewards, authority, block.header.block_number, impl);

    auto it1 = rewards.begin();
    auto it2 = block.rewards.begin();

    bool bad_reward = rewards.size() != block.rewards.size();

    while (!bad_reward && it1 != rewards.end())
    {
        bad_reward = *it1 != *it2;

        ++it1;
        ++it2;
    }

    return bad_reward;
}

void broadcast_storage_info(publiqpp::detail::node_internals& impl)
{//TODO
    vector<string> storages = impl.m_state.get_nodes_by_type(NodeType::storage);

    if (storages.empty()) return;

    uint64_t block_number = impl.m_blockchain.length() - 1;
    SignedBlock const& signed_block = impl.m_blockchain.at(block_number);

    StatInfo stat_info;
    stat_info.hash = meshpp::hash(signed_block.block_details.to_string());
    stat_info.reporter_address = impl.m_pb_key.to_string();

    for (auto& nodeid : storages)
    {
        StatItem stat_item;
        stat_item.node_address = nodeid;
        //stat_item.content_hash = meshpp::hash("storage");
        stat_item.passed = 1;
        stat_item.failed = 0;

        stat_info.items.push_back(stat_item);
    }

    Transaction transaction;
    transaction.action = stat_info;
    transaction.creation.tm = system_clock::to_time_t(system_clock::now());
    transaction.expiry.tm = system_clock::to_time_t(system_clock::now() + chrono::minutes(10));

    SignedTransaction signed_transaction;
    signed_transaction.authority = impl.m_pb_key.to_string();
    signed_transaction.transaction_details = transaction;
    signed_transaction.signature = impl.m_pv_key.sign(transaction.to_string()).base58;

    Broadcast broadcast;
    broadcast.echoes = 2;
    broadcast.package = signed_transaction;

    broadcast_message(std::move(broadcast),
                      impl.m_ptr_p2p_socket->name(),
                      impl.m_ptr_p2p_socket->name(),
                      true,
                      nullptr,
                      impl.m_p2p_peers,
                      impl.m_ptr_p2p_socket.get());
}

void revert_pool(time_t expiry_time, 
                 publiqpp::detail::node_internals& impl,
                 vector<SignedTransaction>& pool_transactions)
{
    //  collect transactions to be reverted from pool
    //
    pool_transactions.clear();
    size_t state_pool_size = impl.m_transaction_pool.length();

    for (size_t index = 0; index != state_pool_size; ++index)
    {
        SignedTransaction const& signed_transaction = impl.m_transaction_pool.at(index);

        if (expiry_time <= signed_transaction.transaction_details.expiry.tm)
            pool_transactions.push_back(signed_transaction);
    }

    //  revert transactions from pool
    //
    for (size_t index = state_pool_size; index != 0; --index)
    {
        SignedTransaction const& signed_transaction = impl.m_transaction_pool.at(index - 1);

        impl.m_transaction_pool.pop_back();
        impl.m_action_log.revert();
        impl.m_transaction_cache.erase(meshpp::hash(signed_transaction.to_string()));
        revert_transaction(signed_transaction, impl);
    }

    assert(impl.m_transaction_pool.length() == 0);
}

///////////////////////////////////////////////////////////////////////////////////

void mine_block(unique_ptr<publiqpp::detail::node_internals>& m_pimpl)
{
    auto now = system_clock::now();

    auto transaction_cache_backup = m_pimpl->m_transaction_cache;
    beltpp::on_failure guard([&m_pimpl, transaction_cache_backup]
    {
        m_pimpl->discard();
        m_pimpl->m_transaction_cache = std::move(transaction_cache_backup);
    });

    std::multimap<BlockchainMessage::ctime, std::pair<string, SignedTransaction>> transaction_map;

    vector<SignedTransaction> pool_transactions;
    //  collect transactions to be reverted from pool
    //  revert transactions from pool
    revert_pool(system_clock::to_time_t(now), *m_pimpl.get(), pool_transactions);

    uint64_t block_number = m_pimpl->m_blockchain.length() - 1;
    SignedBlock const& prev_signed_block = m_pimpl->m_blockchain.at(block_number);

    BlockHeader const& prev_header = m_pimpl->m_blockchain.last_header();

    string own_key = m_pimpl->m_pb_key.to_string();
    string prev_hash = meshpp::hash(prev_signed_block.block_details.to_string());
    uint64_t delta = m_pimpl->calc_delta(own_key,
                                         m_pimpl->get_balance().whole,
                                         prev_hash,
                                         prev_header.c_const);

    // fill new block header data
    BlockHeader block_header;
    block_header.block_number = block_number + 1;
    block_header.delta = delta;
    block_header.c_const = prev_header.c_const;
    block_header.c_sum = prev_header.c_sum + delta;
    block_header.prev_hash = prev_hash;
    block_header.time_signed.tm = system_clock::to_time_t(now);

    // update consensus_const if needed
    if (delta > DELTA_UP)
    {
        size_t step = 0;
        BlockHeader const* prev_header_local =
                &m_pimpl->m_blockchain.header_at(block_number);

        while (prev_header_local->delta > DELTA_UP &&
            step <= DELTA_STEP && prev_header_local->block_number > 0)
        {
            ++step;
            prev_header_local = &m_pimpl->m_blockchain.header_at(prev_header_local->block_number - 1);
        }

        // -1 because current delta is not counted
        if (step >= DELTA_STEP - 1)
            block_header.c_const = prev_header.c_const * 2;
    }
    else if (delta < DELTA_DOWN && block_header.c_const > 1)
    {
        size_t step = 0;
        BlockHeader const* prev_header_local =
                &m_pimpl->m_blockchain.header_at(block_number);

        while (prev_header_local->delta < DELTA_DOWN &&
               step <= DELTA_STEP && prev_header_local->block_number > 0)
        {
            ++step;
            prev_header_local = &m_pimpl->m_blockchain.header_at(prev_header_local->block_number - 1);
        }

        // -1 because current delta is not counted
        if (step >= DELTA_STEP - 1)
            block_header.c_const = prev_header.c_const / 2;
    }

    Block block;
    block.header = block_header;

    // check and copy transactions to block
    size_t index;
    for (index = 0; index != std::min(pool_transactions.size(), size_t(BLOCK_MAX_TRANSACTIONS)); ++index)
    {
        auto& signed_transaction = pool_transactions[index];
        if (apply_transaction(signed_transaction, *m_pimpl.get(), own_key))
        {
            string key = meshpp::hash(signed_transaction.to_string());
            block.signed_transactions.push_back(std::move(signed_transaction));
            m_pimpl->m_transaction_cache[key] =
                    system_clock::from_time_t(signed_transaction.transaction_details.creation.tm);
        }
    }

    // grant rewards and move to block
    grant_rewards(block.signed_transactions, block.rewards, own_key, block.header.block_number, *m_pimpl.get());

    meshpp::signature sgn = m_pimpl->m_pv_key.sign(block.to_string());

    SignedBlock signed_block;
    signed_block.signature = sgn.base58;
    signed_block.authority = sgn.pb_key.to_string();
    signed_block.block_details = block;

    // apply rewards to state and action_log
    for (auto& reward : block.rewards)
        m_pimpl->m_state.increase_balance(reward.to, reward.amount);

    // insert to blockchain and action_log
    m_pimpl->m_blockchain.insert(signed_block);
    m_pimpl->m_action_log.log_block(signed_block);

    // apply back rest of the pool content to the state and action_log
    for (; index != pool_transactions.size(); ++index)
    {
        auto& signed_transaction = pool_transactions[index];
        if (apply_transaction(signed_transaction, *m_pimpl.get()))
        {
            string key = meshpp::hash(signed_transaction.to_string());
            m_pimpl->m_action_log.log_transaction(signed_transaction);
            m_pimpl->m_transaction_pool.push_back(signed_transaction);
            m_pimpl->m_transaction_cache[key] =
                    system_clock::from_time_t(signed_transaction.transaction_details.creation.tm);
        }
    }

    m_pimpl->save(guard);
    m_pimpl->calc_sync_info(block);

    m_pimpl->writeln_node("new block mined : " + std::to_string(block_header.block_number));
}

void process_blockheader_request(BlockHeaderRequest const& header_request,
                                 std::unique_ptr<publiqpp::detail::node_internals>& m_pimpl,
                                 beltpp::isocket& sk,
                                 beltpp::isocket::peer_id const& peerid)
{
    // headers always requested in reverse order!

    uint64_t from = m_pimpl->m_blockchain.length() - 1;
    from = from < header_request.blocks_from ? from : header_request.blocks_from;

    uint64_t to = header_request.blocks_to;
    to = to > from ? from : to;
    to = from > HEADER_TR_LENGTH && to < from - HEADER_TR_LENGTH ? from - HEADER_TR_LENGTH : to;

    BlockHeaderResponse header_response;
    for (auto index = from + 1; index > to; --index)
    {
        BlockHeader const& header = m_pimpl->m_blockchain.header_at(index - 1);

        header_response.block_headers.push_back(header);
    }

    sk.send(peerid, header_response);
}

void process_blockheader_response(BlockHeaderResponse&& header_response,
                                  std::unique_ptr<publiqpp::detail::node_internals>& m_pimpl,
                                  beltpp::isocket& sk,
                                  beltpp::isocket::peer_id const& peerid)
{
    // find needed header from own data
    BlockHeader const* tmp_header = &m_pimpl->m_blockchain.last_header();

    // validate received headers
    if (header_response.block_headers.empty())
        throw wrong_data_exception("blockheader response. empty response received!");

    auto r_it = header_response.block_headers.begin();
    if (r_it->block_number == tmp_header->block_number && m_pimpl->sync_headers.empty() &&
        r_it->c_sum <= tmp_header->c_sum)
        throw wrong_data_exception("blockheader response. wrong data received!");

    if (!m_pimpl->sync_headers.empty() && // we have something received before
        tmp_header->block_number >= m_pimpl->sync_headers.rbegin()->block_number)
    {
        // load next mot checked header
        tmp_header = &m_pimpl->m_blockchain.header_at(m_pimpl->sync_headers.rbegin()->block_number - 1);

        if (tmp_header->block_number != r_it->block_number)
            throw wrong_data_exception("blockheader response. unexpected data received!");
    }

    //-----------------------------------------------------//
    auto check_headers_vector = [](vector<BlockHeader> const& header_vector)
    {
        bool t = false;
        auto it = header_vector.begin();
        for (++it; !t && it != header_vector.end(); ++it)
            t = check_headers(*(it - 1), *it);

        return t;
    };
    //-----------------------------------------------------//

    if (check_headers_vector(header_response.block_headers))
        throw wrong_data_exception("blockheader response. wrong data in response!");

    SignedBlock const& tmp_block = m_pimpl->m_blockchain.at(tmp_header->block_number);
    
    string tmp_hash = meshpp::hash(tmp_block.block_details.to_string());

    // find last common header
    bool found = false;
    bool lcb_found = false;
    r_it = header_response.block_headers.begin();
    while (!found && r_it != header_response.block_headers.end())
    {
        if (r_it->prev_hash == tmp_hash &&
            r_it->block_number == tmp_header->block_number + 1)
        {
            found = true;
            lcb_found = true;
            m_pimpl->sync_headers.push_back(std::move(*r_it));
        }
        else if (r_it->block_number > tmp_header->block_number)
            m_pimpl->sync_headers.push_back(std::move(*r_it++));
        else
            found = true;
    }

    if (found)
    {
        while (!lcb_found &&
               r_it != header_response.block_headers.end() &&
               r_it->c_sum > tmp_header->c_sum)
        {
            m_pimpl->sync_headers.push_back(std::move(*r_it++));
            tmp_header = &m_pimpl->m_blockchain.header_at(tmp_header->block_number - 1);
        }

        for (; !lcb_found && r_it != header_response.block_headers.end(); ++r_it)
        {
            if (tmp_header->prev_hash == r_it->prev_hash)
                lcb_found = true;

            if (tmp_header->block_number > 0)
            {
                m_pimpl->sync_headers.push_back(std::move(*r_it));
                tmp_header = &m_pimpl->m_blockchain.header_at(tmp_header->block_number - 1);
            }
        }

        if (lcb_found)
        {
            if (check_headers(*m_pimpl->sync_headers.rbegin(), *tmp_header) ||
                check_headers_vector(m_pimpl->sync_headers))
                throw wrong_data_exception("blockheader response. header check failed!");

            // verify consensus_const
            vector<pair<uint64_t, uint64_t>> delta_vector;

            for (auto const& item : m_pimpl->sync_headers)
                delta_vector.push_back(pair<uint64_t, uint64_t>(item.delta, item.c_const));
        
            uint64_t number = m_pimpl->sync_headers.rbegin()->block_number - 1;
            uint64_t delta_step = number < DELTA_STEP ? number : DELTA_STEP;
        
            for (uint64_t i = 0; i < delta_step; ++i)
            {
                BlockHeader const& _header = m_pimpl->m_blockchain.header_at(number - i);
        
                delta_vector.push_back(pair<uint64_t, uint64_t>(_header.delta, _header.c_const));
            }
        
            for (auto it = delta_vector.begin(); it + delta_step != delta_vector.end(); ++it)
            {
                if (it->first > DELTA_UP)
                {
                    size_t step = 0;
                    uint64_t _delta = it->first;
        
                    while (_delta > DELTA_UP && step < DELTA_STEP && it + step != delta_vector.end())
                    {
                        ++step;
                        _delta = (it + step)->first;
                    }
        
                    if (step >= DELTA_STEP && it->second != (it + 1)->second * 2)
                        throw wrong_data_exception("blockheader response. wrong consensus const up !");
                }
                else if (it->first < DELTA_DOWN && it->second > 1)
                {
                    size_t step = 0;
                    uint64_t _delta = it->first;
        
                    while (_delta < DELTA_DOWN && step < DELTA_STEP && it + step != delta_vector.end())
                    {
                        ++step;
                        _delta = (it + step)->first;
                    }
        
                    if (step >= DELTA_STEP && it->second != (it + 1)->second / 2)
                        throw wrong_data_exception("blockheader response. wrong consensus const down !");
                }
            }

            //3. request blockchain from found point
            BlockchainRequest blockchain_request;
            blockchain_request.blocks_from = m_pimpl->sync_headers.rbegin()->block_number;
            blockchain_request.blocks_to = m_pimpl->sync_headers.begin()->block_number;

            sk.send(peerid, blockchain_request);
            m_pimpl->update_sync_time();
            m_pimpl->store_request(peerid, blockchain_request);

            return;
        }
    }

    if (!found || !lcb_found)
    {
        // request more headers
        BlockHeaderRequest header_request;
        header_request.blocks_from = m_pimpl->sync_headers.rbegin()->block_number - 1;
        header_request.blocks_to = header_request.blocks_from > HEADER_TR_LENGTH ? header_request.blocks_from - HEADER_TR_LENGTH : 0;

        sk.send(peerid, header_request);
        m_pimpl->update_sync_time();
        m_pimpl->store_request(peerid, header_request);
    }
}

void process_blockchain_request(BlockchainRequest const& blockchain_request,
                                std::unique_ptr<publiqpp::detail::node_internals>& m_pimpl,
                                beltpp::isocket& sk,
                                beltpp::isocket::peer_id const& peerid)
{
    // blocks are always requested in regular order

    uint64_t number = m_pimpl->m_blockchain.length() - 1;
    uint64_t from = number < blockchain_request.blocks_from ? number : blockchain_request.blocks_from;

    uint64_t to = blockchain_request.blocks_to;
    to = to < from ? from : to;
    to = to > from + BLOCK_TR_LENGTH ? from + BLOCK_TR_LENGTH : to;
    to = to > number ? number : to;

    BlockchainResponse chain_response;
    for (auto i = from; i <= to; ++i)
    {
        SignedBlock const& signed_block = m_pimpl->m_blockchain.at(i);

        chain_response.signed_blocks.push_back(std::move(signed_block));
    }

    sk.send(peerid, chain_response);
}

void process_blockchain_response(BlockchainResponse&& response,
                                 std::unique_ptr<publiqpp::detail::node_internals>& m_pimpl,
                                 beltpp::isocket& sk,
                                 beltpp::isocket::peer_id const& peerid)
{
    //1. check received blockchain validity

    if (response.signed_blocks.empty())
        throw wrong_data_exception("blockchain response. empty response received!");

    // find last common block
    uint64_t block_number = m_pimpl->sync_headers.back().block_number;

    if (block_number == 0) throw wrong_data_exception("blockchain response. uzum en qcen!");

    //2. check and add received blockchain to sync_blocks_vector for future process
    size_t length = m_pimpl->sync_blocks.size();

    // put prev_signed_block in correct place
    SignedBlock const* prev_signed_block;
    if (m_pimpl->sync_blocks.empty())
        prev_signed_block = &m_pimpl->m_blockchain.at(block_number - 1);
    else
        prev_signed_block = &(*m_pimpl->sync_blocks.rbegin());

    auto header_it = m_pimpl->sync_headers.rbegin() + length;

    if (header_it->prev_hash != meshpp::hash(prev_signed_block->block_details.to_string()))
        throw wrong_data_exception("blockchain response. previous hash!");

    ++header_it;
    for (auto& block_item : response.signed_blocks)
    {
        Block& block = block_item.block_details;
        string str = block.to_string();

        // verify block signature
        if (!meshpp::verify_signature(meshpp::public_key(block_item.authority), str, block_item.signature))
            throw wrong_data_exception("blockchain response. block signature!");

        if (header_it != m_pimpl->sync_headers.rend())
        {
            if (*(header_it - 1) != block.header || header_it->prev_hash != meshpp::hash(str))
                throw wrong_data_exception("blockchain response. block header!");

            ++header_it;
        }

        // verify block transactions
        for (auto tr_it = block.signed_transactions.begin(); tr_it != block.signed_transactions.end(); ++tr_it)
        {
            if (!meshpp::verify_signature(meshpp::public_key(tr_it->authority),
                                          tr_it->transaction_details.to_string(),
                                          tr_it->signature))
                throw wrong_data_exception("blockchain response. transaction signature!");

            action_validate(*m_pimpl.get(), *tr_it);

            system_clock::time_point creation = system_clock::from_time_t(tr_it->transaction_details.creation.tm);
            system_clock::time_point expiry = system_clock::from_time_t(tr_it->transaction_details.expiry.tm);

            if (expiry - creation > chrono::hours(TRANSACTION_MAX_LIFETIME_HOURS))
                throw wrong_data_exception("blockchain response. too long lifetime for transaction");

            if (creation - chrono::seconds(NODES_TIME_SHIFT) > system_clock::from_time_t(block.header.time_signed.tm))
                throw wrong_data_exception("blockchain response. transaction from the future!");

            if (expiry < system_clock::from_time_t(block.header.time_signed.tm))
                throw wrong_data_exception("blockchain response. expired transaction!");
        }

        // store blocks for future use
        m_pimpl->sync_blocks.push_back(std::move(block_item));
    }

    // request new chain if needed
    if (m_pimpl->sync_blocks.size() < BLOCK_INSERT_LENGTH &&
        m_pimpl->sync_blocks.size() < m_pimpl->sync_headers.size())
    {
        BlockchainRequest blockchain_request;
        blockchain_request.blocks_from = (header_it - 1)->block_number;
        blockchain_request.blocks_to = m_pimpl->sync_headers.begin()->block_number;

        sk.send(peerid, blockchain_request);
        m_pimpl->update_sync_time();
        m_pimpl->store_request(peerid, blockchain_request);

        return; // will wait new chain
    }

    //3. all needed blocks received, start to check
    unordered_map<string, system_clock::time_point> transaction_cache_backup = m_pimpl->m_transaction_cache;

    auto now = system_clock::now();
    beltpp::on_failure guard([&m_pimpl, &transaction_cache_backup]
    {
        m_pimpl->discard();
        m_pimpl->clear_sync_state(m_pimpl->sync_peerid);
        m_pimpl->m_transaction_cache = std::move(transaction_cache_backup);
    });

    uint64_t lcb_number = m_pimpl->sync_headers.rbegin()->block_number - 1;
    vector<SignedTransaction> reverted_transactions;
    bool clear_pool = m_pimpl->sync_blocks.size() < m_pimpl->sync_headers.size();

    //  collect transactions to be reverted from blockchain
    //
    size_t blockchain_length = m_pimpl->m_blockchain.length();

    for (size_t index = lcb_number + 1; index < blockchain_length; ++index)
    {
        SignedBlock const& signed_block = m_pimpl->m_blockchain.at(index);

        reverted_transactions.insert(reverted_transactions.end(),
                                     signed_block.block_details.signed_transactions.begin(),
                                     signed_block.block_details.signed_transactions.end());
    }

    vector<SignedTransaction> pool_transactions;
    //  collect transactions to be reverted from pool
    //  revert transactions from pool
    revert_pool(system_clock::to_time_t(now - chrono::seconds(NODES_TIME_SHIFT)), *m_pimpl.get(), pool_transactions);

    //  revert blocks
    //  calculate back to get state at LCB point
    for (size_t index = blockchain_length - 1;
         index < blockchain_length && index > lcb_number;
         --index)
    {
        SignedBlock const& signed_block = m_pimpl->m_blockchain.at(index);
        m_pimpl->m_blockchain.remove_last_block();
        m_pimpl->m_action_log.revert();

        Block const& block = signed_block.block_details;

        // decrease all reward amounts from balances and revert reward
        for (auto it = block.rewards.crbegin(); it != block.rewards.crend(); ++it)
            m_pimpl->m_state.decrease_balance(it->to, it->amount);

        // calculate back transactions
        for (auto it = block.signed_transactions.crbegin(); it != block.signed_transactions.crend(); ++it)
        {
            revert_transaction(*it, *m_pimpl.get(), signed_block.authority);

            string key = meshpp::hash(it->to_string());
            m_pimpl->m_transaction_cache.erase(key);
        }
    }
    //  update the variable, just in case it will be needed down the code
    blockchain_length = m_pimpl->m_blockchain.length();

    unordered_set<string> set_tr_hashes_to_remove;

    // verify new received blocks
    BlockHeader const& prev_header = m_pimpl->m_blockchain.header_at(lcb_number);
    uint64_t c_const = prev_header.c_const;

    for (auto const& signed_block : m_pimpl->sync_blocks)
    {
        Block const& block = signed_block.block_details;

        // verify consensus_delta
        Coin amount = m_pimpl->m_state.get_balance(signed_block.authority);
        uint64_t delta = m_pimpl->calc_delta(signed_block.authority, amount.whole, block.header.prev_hash, c_const);

        if (delta != block.header.delta)
            throw wrong_data_exception("blockchain response. consensus delta!");

        // verify miner balance at mining time
        if (coin(amount) < MINE_AMOUNT_THRESHOLD)
            throw wrong_data_exception("blockchain response. miner balance!");

        // verify block transactions
        for (auto const& tr_item : block.signed_transactions)
        {
            string key = meshpp::hash(tr_item.to_string());

            set_tr_hashes_to_remove.insert(key);

            if (m_pimpl->m_transaction_cache.find(key) != m_pimpl->m_transaction_cache.end())
                throw wrong_data_exception("blockchain response. transaction double use!");

            m_pimpl->m_transaction_cache[key] = system_clock::from_time_t(tr_item.transaction_details.creation.tm);

            if (!apply_transaction(tr_item, *m_pimpl.get(), signed_block.authority))
                throw wrong_data_exception("blockchain response. sender balance!");
        }

        // verify block rewards
        if (check_rewards(block, signed_block.authority, *m_pimpl.get()))
            throw wrong_data_exception("blockchain response. block rewards!");

        // increase all reward amounts to balances
        for (auto const& reward_item : block.rewards)
            m_pimpl->m_state.increase_balance(reward_item.to, reward_item.amount);

        // Insert to blockchain
        m_pimpl->m_blockchain.insert(signed_block);
        m_pimpl->m_action_log.log_block(signed_block);

        c_const = block.header.c_const;
    }

    if (false == clear_pool)
        reverted_transactions.insert(reverted_transactions.end(),
                                     pool_transactions.begin(),
                                     pool_transactions.end());

    // apply back the rest of the transaction pool
    //
    for (auto const& signed_transaction : reverted_transactions)
    {
        string key = meshpp::hash(signed_transaction.to_string());
        if (now - chrono::seconds(NODES_TIME_SHIFT) <=
            system_clock::from_time_t(signed_transaction.transaction_details.expiry.tm) &&
            0 == set_tr_hashes_to_remove.count(key))
        {
            if (apply_transaction(signed_transaction, *m_pimpl.get()))
            {
                m_pimpl->m_action_log.log_transaction(signed_transaction);
                m_pimpl->m_transaction_pool.push_back(signed_transaction);
                m_pimpl->m_transaction_cache[key] =
                        system_clock::from_time_t(signed_transaction.transaction_details.creation.tm);
            }
        }
    }

    m_pimpl->save(guard);
    if (false == m_pimpl->sync_blocks.empty())
        //  please pay special attention to this change during code review
        m_pimpl->calc_sync_info(m_pimpl->sync_blocks.back().block_details);

    // request new chain if the process was stopped
    // by BLOCK_INSERT_LENGTH restriction
    length = m_pimpl->sync_blocks.size();
    if (length < m_pimpl->sync_headers.size())
    {
        // clear already inserted blocks and headers
        m_pimpl->sync_blocks.clear();
        for (size_t i = 0; i < length; ++i)
            m_pimpl->sync_headers.pop_back();

        BlockchainRequest blockchain_request;
        blockchain_request.blocks_from = m_pimpl->sync_headers.rbegin()->block_number;
        blockchain_request.blocks_to = m_pimpl->sync_headers.begin()->block_number;

        sk.send(peerid, blockchain_request);
        m_pimpl->update_sync_time();
        m_pimpl->store_request(peerid, blockchain_request);
    }
    else
    {
        m_pimpl->clear_sync_state(m_pimpl->sync_peerid);

        if (m_pimpl->m_node_type == NodeType::channel)
            broadcast_storage_info(*m_pimpl.get());

        if (m_pimpl->m_node_type == NodeType::storage && !m_pimpl->m_slave_peer.empty())
        {
            StatInfo stat_info;
            TaskRequest task_request;
            task_request.task_id = ++m_pimpl->m_slave_taskid;
            ::detail::assign_packet(task_request.package, stat_info);
            task_request.time_signed.tm = system_clock::to_time_t(now);
            meshpp::signature signed_msg = m_pimpl->m_pv_key.sign(std::to_string(task_request.task_id) + 
                                                                  meshpp::hash(stat_info.to_string()) +
                                                                  std::to_string(task_request.time_signed.tm));
            task_request.signature = signed_msg.base58;

            // send task to slave
            m_pimpl->m_ptr_rpc_socket->send(m_pimpl->m_slave_peer, task_request);

            beltpp::packet task_packet;
            task_packet.set(stat_info);

            m_pimpl->m_slave_tasks.add(task_request.task_id, task_packet);
        }
    }
}

void broadcast_node_type(std::unique_ptr<publiqpp::detail::node_internals>& m_pimpl)
{
    //  don't do anything if there are no peers
    if (m_pimpl->m_p2p_peers.empty())
        return;

    NodeType my_state_type;
    if (m_pimpl->m_state.get_role(m_pimpl->m_pb_key.to_string(), my_state_type))
    {
        assert(my_state_type == m_pimpl->m_node_type);
        return; //  if already stored, do nothing
    }

    if (m_pimpl->m_node_type == BlockchainMessage::NodeType::blockchain)
        return; //  no need to explicitly broadcast in this case

    Role role;
    role.node_address = m_pimpl->m_pb_key.to_string();
    role.node_type = m_pimpl->m_node_type;

    Transaction transaction;
    transaction.action = std::move(role);
    transaction.creation.tm = system_clock::to_time_t(system_clock::now());
    transaction.expiry.tm = system_clock::to_time_t(system_clock::now() + chrono::hours(TRANSACTION_MAX_LIFETIME_HOURS));

    SignedTransaction signed_transaction;
    signed_transaction.authority = m_pimpl->m_pb_key.to_string();
    signed_transaction.signature = m_pimpl->m_pv_key.sign(transaction.to_string()).base58;
    signed_transaction.transaction_details = transaction;

    if (action_process_on_chain(signed_transaction, *m_pimpl.get()))
    {
        Broadcast broadcast;
        broadcast.echoes = 2;
        broadcast.package = signed_transaction;

        broadcast_message(std::move(broadcast),
                          m_pimpl->m_ptr_p2p_socket->name(),
                          m_pimpl->m_ptr_p2p_socket->name(),
                          true, // broadcast to all peers
                          nullptr, // log disabled
                          m_pimpl->m_p2p_peers,
                          m_pimpl->m_ptr_p2p_socket.get());
    }
}

void broadcast_address_info(std::unique_ptr<publiqpp::detail::node_internals>& m_pimpl)
{
    NodeType my_state_type;
    if (false == m_pimpl->m_state.get_role(m_pimpl->m_pb_key.to_string(), my_state_type))
        return;
    if (m_pimpl->m_public_address.local.empty() &&
        m_pimpl->m_public_address.remote.empty())
        return;

    AddressInfo address_info;
    address_info.node_address = m_pimpl->m_pb_key.to_string();
    beltpp::assign(address_info.ip_address, m_pimpl->m_public_address);

    Transaction transaction;
    transaction.action = address_info;
    transaction.creation.tm = system_clock::to_time_t(system_clock::now());
    transaction.expiry.tm = system_clock::to_time_t(system_clock::now() + chrono::hours(24));

    SignedTransaction signed_transaction;
    signed_transaction.transaction_details = transaction;
    signed_transaction.authority = m_pimpl->m_pb_key.to_string();
    signed_transaction.signature = m_pimpl->m_pv_key.sign(transaction.to_string()).base58;

    Broadcast broadcast;
    broadcast.echoes = 2;
    broadcast.package = signed_transaction;

    broadcast_message(std::move(broadcast),
        m_pimpl->m_ptr_p2p_socket->name(),
        m_pimpl->m_ptr_p2p_socket->name(),
        true, // broadcast to all peers
        nullptr, // log disabled
        m_pimpl->m_p2p_peers,
        m_pimpl->m_ptr_p2p_socket.get());
}

void broadcast_storage_stat(StatInfo& stat_info, 
                            std::unique_ptr<publiqpp::detail::node_internals>& m_pimpl)
{
    unordered_set<string> channels_set;
    vector<string> channels = m_pimpl->m_state.get_nodes_by_type(NodeType::channel);

    if (channels.empty()) return;

    for (auto& channel_node_address : channels)
        channels_set.insert(channel_node_address);

    for (auto it = stat_info.items.begin(); it != stat_info.items.end();)
    {
        if (channels_set.count(it->node_address))
            ++it;
        else
            it = stat_info.items.erase(it);
    }

    if (stat_info.items.empty()) return;

    uint64_t block_number = m_pimpl->m_blockchain.length() - 1;
    SignedBlock const& signed_block = m_pimpl->m_blockchain.at(block_number);

    stat_info.hash = meshpp::hash(signed_block.block_details.to_string());
    stat_info.reporter_address = m_pimpl->m_pb_key.to_string();

    Transaction transaction;
    transaction.action = stat_info;
    transaction.creation.tm = system_clock::to_time_t(system_clock::now());
    transaction.expiry.tm = system_clock::to_time_t(system_clock::now() + chrono::minutes(10));

    SignedTransaction signed_transaction;
    signed_transaction.authority = m_pimpl->m_pb_key.to_string();
    signed_transaction.transaction_details = transaction;
    signed_transaction.signature = m_pimpl->m_pv_key.sign(transaction.to_string()).base58;

    Broadcast broadcast;
    broadcast.echoes = 2;
    broadcast.package = signed_transaction;

    broadcast_message(std::move(broadcast),
        m_pimpl->m_ptr_p2p_socket->name(),
        m_pimpl->m_ptr_p2p_socket->name(),
        true,
        nullptr,
        m_pimpl->m_p2p_peers,
        m_pimpl->m_ptr_p2p_socket.get());
}

bool process_address_info(BlockchainMessage::SignedTransaction const& signed_transaction,
                          BlockchainMessage::AddressInfo const& address_info,
                          std::unique_ptr<publiqpp::detail::node_internals>& pimpl)
{
    beltpp::ip_address beltpp_ip_address;
    beltpp::assign(beltpp_ip_address, address_info.ip_address);
    if (beltpp_ip_address.remote.empty() &&
        beltpp_ip_address.local.empty())
        return false;
    // Check data and authority
    if (signed_transaction.authority != address_info.node_address)
        throw authority_exception(signed_transaction.authority, address_info.node_address);

    // Check pool and cache
    string tr_hash = meshpp::hash(signed_transaction.to_string());

    if (pimpl->m_transaction_cache.count(tr_hash))
        return false;

    auto transaction_cache_backup = pimpl->m_transaction_cache;

    beltpp::on_failure guard([&pimpl, &transaction_cache_backup]
    {
        //pimpl->discard(); // not working with other state
        pimpl->m_transaction_cache = std::move(transaction_cache_backup);
    });

    //  this is not added to pool, because we don't store it in blockchain
        //  pimpl->m_transaction_pool.push_back(signed_transaction);
    pimpl->m_transaction_cache[tr_hash] =
            system_clock::from_time_t(signed_transaction.transaction_details.creation.tm);

    return true;
}
}// end of namespace publiqpp
