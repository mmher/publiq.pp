#include "rpc.hpp"
#include "exception.hpp"
#include "rpc_internals.hpp"

#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <exception>

using beltpp::packet;
using beltpp::socket;
using beltpp::ip_address;
using peer_id = socket::peer_id;


namespace chrono = std::chrono;
using chrono::system_clock;

using std::pair;
using std::string;
using std::vector;
using std::unique_ptr;
using std::unordered_set;

namespace storage_utility
{
rpc::rpc(ip_address const& rpc_bind_to_address,
           beltpp::ilog* plogger_rpc)
    : m_pimpl(new detail::rpc_internals(rpc_bind_to_address,
                                        plogger_rpc))
{}

rpc::rpc(rpc&&) noexcept = default;

rpc::~rpc() = default;

void rpc::wake()
{
    m_pimpl->m_ptr_eh->wake();
}

bool rpc::run()
{
    bool code = true;

    unordered_set<beltpp::ievent_item const*> wait_sockets;

    auto wait_result = m_pimpl->m_ptr_eh->wait(wait_sockets);

    if (wait_result & beltpp::event_handler::event)
    {
        for (auto& pevent_item : wait_sockets)
        {
            beltpp::socket::peer_id peerid;

            beltpp::isocket* psk = nullptr;
            if (pevent_item == m_pimpl->m_ptr_rpc_socket.get())
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
                switch (received_packet.type())
                {
                case beltpp::isocket_join::rtt:
                {

                    break;
                }
                case beltpp::isocket_drop::rtt:
                {

                    break;
                }
                case beltpp::isocket_protocol_error::rtt:
                {
                    beltpp::isocket_protocol_error msg;
                    received_packet.get(msg);
                    m_pimpl->writeln_rpc("protocol error: ");
                    m_pimpl->writeln_rpc(msg.buffer);
                    psk->send(peerid, beltpp::packet(beltpp::isocket_drop()));

                    break;
                }
                case beltpp::isocket_open_refused::rtt:
                {
                    beltpp::isocket_open_refused msg;
                    received_packet.get(msg);
                    //m_pimpl->writeln_rpc_warning(msg.reason + ", " + peerid);
                    break;
                }
                case beltpp::isocket_open_error::rtt:
                {
                    beltpp::isocket_open_error msg;
                    received_packet.get(msg);
                    //m_pimpl->writeln_rpc_warning(msg.reason + ", " + peerid);
                    break;
                }
                case SignRequest::rtt:
                {
                    SignRequest msg_sign_request;
                    std::move(received_packet).get(msg_sign_request);

                    string map_key;
                    map_key =   msg_sign_request.private_key +
                                msg_sign_request.order.storage_address +
                                msg_sign_request.order.file_uri +
                                msg_sign_request.order.content_unit_uri +
                                msg_sign_request.order.session_id;

                    auto const& signed_storage_order_it = m_pimpl->cache_signed_storage_order.find(map_key);
                    if (signed_storage_order_it != m_pimpl->cache_signed_storage_order.end())
                    {
                        chrono::system_clock::duration duration =
                                chrono::system_clock::now() -
                                chrono::system_clock::from_time_t(signed_storage_order_it->second.order.time_signed.tm);

                        uint64_t duration_seconds = uint64_t(chrono::duration_cast<chrono::seconds>(duration).count());

                        if (signed_storage_order_it->second.order.seconds > duration_seconds)
                        {
                            psk->send(peerid, beltpp::packet(signed_storage_order_it->second));
                        }
                        else
                        {
                            meshpp::private_key pv(msg_sign_request.private_key);

                            Authority authorization;
                            authorization.address = pv.get_public_key().to_string();
                            authorization.signature = pv.sign(msg_sign_request.order.to_string()).base58;

                            SignedStorageOrder signed_storage_order;
                            signed_storage_order.order = msg_sign_request.order;
                            signed_storage_order.authorization = authorization;

                            m_pimpl->cache_signed_storage_order.erase(map_key);
                            m_pimpl->cache_signed_storage_order.insert(std::make_pair(map_key, signed_storage_order));

                            psk->send(peerid, beltpp::packet(std::move(signed_storage_order)));
                        }
                    }
                    else
                    {
                        meshpp::private_key pv(msg_sign_request.private_key);

                        Authority authorization;
                        authorization.address = pv.get_public_key().to_string();
                        authorization.signature = pv.sign(msg_sign_request.order.to_string()).base58;

                        SignedStorageOrder signed_storage_order;
                        signed_storage_order.order = msg_sign_request.order;
                        signed_storage_order.authorization = authorization;

                        m_pimpl->cache_signed_storage_order.insert(std::make_pair(map_key, signed_storage_order));

                        psk->send(peerid, beltpp::packet(std::move(signed_storage_order)));
                    }

                    break;
                }
                case SignedStorageOrder::rtt:
                {
                    SignedStorageOrder msg_verfy_sig_storage_order;
                    std::move(received_packet).get(msg_verfy_sig_storage_order);

                    string message = msg_verfy_sig_storage_order.order.to_string();

                    if ( meshpp::verify_signature(
                                msg_verfy_sig_storage_order.authorization.address,
                                message,
                                msg_verfy_sig_storage_order.authorization.signature)
                        )
                    {
                        chrono::system_clock::duration duration =
                                chrono::system_clock::now() -
                                chrono::system_clock::from_time_t(msg_verfy_sig_storage_order.order.time_signed.tm);

                        uint64_t duration_seconds = uint64_t(chrono::duration_cast<chrono::seconds>(duration).count());

                        if (msg_verfy_sig_storage_order.order.seconds > duration_seconds)
                        {
                            VerificationResponse verify_response;

                            verify_response.channel_address = msg_verfy_sig_storage_order.authorization.address;
                            verify_response.storage_address = msg_verfy_sig_storage_order.order.storage_address;
                            verify_response.file_uri = msg_verfy_sig_storage_order.order.file_uri;
                            verify_response.content_unit_uri = msg_verfy_sig_storage_order.order.content_unit_uri;
                            verify_response.session_id = msg_verfy_sig_storage_order.order.session_id;

                            psk->send(peerid, beltpp::packet(std::move(verify_response)));
                            break;
                        }
                        else
                        {
                            RemoteError remote_error;
                            remote_error.message = "sign time expired";
                            psk->send(peerid, beltpp::packet(remote_error));
                        }
                    }
                    else
                    {
                        RemoteError remote_error;
                        remote_error.message = "sign verification failed";
                        psk->send(peerid, beltpp::packet(remote_error));
                    }

                    break;
                }
                default:
                {
                    m_pimpl->writeln_rpc("can't handle: " + std::to_string(received_packet.type()) +
                                          ". peer: " + peerid);

                    break;
                }
                }   // switch received_packet.type()
            }
//            catch (meshpp::exception_public_key const& e)
//            {
//                InvalidPublicKey msg;
//                msg.public_key = e.pub_key;
//                psk->send(peerid, beltpp::packet(msg));

//                throw;
//            }
//            catch (meshpp::exception_private_key const& e)
//            {
//                InvalidPrivateKey msg;
//                msg.private_key = e.priv_key;
//                psk->send(peerid, beltpp::packet(msg));

//                throw;
//            }
//            catch (meshpp::exception_signature const& e)
//            {
//                InvalidSignature msg;
//                msg.details.public_key = e.sgn.pb_key.to_string();
//                msg.details.signature = e.sgn.base58;
//                BlockchainMessage::detail::loader(msg.details.package,
//                                                  std::string(e.sgn.message.begin(), e.sgn.message.end()),
//                                                  nullptr);

//                psk->send(peerid, beltpp::packet(msg));
//                throw;
//            }
//            catch (wrong_data_exception const& e)
//            {
//                RemoteError remote_error;
//                remote_error.message = e.message;
//                psk->send(peerid, beltpp::packet(remote_error));

//                throw;
//            }
//            catch (wrong_request_exception const& e)
//            {
//                RemoteError remote_error;
//                remote_error.message = e.message;
//                psk->send(peerid, beltpp::packet(remote_error));

//                throw;
//            }
//            catch (wrong_document_exception const& e)
//            {
//                RemoteError remote_error;
//                remote_error.message = e.message;
//                psk->send(peerid, beltpp::packet(remote_error));

//                throw;
//            }
//            catch (authority_exception const& e)
//            {
//                InvalidAuthority msg;
//                msg.authority_provided = e.authority_provided;
//                msg.authority_required = e.authority_required;
//                psk->send(peerid, beltpp::packet(msg));

//                throw;
//            }
//            catch (too_long_string_exception const& e)
//            {
//                TooLongString msg;
//                beltpp::assign(msg, e);
//                psk->send(peerid, beltpp::packet(msg));

//                throw;
//            }
//            catch (uri_exception const& e)
//            {
//                UriError msg;
//                beltpp::assign(msg, e);
//                psk->send(peerid, beltpp::packet(msg));

//                throw;
//            }
            catch (std::exception const& e)
            {
                RemoteError msg;
                msg.message = e.what();
                psk->send(peerid, beltpp::packet(msg));

                throw;
            }
            catch (...)
            {
                RemoteError msg;
                msg.message = "unknown exception";
                psk->send(peerid, beltpp::packet(msg));

                throw;
            }
            }   // for (auto& received_packet : received_packets)
        }   // for (auto& pevent_item : wait_sockets)
    }

    if (wait_result & beltpp::event_handler::timer_out)
        m_pimpl->m_ptr_rpc_socket->timer_action();

    if (wait_result & beltpp::event_handler::on_demand)
    {
    }

    return code;
}
}


