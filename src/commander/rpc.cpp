#include "rpc.hpp"
#include "http.hpp"
#include "daemon_rpc.hpp"

#include <belt.pp/scope_helper.hpp>

#include <mesh.pp/settings.hpp>
#include <mesh.pp/cryptoutility.hpp>

#include <publiq.pp/coin.hpp>

#include <memory>
#include <chrono>
#include <unordered_set>
#include <set>

using std::unordered_set;
using std::set;
using std::unique_ptr;
namespace chrono = std::chrono;
using beltpp::packet;
using peer_id = beltpp::socket::peer_id;

using namespace CommanderMessage;

using sf = beltpp::socket_family_t<&commander::http::message_list_load<&CommanderMessage::message_list_load>>;

static inline
beltpp::void_unique_ptr get_putl()
{
    beltpp::message_loader_utility utl;
    CommanderMessage::detail::extension_helper(utl);

    auto ptr_utl =
        beltpp::new_void_unique_ptr<beltpp::message_loader_utility>(std::move(utl));

    return ptr_utl;
}

rpc::rpc(beltpp::ip_address const& rpc_address,
         beltpp::ip_address const& connect_to_address)
    : eh()
    , rpc_socket(beltpp::getsocket<sf>(eh))
    , head_block_index(meshpp::data_file_path("head_block_index.txt"))
    , accounts("accounts", meshpp::data_directory_path("accounts"), 100, get_putl())
    , connect_to_address(connect_to_address)
{
    eh.set_timer(chrono::seconds(10));
    eh.add(rpc_socket);

    rpc_socket.listen(rpc_address);
}

void process_rewards(uint64_t head_block_index,
                     uint64_t block_index,
                     daemon_rpc::LogIndexLoader& reward_index,
                     daemon_rpc::RewardLogLoader& reward_logs,
                     AccountHistory& result)
{
    auto const& value = reward_index.as_const().at(std::to_string(block_index));

    for (uint64_t index = value.first;
         index != value.first + value.second;
         ++index)
    {
        auto& reward_log = reward_logs.at(index);
        AccountHistoryItem item;
        item.block_index = block_index;
        item.confirmations = head_block_index - block_index + 1;
        item.item_type = AccountHistoryItemType::rewarded;
        item.timestamp.tm = 0;  //  todo
        publiqpp::coin(reward_log.amount).to_Coin(item.amount);

        item.details = AccountHistoryRewarded();

        result.log.push_back(std::move(item));
    }
}

void process_transactions(uint64_t head_block_index,
                          uint64_t block_index,
                          string const& address,
                          daemon_rpc::LogIndexLoader& transaction_index,
                          daemon_rpc::TransactionLogLoader& transaction_logs,
                          AccountHistory& result)
{
    auto const& value = transaction_index.as_const().at(std::to_string(block_index));

    for (uint64_t index = value.first;
         index != value.first + value.second;
         ++index)
    {
        auto& transaction_log = transaction_logs.at(index);
        BlockchainMessage::Transfer tf;
        std::move(transaction_log.action).get(tf);

        if (tf.to == address)
        {
            AccountHistoryItem item;
            AccountHistoryReceived details;
            item.block_index = block_index;
            item.confirmations = head_block_index - block_index + 1;
            item.item_type = AccountHistoryItemType::received;
            item.timestamp.tm = transaction_log.time_signed.tm;
            publiqpp::coin(tf.amount).to_Coin(item.amount);

            details.from = tf.from;
            details.message = tf.message;
            details.transaction_hash = transaction_log.transaction_hash;
            item.details = std::move(details);

            result.log.push_back(std::move(item));
        }
        if (tf.from == address)
        {
            {
                AccountHistoryItem item;
                AccountHistorySent details;
                item.block_index = block_index;
                item.confirmations = head_block_index - block_index + 1;
                item.item_type = AccountHistoryItemType::sent;
                item.timestamp.tm = transaction_log.time_signed.tm;
                publiqpp::coin(tf.amount).to_Coin(item.amount);

                details.to = tf.to;
                details.message = tf.message;
                details.transaction_hash = transaction_log.transaction_hash;
                item.details = std::move(details);

                result.log.push_back(std::move(item));
            }
            if (transaction_log.fee != BlockchainMessage::Coin())
            {
                AccountHistoryItem item;
                AccountHistorySentFee details;
                item.block_index = block_index;
                item.confirmations = head_block_index - block_index + 1;
                item.item_type = AccountHistoryItemType::sent_fee;
                item.timestamp.tm = transaction_log.time_signed.tm;
                publiqpp::coin(transaction_log.fee).to_Coin(item.amount);

                details.to = string(); //  todo
                details.transaction_hash = transaction_log.transaction_hash;
                item.details = std::move(details);

                result.log.push_back(std::move(item));
            }
        }
        if (tf.to != address &&
            tf.from != address &&
            transaction_log.fee != BlockchainMessage::Coin())
        {   //  todo fix by checking against actual block authority
            AccountHistoryItem item;
            AccountHistoryReceivedFee details;
            item.block_index = block_index;
            item.confirmations = head_block_index - block_index + 1;
            item.item_type = AccountHistoryItemType::received_fee;
            item.timestamp.tm = transaction_log.time_signed.tm;
            publiqpp::coin(transaction_log.fee).to_Coin(item.amount);

            details.from = tf.from;
            details.transaction_hash = transaction_log.transaction_hash;
            item.details = std::move(details);

            result.log.push_back(std::move(item));
        }
    }
}

