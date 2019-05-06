#include "node.hpp"
#include "common.hpp"
#include "exception.hpp"

#include "communication_rpc.hpp"
#include "communication_p2p.hpp"
#include "transaction_handler.hpp"

#include "open_container_packet.hpp"
#include "sessions.hpp"

#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <exception>

using namespace BlockchainMessage;

using beltpp::ip_address;
using beltpp::socket;
using beltpp::packet;
using peer_id = socket::peer_id;

namespace chrono = std::chrono;
using chrono::system_clock;

using std::pair;
using std::string;
using std::vector;
using std::unordered_set;
using std::unique_ptr;

namespace publiqpp
{
//  free functions
void sync_worker(detail::node_internals& impl);
/*
 * node
 */
node::node(string const& genesis_signed_block,
           ip_address const & public_address,
           ip_address const& rpc_bind_to_address,
           ip_address const& p2p_bind_to_address,
           std::vector<ip_address> const& p2p_connect_to_addresses,
           boost::filesystem::path const& fs_blockchain,
           boost::filesystem::path const& fs_action_log,
           boost::filesystem::path const& fs_transaction_pool,
           boost::filesystem::path const& fs_state,
           boost::filesystem::path const& fs_documents,
           beltpp::ilog* plogger_p2p,
           beltpp::ilog* plogger_node,
           meshpp::private_key const& pv_key,
           NodeType& n_type,
           bool log_enabled,
           bool transfer_only,
           bool testnet)
    : m_pimpl(new detail::node_internals(genesis_signed_block,
                                         public_address,
                                         rpc_bind_to_address,
                                         p2p_bind_to_address,
                                         p2p_connect_to_addresses,
                                         fs_blockchain,
                                         fs_action_log,
                                         fs_transaction_pool,
                                         fs_state,
                                         fs_documents,
                                         plogger_p2p,
                                         plogger_node,
                                         pv_key,
                                         n_type,
                                         log_enabled,
                                         transfer_only,
                                         testnet))
{}

node::node(node&&) noexcept = default;

node::~node() = default;

void node::wake()
{
    m_pimpl->m_ptr_eh->wake();
}

string node::name() const
{
    return m_pimpl->m_ptr_p2p_socket->name();
}

bool node::run()
{
    bool code = true;

    unordered_set<beltpp::ievent_item const*> wait_sockets;

    auto wait_result = m_pimpl->m_ptr_eh->wait(wait_sockets);

    enum class interface_type {p2p, rpc};

    if (wait_result & beltpp::event_handler::event)
    {
        for (auto& pevent_item : wait_sockets)
        {
            interface_type it = interface_type::rpc;
            if (pevent_item == &m_pimpl->m_ptr_p2p_socket->worker())
                it = interface_type::p2p;

            auto str_receive = [it]
            {
                if (it == interface_type::p2p)
                    return "p2p_sk.receive";
                return "rpc_sk.receive";
            };
            str_receive();

            beltpp::socket::peer_id peerid;

            beltpp::isocket* psk = nullptr;
            if (pevent_item == &m_pimpl->m_ptr_p2p_socket->worker())
                psk = m_pimpl->m_ptr_p2p_socket.get();
            else if (pevent_item == m_pimpl->m_ptr_rpc_socket.get())
                psk = m_pimpl->m_ptr_rpc_socket.get();

            if (nullptr == psk)
                throw std::logic_error("event handler behavior");

            beltpp::socket::packets received_packets;
            if (psk != nullptr)
                received_packets = psk->receive(peerid);

            for (auto& received_packet : received_packets)
            {
            try
            {
                if (m_pimpl->m_nodeid_sessions.process(peerid, std::move(received_packet)))
                    continue;
                if (m_pimpl->m_sync_sessions.process(peerid, std::move(received_packet)))
                    continue;

                vector<packet*> composition;

                open_container_packet<Broadcast, SignedTransaction> broadcast_signed_transaction;
                open_container_packet<Broadcast> broadcast_anything;
                bool is_container = broadcast_signed_transaction.open(received_packet, composition, *m_pimpl.get()) ||
                                    broadcast_anything.open(received_packet, composition, *m_pimpl.get());

                if (is_container == false)
                {
                    composition.clear();
                    composition.push_back(&received_packet);
                }

                packet& ref_packet = *composition.back();

                switch (ref_packet.type())
                {
                case beltpp::isocket_join::rtt:
                {
                    if (it == interface_type::p2p)
                        m_pimpl->writeln_node("joined: " + detail::peer_short_names(peerid));

                    if (psk == m_pimpl->m_ptr_p2p_socket.get())
                    {
                        beltpp::on_failure guard(
                            [&peerid, &psk] { psk->send(peerid, beltpp::packet(beltpp::isocket_drop())); });

                        m_pimpl->add_peer(peerid);

                        beltpp::ip_address external_address =
                                m_pimpl->m_ptr_p2p_socket->external_address();
                        assert(external_address.local.empty() == false);
                        assert(external_address.remote.empty());
                        external_address.local.port =
                                m_pimpl->m_rpc_bind_to_address.local.port;

                        guard.dismiss();
                    }

                    break;
                }
                case beltpp::isocket_drop::rtt:
                {
                    if (it == interface_type::p2p)
                        m_pimpl->writeln_node("dropped: " + detail::peer_short_names(peerid));

                    if (it == interface_type::p2p)
                        m_pimpl->remove_peer(peerid);

                    break;
                }
                case beltpp::isocket_protocol_error::rtt:
                {
                    beltpp::isocket_protocol_error msg;
                    ref_packet.get(msg);
                    m_pimpl->writeln_node("protocol error: " + detail::peer_short_names(peerid));
                    m_pimpl->writeln_node(msg.buffer);
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));

                    if (it == interface_type::p2p)
                        m_pimpl->remove_peer(peerid);
                    
                    break;
                }
                case beltpp::isocket_open_refused::rtt:
                {
                    beltpp::isocket_open_refused msg;
                    ref_packet.get(msg);
                    m_pimpl->writeln_node_warning(msg.reason + ", " + peerid);
                    break;
                }
                case beltpp::isocket_open_error::rtt:
                {
                    beltpp::isocket_open_error msg;
                    ref_packet.get(msg);
                    m_pimpl->writeln_node_warning(msg.reason + ", " + peerid);
                    break;
                }
                case Transfer::rtt:
                case File::rtt:
                case ContentUnit::rtt:
                case Content::rtt:
                case Role::rtt:
                case StorageUpdate::rtt:
                case ServiceStatistics::rtt:
                {
                    if (broadcast_signed_transaction.items.empty())
                        throw wrong_data_exception("will process only \"broadcast signed transaction\"");

                    if (m_pimpl->m_transfer_only && ref_packet.type() != Transfer::rtt)
                        throw std::runtime_error("this is coin only blockchain");

                    Broadcast* p_broadcast = nullptr;
                    SignedTransaction* p_signed_tx = nullptr;

                    broadcast_signed_transaction.items[0]->get(p_broadcast);
                    broadcast_signed_transaction.items[1]->get(p_signed_tx);

                    assert(p_broadcast);
                    assert(p_signed_tx);

                    Broadcast& broadcast = *p_broadcast;
                    SignedTransaction& signed_tx = *p_signed_tx;

                    if (action_process_on_chain(signed_tx, *m_pimpl.get()))
                    {
                        broadcast_message(std::move(broadcast),
                                          m_pimpl->m_ptr_p2p_socket->name(),
                                          peerid,
                                          it == interface_type::rpc,
                                          nullptr,
                                          m_pimpl->m_p2p_peers,
                                          m_pimpl->m_ptr_p2p_socket.get());
                    }
                
                    if (it == interface_type::rpc)
                        psk->send(peerid, beltpp::packet(Done()));

                    break;
                }
                case AddressInfo::rtt:
                {
                    if (broadcast_signed_transaction.items.empty())
                        throw wrong_data_exception("will process only \"broadcast signed transaction\"");

                    if (it != interface_type::p2p)
                        throw wrong_request_exception("AddressInfo received through rpc!");

                    Broadcast* p_broadcast = nullptr;
                    SignedTransaction* p_signed_tx = nullptr;
                    AddressInfo* p_address_info = nullptr;

                    broadcast_signed_transaction.items[0]->get(p_broadcast);
                    broadcast_signed_transaction.items[1]->get(p_signed_tx);
                    ref_packet.get(p_address_info);

                    assert(p_broadcast);
                    assert(p_signed_tx);
                    assert(p_address_info);

                    Broadcast& broadcast = *p_broadcast;
                    SignedTransaction& signed_tx = *p_signed_tx;
                    AddressInfo& address_info = *p_address_info;

                    if (process_address_info(signed_tx, address_info, m_pimpl))
                    {
                        beltpp::ip_address beltpp_ip_address;
                        beltpp::assign(beltpp_ip_address, address_info.ip_address);

                        m_pimpl->m_nodeid_service.add(address_info.node_address,
                                                      beltpp_ip_address,
                                                      unique_ptr<session_action_broadcast_address_info>(
                                                          new session_action_broadcast_address_info(*m_pimpl.get(),
                                                                                                    peerid,
                                                                                                    std::move(broadcast))));
                    }

                    break;
                }
                case Statistics::rtt:
                {
                    if (NodeType::blockchain == m_pimpl->m_node_type || nullptr == m_pimpl->m_slave_node)
                        throw wrong_request_exception("Do not distrub!");

                    Statistics statistics;
                    std::move(ref_packet).get(statistics);
                    statistics.data.server_address = m_pimpl->m_pb_key.to_string();

                    Transaction transaction;
                    transaction.action = std::move(statistics.data);
                    transaction.creation.tm = system_clock::to_time_t(system_clock::now());
                    transaction.expiry.tm = system_clock::to_time_t(system_clock::now() + chrono::minutes(10));

                    Authority authority;
                    authority.address = m_pimpl->m_pb_key.to_string();
                    authority.signature = m_pimpl->m_pv_key.sign(transaction.to_string()).base58;

                    SignedTransaction signed_tx;
                    signed_tx.transaction_details = transaction;
                    signed_tx.authorizations.push_back(authority);

                    if (action_process_on_chain(signed_tx, *m_pimpl.get()))
                    {
                        Broadcast broadcast;
                        broadcast.echoes = 2;
                        broadcast.package = signed_tx;

                        broadcast_message(std::move(broadcast),
                                          m_pimpl->m_ptr_p2p_socket->name(),
                                          peerid,
                                          true,
                                          nullptr,
                                          m_pimpl->m_p2p_peers,
                                          m_pimpl->m_ptr_p2p_socket.get());
                    }


                    break;
                }
                case StorageFile::rtt:
                {
                    if (NodeType::blockchain == m_pimpl->m_node_type||
                        nullptr == m_pimpl->m_slave_node)
                        throw wrong_request_exception("Do not distrub!");

                    if (m_pimpl->m_slave_node)
                    {
                        StorageFile storage_file;
                        std::move(ref_packet).get(storage_file);

                        vector<unique_ptr<meshpp::session_action<meshpp::session_header>>> actions;
                        actions.emplace_back(new session_action_save_file(*m_pimpl.get(),
                                                                          std::move(storage_file),
                                                                          *psk,
                                                                          peerid));

                        meshpp::session_header header;
                        header.peerid = "slave";
                        m_pimpl->m_sessions.add(header,
                                                std::move(actions),
                                                chrono::minutes(1));
                    }
                    
                    break;
                }
                case StorageFileDelete::rtt:
                {
                    if (NodeType::blockchain == m_pimpl->m_node_type ||
                        nullptr == m_pimpl->m_slave_node)
                        throw wrong_request_exception("Do not distrub!");

                    if (m_pimpl->m_slave_node)
                    {
                        StorageFileDelete storage_file_delete;
                        std::move(ref_packet).get(storage_file_delete);

                        vector<unique_ptr<meshpp::session_action<meshpp::session_header>>> actions;
                        actions.emplace_back(new session_action_delete_file(*m_pimpl.get(),
                                                                            std::move(storage_file_delete.uri),
                                                                            *psk,
                                                                            peerid));

                        meshpp::session_header header;
                        header.peerid = "slave";
                        m_pimpl->m_sessions.add(header,
                                                std::move(actions),
                                                chrono::minutes(1));
                    }

                    break;
                }
                case LoggedTransactionsRequest::rtt:
                {
                    if (it == interface_type::rpc)
                    {
                        LoggedTransactionsRequest msg_get_actions;
                        std::move(ref_packet).get(msg_get_actions);
                        get_actions(msg_get_actions, m_pimpl->m_action_log, *psk, peerid);
                    }
                    break;
                }
                case DigestRequest::rtt:
                {
                    DigestRequest msg_get_hash;
                    std::move(ref_packet).get(msg_get_hash);
                    get_hash(std::move(msg_get_hash), *psk, peerid);
                    break;
                }
                case MasterKeyRequest::rtt:
                {
                    get_random_seed(*psk, peerid);
                    break;
                }
                case PublicAddressesRequest::rtt:
                {
                    PublicAddressesRequest msg;
                    std::move(ref_packet).get(msg);

                    if (msg.address_type == PublicAddressType::rpc)
                        get_public_addresses(*psk, peerid, *m_pimpl.get());
                    else
                        get_peers_addresses(*psk, peerid, *m_pimpl.get());

                    break;
                }
                case KeyPairRequest::rtt:
                {
                    KeyPairRequest kpr_msg;
                    std::move(ref_packet).get(kpr_msg);
                    get_key_pair(kpr_msg, *psk, peerid);
                    break;
                }
                case SignRequest::rtt:
                {
                    SignRequest msg_sign_request;
                    std::move(ref_packet).get(msg_sign_request);
                    get_signature(std::move(msg_sign_request), *psk, peerid);
                    break;
                }
                case Signature::rtt:
                {
                    Signature msg_signature;
                    std::move(ref_packet).get(msg_signature);
                    verify_signature(msg_signature, *psk, peerid);
                    break;
                }
                case SyncRequest::rtt:
                {
                    BlockHeaderExtended const& header = m_pimpl->m_blockchain.header_ex_at(m_pimpl->m_blockchain.length() - 1);

                    SyncResponse sync_response;
                    sync_response.own_header = header;

                    if (m_pimpl->all_sync_info.net_sync_info().c_sum > m_pimpl->all_sync_info.own_sync_info().c_sum)
                        sync_response.promised_header = m_pimpl->all_sync_info.net_sync_info();
                    else
                        sync_response.promised_header = m_pimpl->all_sync_info.own_sync_info();

                    psk->send(peerid, beltpp::packet(std::move(sync_response)));

                    break;
                }
                case BlockHeaderRequest::rtt:
                {
                    BlockHeaderRequest header_request;
                    std::move(ref_packet).get(header_request);

                    //m_pimpl->writeln_node("header response - " + peerid);
                    session_action_header::process_request(peerid,
                                                           header_request,
                                                           *m_pimpl.get());

                    break;
                }
                case BlockchainRequest::rtt:
                {
                    if (it != interface_type::p2p)
                        throw wrong_request_exception("BlockchainRequest received through rpc!");

                    BlockchainRequest blockchain_request;
                    std::move(ref_packet).get(blockchain_request);

                    //m_pimpl->writeln_node("chain response - " + peerid);
                    session_action_block::process_request(peerid,
                                                          blockchain_request,
                                                          *m_pimpl.get());

                    break;
                }
                case TransactionBroadcastRequest::rtt:
                {
                    TransactionBroadcastRequest transaction_broadcast_request;
                    std::move(ref_packet).get(transaction_broadcast_request);

                    Authority authorization;
                    meshpp::private_key pv(transaction_broadcast_request.private_key);
                    authorization.address = pv.get_public_key().to_string();
                    authorization.signature = pv.sign(transaction_broadcast_request.transaction_details.to_string()).base58;

                    BlockchainMessage::SignedTransaction signed_transaction;
                    signed_transaction.transaction_details = transaction_broadcast_request.transaction_details;
                    signed_transaction.authorizations.push_back(authorization);

                    TransactionDone transaction_done;
                    transaction_done.transaction_hash = meshpp::hash(signed_transaction.to_string());

                    if (action_process_on_chain(signed_transaction, *m_pimpl.get()))
                    {
                        BlockchainMessage::Broadcast broadcast;
                        broadcast.echoes = 2;
                        broadcast.package = std::move(signed_transaction);

                        broadcast_message(std::move(broadcast),
                                          m_pimpl->m_ptr_p2p_socket->name(),
                                          peerid,
                                          it == interface_type::rpc,
                                          //m_pimpl->plogger_node,
                                          nullptr,
                                          m_pimpl->m_p2p_peers,
                                          m_pimpl->m_ptr_p2p_socket.get());
                    }

                    psk->send(peerid, beltpp::packet(std::move(transaction_done)));

                    break;
                }
                case Ping::rtt:
                {
                    Pong msg_pong;
                    msg_pong.node_address = m_pimpl->m_pv_key.get_public_key().to_string();
                    msg_pong.stamp.tm = system_clock::to_time_t(system_clock::now());
                    string message_pong = msg_pong.node_address + ::beltpp::gm_time_t_to_gm_string(msg_pong.stamp.tm);
                    auto signed_message = m_pimpl->m_pv_key.sign(message_pong);

                    msg_pong.signature = std::move(signed_message.base58);
                    psk->send(peerid, beltpp::packet(std::move(msg_pong)));
                    break;
                }
                default:
                {
                    m_pimpl->writeln_node("master don't know how to handle: " + std::to_string(ref_packet.type())/* +
                                          ". dropping " + detail::peer_short_names(peerid)*/);

                    break;
                }
                }   // switch ref_packet.type()
            }
            catch (meshpp::exception_public_key const& e)
            {
                if (it == interface_type::rpc)
                {
                    InvalidPublicKey msg;
                    msg.public_key = e.pub_key;
                    psk->send(peerid, beltpp::packet(msg));
                }
                else
                {
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));
                    m_pimpl->remove_peer(peerid);
                }
                throw;
            }
            catch (meshpp::exception_private_key const& e)
            {
                if (it == interface_type::rpc)
                {
                    InvalidPrivateKey msg;
                    msg.private_key = e.priv_key;
                    psk->send(peerid, beltpp::packet(msg));
                }
                else
                {
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));
                    m_pimpl->remove_peer(peerid);
                }
                throw;
            }
            catch (meshpp::exception_signature const& e)
            {
                if (it == interface_type::rpc)
                {
                    InvalidSignature msg;
                    msg.details.public_key = e.sgn.pb_key.to_string();
                    msg.details.signature = e.sgn.base58;
                    BlockchainMessage::detail::loader(msg.details.package,
                                                      std::string(e.sgn.message.begin(), e.sgn.message.end()),
                                                      nullptr);

                    psk->send(peerid, beltpp::packet(msg));
                }
                else
                {
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));
                    m_pimpl->remove_peer(peerid);
                }
                throw;
            }
            catch (wrong_data_exception const& e)
            {
                if (it == interface_type::rpc)
                {
                    RemoteError remote_error;
                    remote_error.message = e.message;
                    psk->send(peerid, beltpp::packet(remote_error));
                }
                else
                {
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));
                    m_pimpl->remove_peer(peerid);
                }
                throw;
            }
            catch (wrong_request_exception const& e)
            {
                if (it == interface_type::rpc)
                {
                    RemoteError remote_error;
                    remote_error.message = e.message;
                    psk->send(peerid, beltpp::packet(remote_error));
                }
                else
                {
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));
                    m_pimpl->remove_peer(peerid);
                }
                throw;
            }
            catch (wrong_document_exception const& e)
            {
                if (it == interface_type::rpc)
                {
                    RemoteError remote_error;
                    remote_error.message = e.message;
                    psk->send(peerid, beltpp::packet(remote_error));
                }
                else
                {
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));
                    m_pimpl->remove_peer(peerid);
                }
                throw;
            }
            catch (authority_exception const& e)
            {
                if (it == interface_type::rpc)
                {
                    InvalidAuthority msg;
                    msg.authority_provided = e.authority_provided;
                    msg.authority_required = e.authority_required;
                    psk->send(peerid, beltpp::packet(msg));
                }
                else
                {
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));
                    m_pimpl->remove_peer(peerid);
                }
                throw;
            }
            catch (not_enough_balance_exception const& e)
            {
                if (it == interface_type::rpc)
                {
                    NotEnoughBalance msg;
                    e.balance.to_Coin(msg.balance);
                    e.spending.to_Coin(msg.spending);
                    psk->send(peerid, beltpp::packet(msg));
                }
                else
                {
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));
                    m_pimpl->remove_peer(peerid);
                }
                throw;
            }
            catch (too_long_string_exception const& e)
            {
                if (it == interface_type::rpc)
                {
                    TooLongString msg;
                    beltpp::assign(msg, e);
                    psk->send(peerid, beltpp::packet(msg));
                }
                else
                {
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));
                    m_pimpl->remove_peer(peerid);
                }
                throw;
            }
            catch (uri_exception const& e)
            {
                if (it == interface_type::rpc)
                {
                    UriError msg;
                    beltpp::assign(msg, e);
                    psk->send(peerid, beltpp::packet(msg));
                }
                else
                {
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));
                    m_pimpl->remove_peer(peerid);
                }
                throw;
            }
            catch (std::exception const& e)
            {
                if (it == interface_type::rpc)
                {
                    RemoteError msg;
                    msg.message = e.what();
                    psk->send(peerid, beltpp::packet(msg));
                }
                throw;
            }
            catch (...)
            {
                if (it == interface_type::rpc)
                {
                    RemoteError msg;
                    msg.message = "unknown exception";
                    psk->send(peerid, beltpp::packet(msg));
                }
                throw;
            }
            }   // for (auto& received_packet : received_packets)
        }   // for (auto& pevent_item : wait_sockets)
    }

    if (wait_result & beltpp::event_handler::timer_out)
    {
        m_pimpl->m_ptr_p2p_socket->timer_action();
        m_pimpl->m_ptr_rpc_socket->timer_action();
    }

    if (m_pimpl->m_slave_node && (wait_result & beltpp::event_handler::on_demand))
    {
        beltpp::socket::packets received_packets = m_pimpl->m_slave_node->receive();

        for (auto& ref_packet : received_packets)
            m_pimpl->m_sessions.process("slave", std::move(ref_packet));
    }

    m_pimpl->m_sessions.erase_all_pending();
    m_pimpl->m_sync_sessions.erase_all_pending();
    m_pimpl->m_nodeid_sessions.erase_all_pending();

    // broadcast own transactions to all peers for the case
    // when node could not do this when received it through rpc
    if (m_pimpl->m_broadcast_timer.expired() && !m_pimpl->m_p2p_peers.empty())
    {
        m_pimpl->m_broadcast_timer.update();

        size_t pool_size = m_pimpl->m_transaction_pool.length();
        if (pool_size > 0)
        {
            m_pimpl->writeln_node("broadcasting old stored transactions to all peers");

            auto current_time = system_clock::now();

            for (size_t pool_index = 0; pool_index != pool_size; ++pool_index)
            {
                SignedTransaction const& signed_transaction = m_pimpl->m_transaction_pool.at(pool_index);

                if (current_time < system_clock::from_time_t(signed_transaction.transaction_details.expiry.tm) &&
                    current_time > system_clock::from_time_t(signed_transaction.transaction_details.creation.tm) + chrono::seconds(BLOCK_MINE_DELAY))
                {
                    Broadcast broadcast;
                    broadcast.echoes = 2;
                    broadcast.package = signed_transaction;

                    broadcast_message(std::move(broadcast),
                                      m_pimpl->m_ptr_p2p_socket->name(),
                                      m_pimpl->m_ptr_p2p_socket->name(),
                                      true, // like from rpc
                                      nullptr, // no logger
                                      m_pimpl->m_p2p_peers,
                                      m_pimpl->m_ptr_p2p_socket.get());
                }
            }
        }
    }

    // clean old transaction keys from cache
    // to minimize it and make it work faster
    if (m_pimpl->m_cache_cleanup_timer.expired())
    {
        m_pimpl->m_cache_cleanup_timer.update();

        m_pimpl->clean_transaction_cache();

        //  temp place
        broadcast_node_type(m_pimpl);
        broadcast_address_info(m_pimpl);

        //  yes temp place still
        m_pimpl->m_nodeid_service.take_actions([this](std::string const& node_address,
                                                      beltpp::ip_address const& address,
                                                      std::unique_ptr<session_action_broadcast_address_info>&& ptr_action)
        {
            vector<unique_ptr<meshpp::session_action<meshpp::nodeid_session_header>>> actions;
            actions.emplace_back(new session_action_connections(*m_pimpl->m_ptr_rpc_socket.get()));
            actions.emplace_back(new session_action_signatures(*m_pimpl->m_ptr_rpc_socket.get(),
                                                                m_pimpl->m_nodeid_service));

            actions.emplace_back(std::move(ptr_action));

            meshpp::nodeid_session_header header;
            header.nodeid = node_address;
            header.address = address;
            m_pimpl->m_nodeid_sessions.add(header,
                                           std::move(actions),
                                           chrono::minutes(1));
        });
    }

    // init sync process and block mining
    if (m_pimpl->m_check_timer.expired())
    {
        m_pimpl->m_check_timer.update();

        sync_worker(*m_pimpl.get());
    }

    return code;
}