AccountHistory get_history(uint64_t head_block_index,
                           uint64_t block_start,
                           uint64_t block_count,
                           string const& address)
{
    auto reward_index = daemon_rpc::get_reward_log_index(address);
    auto reward_logs = daemon_rpc::get_reward_log(address);
    auto transaction_index = daemon_rpc::get_transaction_log_index(address);
    auto transaction_logs = daemon_rpc::get_transaction_log(address);

    set<uint64_t> set_tx;
    for (auto const& str_block : transaction_index.keys())
    {
        size_t pos;
        uint64_t block_index = beltpp::stoui64(str_block, pos);
        if (block_index >= block_start &&
            (
                block_index < block_start + block_count ||
                block_start > block_start + block_count
            ))
            set_tx.insert(block_index);
    }
    set<uint64_t> set_rw;
    for (auto const& str_block : reward_index.keys())
    {
        size_t pos;
        uint64_t block_index = beltpp::stoui64(str_block, pos);
        if (block_index >= block_start &&
            (
                block_index < block_start + block_count ||
                block_start > block_start + block_count
            ))
            set_rw.insert(block_index);
    }

    uint64_t rw_min = 0, rw_max = 0;
    uint64_t tx_min = 0, tx_max = 0;

    uint64_t rw1_min = 0, rw1_max = 0;
    uint64_t tx1_min = 0, tx1_max = 0;
    uint64_t rwtx_min = 0, rwtx_max = 0;
    uint64_t rw2_min = 0, rw2_max = 0;
    uint64_t tx2_min = 0, tx2_max = 0;

    if (false == set_rw.empty())
    {
        rw_min = *set_rw.cbegin();
        rw_max = *set_rw.crbegin() + 1;
    }
    if (false == set_tx.empty())
    {
        tx_min = *set_tx.cbegin();
        tx_max = *set_tx.crbegin() + 1;
    }

    range_break(rw_min, rw_max,
                tx_min, tx_max,
                rw1_min, rw1_max,
                tx1_min, tx1_max,
                rwtx_min, rwtx_max,
                rw2_min, rw2_max,
                tx2_min, tx2_max);

    AccountHistory result;


    for (uint64_t i = tx1_min; i != tx1_max; ++i)
    {
        if (set_tx.find(i) != set_tx.end())
        {
            process_transactions(head_block_index,
                                 i,
                                 address,
                                 transaction_index,
                                 transaction_logs,
                                 result);
        }
    }

    for (uint64_t i = rw1_min; i != rw1_max; ++i)
    {
        if (set_rw.find(i) != set_rw.end())
        {
            process_rewards(head_block_index,
                            i,
                            reward_index,
                            reward_logs,
                            result);
        }
    }

    for (uint64_t i = rwtx_min; i != rwtx_max; ++i)
    {
        if (set_tx.find(i) != set_tx.end())
        {
            process_transactions(head_block_index,
                                 i,
                                 address,
                                 transaction_index,
                                 transaction_logs,
                                 result);
        }

        if (set_rw.find(i) != set_rw.end())
        {
            process_rewards(head_block_index,
                            i,
                            reward_index,
                            reward_logs,
                            result);
        }
    }

    for (uint64_t i = tx2_min; i != tx2_max; ++i)
    {
        if (set_tx.find(i) != set_tx.end())
        {
            process_transactions(head_block_index,
                                 i,
                                 address,
                                 transaction_index,
                                 transaction_logs,
                                 result);
        }
    }

    for (uint64_t i = rw2_min; i != rw2_max; ++i)
    {
        if (set_rw.find(i) != set_rw.end())
        {
            process_rewards(head_block_index,
                            i,
                            reward_index,
                            reward_logs,
                            result);
        }
    }

    reward_index.discard();
    reward_logs.discard();
    transaction_index.discard();
    transaction_logs.discard();

    return result;
}