void node::set_slave_node(storage_node& slave_node)
{
    m_pimpl->m_slave_node = &slave_node;
}

//  free functions
void block_worker(detail::node_internals& impl)
{
    if (impl.all_sync_info.blockchain_sync_in_progress)
        return;

    uint64_t additional_delay_threshhold = 60;

    auto const blockchain_length = impl.m_blockchain.length();
    auto const last_header = impl.m_blockchain.last_header();

    chrono::system_clock::duration last_block_age =
            chrono::system_clock::now() -
            chrono::system_clock::from_time_t(last_header.time_signed.tm);

    double last_block_age_seconds = double(chrono::duration_cast<chrono::seconds>(last_block_age).count());

    double revert_fraction = std::max(1.0, last_block_age_seconds / BLOCK_MINE_DELAY);

    auto it_scan_least_revert = impl.all_sync_info.headers_actions_data.end();
    auto it_scan_most_approved_revert = impl.all_sync_info.headers_actions_data.end();

    for (auto it = impl.all_sync_info.headers_actions_data.begin();
         it != impl.all_sync_info.headers_actions_data.end();
         ++it)
    {
        if (it->second.headers.empty())
            continue;

        auto scan_peer = it->first;
        auto start_number = it->second.headers.back().block_number;

        if (start_number > blockchain_length)
            continue;   //  in case we had earlier reverted to a shorter chain
                        //  but an even older headers response is laying around

        if (start_number > 0 &&
            impl.m_blockchain.header_ex_at(start_number - 1).block_hash !=
            it->second.headers.back().prev_hash)
            continue;

        double revert_coefficient = 0;
        size_t revert_count = blockchain_length - start_number;
        if (1 <= revert_count)
            revert_coefficient = revert_count - 1 + revert_fraction;

        it->second.reverts_required = revert_coefficient;

        bool unsafe_time_to_revert = BLOCK_WAIT_DELAY + additional_delay_threshhold <
                                     revert_coefficient * BLOCK_MINE_DELAY;
        //  that is if revert_coefficient > 0.4
        //  or in other words need to revert block that is older than 4 minutes

        if (unsafe_time_to_revert)
        {
            unordered_map<string, string> map_nodeid_ip_address;
            for (auto const& peerid : impl.m_p2p_peers)
                map_nodeid_ip_address[peerid] = impl.m_ptr_p2p_socket->info_connection(peerid).remote.address;

            PublicAddressesInfo public_addresses = impl.m_nodeid_service.get_addresses();

            for (auto const& item : public_addresses.addresses_info)
            {
                if (item.seconds_since_checked > PUBLIC_ADDRESS_FRESH_THRESHHOLD_SECONDS)
                    break;
                if (impl.m_p2p_peers.count(item.node_address))
                    continue;

                map_nodeid_ip_address[item.node_address] = item.ip_address.local.address;
            }

            coin approve, reject;
            enum class vote_type {approve, reject};
            unordered_map<string, pair<coin, vote_type>> votes;

            size_t poll_participants = 0;

            for (auto const& item : impl.all_sync_info.sync_responses)
            {
                auto it_ip_address = map_nodeid_ip_address.find(item.first);
                if (it_ip_address == map_nodeid_ip_address.end())
                    continue;
                string str_ip_address = it_ip_address->second;
                auto& replacing = votes[str_ip_address];
                auto voting = std::make_pair(
                                  impl.m_state.get_balance(item.first, state_layer::pool) + coin(1,0),
                                  vote_type::approve);
                if (voting.first <= replacing.first)
                    continue;

                if (replacing.first == publiqpp::coin())
                    ++poll_participants;

                if (replacing.second == vote_type::approve)
                    approve -= replacing.first;
                else
                    reject -= replacing.first;

                if (item.second.own_header == it->second.headers.front())
                {
                    voting.second = vote_type::approve;
                    approve += voting.first;
                }
                else
                {
                    voting.second = vote_type::reject;
                    reject += voting.first;
                }

                replacing = voting;
            }

            if (approve > reject &&
                poll_participants > 2 &&
                (
                    impl.all_sync_info.headers_actions_data.end() == it_scan_most_approved_revert ||
                    it_scan_most_approved_revert->second.reverts_required < revert_coefficient
                )
               )
                it_scan_most_approved_revert = it;
        }
        else if (impl.all_sync_info.headers_actions_data.end() == it_scan_least_revert ||
                 it_scan_least_revert->second.reverts_required > revert_coefficient)
            it_scan_least_revert = it;
    }


    auto it_chosen = impl.all_sync_info.headers_actions_data.end();
    if (impl.all_sync_info.headers_actions_data.end() != it_scan_most_approved_revert)
        it_chosen = it_scan_most_approved_revert;
    else if (impl.all_sync_info.headers_actions_data.end() != it_scan_least_revert)
        it_chosen = it_scan_least_revert;

    if (impl.all_sync_info.headers_actions_data.end() != it_chosen)
    {
        vector<unique_ptr<meshpp::session_action<meshpp::nodeid_session_header>>> actions;
        actions.emplace_back(new session_action_block(impl));

        meshpp::nodeid_session_header header;
        header.nodeid = it_chosen->first;
        header.address = impl.m_ptr_p2p_socket->info_connection(it_chosen->first);
        impl.m_sync_sessions.add(header,
                                 std::move(actions),
                                 chrono::seconds(SYNC_TIMER));
    }
}