AccountResponse AccountResponseFromRawAccount(size_t head_index,
                                              Account const& account)
{
    auto history = get_history(head_index,
                               head_index,
                               2,
                               account.address);

    publiqpp::coin unconfirmed_sent;
    publiqpp::coin unconfirmed_received;

    for (auto const& item : history.log)
    {
        if (item.confirmations == 0)
        {
            if (item.item_type == AccountHistoryItemType::received ||
                item.item_type == AccountHistoryItemType::received_fee ||
                item.item_type == AccountHistoryItemType::rewarded)
            {
                publiqpp::coin temp(item.amount.whole, item.amount.fraction);
                unconfirmed_received += temp;
            }
            else
            {
                publiqpp::coin temp(item.amount.whole, item.amount.fraction);
                unconfirmed_sent += temp;
            }
        }
    }

    AccountResponse response;
    response.address = account.address;
    response.balance = account.balance;

    publiqpp::coin confirmed_balance(account.balance.whole, account.balance.fraction);
    confirmed_balance += unconfirmed_sent;
    confirmed_balance -= unconfirmed_received;

    confirmed_balance.to_Coin(response.confirmed_balance);
    unconfirmed_received.to_Coin(response.unconfirmed_received);
    unconfirmed_sent.to_Coin(response.unconfirmed_sent);

    return response;
}

void import_account_if_needed(string const& address,
                              rpc& rpc_server,
                              beltpp::ip_address const& connect_to_address)
{
    Account account;
    account.address = address;

    meshpp::public_key pb(account.address);

    if (false == rpc_server.accounts.contains(address))
    {
        daemon_rpc dm;
        dm.open(connect_to_address);
        beltpp::finally finally_close([&dm]{ dm.close(); });

        unordered_set<string> set_accounts = {address};

        dm.sync(rpc_server, set_accounts, true);
    }
    if (false == rpc_server.accounts.contains(address))
    {
        beltpp::on_failure guard([&rpc_server](){rpc_server.accounts.discard();});
        rpc_server.accounts.insert(address, account);
        rpc_server.accounts.save();

        guard.dismiss();
        rpc_server.accounts.commit();
    }
}

string send(Send const& send,
            rpc& rpc_server,
            beltpp::ip_address const& connect_to_address)
{
    daemon_rpc dm;
    dm.open(connect_to_address);
    beltpp::finally finally_close([&dm]{ dm.close(); });

    return dm.send(send, rpc_server);
}

void rpc::run()
{
    unordered_set<beltpp::ievent_item const*> wait_sockets;
    auto wait_result = eh.wait(wait_sockets);
    B_UNUSED(wait_sockets);

    if (wait_result == beltpp::event_handler::event)
    {
        beltpp::isocket::peer_id peerid;
        beltpp::socket::packets received_packets;

        received_packets = rpc_socket.receive(peerid);

        for (auto& received_packet : received_packets)
        {
        try
        {
            packet& ref_packet = received_packet;

            switch (ref_packet.type())
            {
            case AccountsRequest::rtt:
            {
                AccountsResponse msg;

                for (auto const& account : accounts.keys())
                {
                    auto account_raw = accounts.as_const().at(account);

                    msg.accounts.push_back(AccountResponseFromRawAccount(head_block_index.as_const()->value,
                                                                         account_raw));
                }

                rpc_socket.send(peerid, msg);
                break;
            }
            case ImportAccount::rtt:
            {
                ImportAccount msg;
                std::move(ref_packet).get(msg);

                import_account_if_needed(msg.address, *this, connect_to_address);

                rpc_socket.send(peerid, Done());
                break;
            }
            case AccountHistoryRequest::rtt:
            {
                AccountHistoryRequest msg;
                std::move(ref_packet).get(msg);

                meshpp::public_key pb(msg.address);
                AccountHistory response;

                if (accounts.contains(msg.address))
                {
                    response = get_history(head_block_index.as_const()->value,
                                           msg.start_block_index,
                                           msg.max_block_count,
                                           msg.address);
                }

                rpc_socket.send(peerid, response);
                break;
            }
            case HeadBlockRequest::rtt:
            {
                NumberValue response;
                response.value = head_block_index.as_const()->value;

                rpc_socket.send(peerid, response);
                break;
            }
            case AccountRequest::rtt:
            {
                AccountRequest msg;
                std::move(ref_packet).get(msg);

                meshpp::public_key temp(msg.address);

                auto const& account_raw = accounts.as_const().at(msg.address);

                rpc_socket.send(peerid, AccountResponseFromRawAccount(head_block_index.as_const()->value,
                                                                      account_raw));
                break;
            }
            case Send::rtt:
            {
                Send msg;
                std::move(ref_packet).get(msg);

                meshpp::private_key pv(msg.private_key);

                import_account_if_needed(pv.get_public_key().to_string(),
                                         *this,
                                         connect_to_address);

                StringValue response;
                response.value = send(msg, *this, connect_to_address);
                rpc_socket.send(peerid, response);

                break;
            }
            }
        }
        catch(std::exception const& ex)
        {
            Failed msg;
            msg.message = ex.what();
            rpc_socket.send(peerid, msg);
            throw;
        }
        catch(...)
        {
            Failed msg;
            msg.message = "unknown error";
            rpc_socket.send(peerid, msg);
            throw;
        }
        }
    }
    else
    {
        daemon_rpc dm;
        dm.open(connect_to_address);
        beltpp::finally finally_close([&dm]{ dm.close(); });

        dm.sync(*this, accounts.keys(), false);
    }
}