double header_worker(detail::node_internals& impl)
{
    block_worker(impl);

    // process collected SyncResponse data
    BlockHeaderExtended head_block_header = impl.m_blockchain.last_header_ex();

    BlockHeaderExtended scan_block_header = head_block_header;
    beltpp::isocket::peer_id scan_peer;

    double revert_coefficient = 0;

    for (auto const& item : impl.all_sync_info.sync_responses)
    {
        if (0 == impl.m_p2p_peers.count(item.first))
            continue; // for the case if peer is dropped before sync started, or this is a public contact (using rpc)

        auto it = impl.all_sync_info.headers_actions_data.find(item.first);
        if (it != impl.all_sync_info.headers_actions_data.end() && it->second.reverts_required > 0)
        {
            revert_coefficient = it->second.reverts_required;
            continue; // don't consider this peer during header action is active
        }

        if (scan_block_header.c_sum < item.second.own_header.c_sum &&
            scan_block_header.block_number <= item.second.own_header.block_number)
        {
            scan_block_header = item.second.own_header;
            scan_peer = item.first;
        }
    }

    //  work through process of block header sync or mining
    //  if there is no active sync
    if (false == impl.all_sync_info.blockchain_sync_in_progress)
    {
        head_block_header = impl.m_blockchain.last_header_ex();

        //  the duration passed according to my system time since the head block
        //  was signed
        chrono::system_clock::duration since_head_block =
                system_clock::now() -
                system_clock::from_time_t(head_block_header.time_signed.tm);

        bool mine_now = false;
        bool sync_now = false;

        auto net_sum = impl.all_sync_info.net_sync_info().c_sum;
        auto own_sum = impl.all_sync_info.own_sync_info().c_sum;

        if (head_block_header.block_number + 1 < scan_block_header.block_number)
        {
            // network is far behind and I have to sync first
            sync_now = true;
        }
        else if (head_block_header.c_sum < scan_block_header.c_sum &&
                 head_block_header.block_number == scan_block_header.block_number)
        {
            //  there is a better consensus sum than what I have and I must get it first
            sync_now = true;
        }
        else if (own_sum < scan_block_header.c_sum &&
                 net_sum < scan_block_header.c_sum)
        {
            //  direct peer has ready excellent chain to offer
            sync_now = true;
        }
        else if (own_sum < scan_block_header.c_sum &&
                 net_sum == scan_block_header.c_sum)
        {
            //  direct peer finally got the excellent chain that was promised before
            sync_now = true;
        }
        else if (own_sum < scan_block_header.c_sum/* &&
                 net_sum > scan_block_header.c_sum*/)
        {
            //  the promised excellent block did not arrive yet
            //  and the network has ready better block than I can mine
            if (chrono::seconds(BLOCK_MINE_DELAY + BLOCK_WAIT_DELAY) < since_head_block)
            {
                //  and it is too late already, can't wait anymore
                sync_now = true;
            }
            //  otherwise will just wait
        }
        else// if (own_sum >= scan_block_header.c_sum)
        {
            //  I can mine better block and don't need received data
            //  I can wait until it is
            //  either past BLOCK_MINE_DELAY and I can mine better than scan_consensus_sum
            //  or it is past BLOCK_MINE_DELAY + BLOCK_WAIT_DELAY and I can mine better than net_sum
            if (chrono::seconds(BLOCK_MINE_DELAY + BLOCK_WAIT_DELAY) <= since_head_block ||
                (chrono::seconds(BLOCK_MINE_DELAY) <= since_head_block && own_sum >= net_sum))
            {
                mine_now = true;
                assert(false == sync_now);
            }
        }

        assert(false == sync_now || false == mine_now);

        if (sync_now)
        {
            assert(false == scan_peer.empty());
            vector<unique_ptr<meshpp::session_action<meshpp::nodeid_session_header>>> actions;
            actions.emplace_back(new session_action_p2pconnections(*impl.m_ptr_p2p_socket.get()));
            actions.emplace_back(new session_action_sync_request(impl,
                                                                 *impl.m_ptr_p2p_socket.get()));
            actions.emplace_back(new session_action_header(impl,
                                                           scan_block_header));

            meshpp::nodeid_session_header header;
            header.nodeid = scan_peer;
            header.address = impl.m_ptr_p2p_socket->info_connection(scan_peer);
            impl.m_sync_sessions.add(header,
                                     std::move(actions),
                                     chrono::seconds(SYNC_TIMER));
        }
        else if (impl.is_miner() && mine_now)
        {
            mine_block(impl);
        }
    }

    return revert_coefficient;
}

void sync_worker(detail::node_internals& impl)
{
    double revert_coefficient = header_worker(impl);

    for (auto const& peerid : impl.m_p2p_peers)
    {
        vector<unique_ptr<meshpp::session_action<meshpp::nodeid_session_header>>> actions;
        actions.emplace_back(new session_action_p2pconnections(*impl.m_ptr_p2p_socket.get()));
        actions.emplace_back(new session_action_sync_request(impl,
                                                             *impl.m_ptr_p2p_socket.get()));

        meshpp::nodeid_session_header header;
        header.nodeid = peerid;
        header.address = impl.m_ptr_p2p_socket->info_connection(peerid);
        impl.m_sync_sessions.add(header,
                                 std::move(actions),
                                 chrono::seconds(SYNC_TIMER));
    }

    if (0 < revert_coefficient)
    {
        PublicAddressesInfo public_addresses = impl.m_nodeid_service.get_addresses();

        for (auto const& item : public_addresses.addresses_info)
        {
            if (item.seconds_since_checked > PUBLIC_ADDRESS_FRESH_THRESHHOLD_SECONDS)
                break;
            if (impl.m_p2p_peers.count(item.node_address))
                continue;

            vector<unique_ptr<meshpp::session_action<meshpp::nodeid_session_header>>> actions;
            actions.emplace_back(new session_action_connections(*impl.m_ptr_rpc_socket.get()));
            actions.emplace_back(new session_action_signatures(*impl.m_ptr_rpc_socket.get(),
                                                               impl.m_nodeid_service));
            actions.emplace_back(new session_action_sync_request(impl,
                                                                 *impl.m_ptr_rpc_socket.get()));

            meshpp::nodeid_session_header header;
            header.nodeid = item.node_address;
            beltpp::assign(header.address, item.ip_address);
            impl.m_nodeid_sessions.add(header,
                                       std::move(actions),
                                       chrono::minutes(SYNC_TIMER));
        }
    }
}

}


