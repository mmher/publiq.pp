#include "sessions.hpp"
#include "common.hpp"
#include "message.hpp"
#include "communication_rpc.hpp"
#include "node_internals.hpp"

#include <mesh.pp/cryptoutility.hpp>
#include <belt.pp/utility.hpp>

#include <iostream>
#include <algorithm>

namespace chrono = std::chrono;
using chrono::system_clock;
using chrono::steady_clock;
using std::string;

namespace publiqpp
{
// --------------------------- session_action_connections ---------------------------
session_action_connections::session_action_connections(beltpp::socket& sk)
    : meshpp::session_action()
    , psk(&sk)
    , peerid_to_drop()
{}

session_action_connections::~session_action_connections()
{
    if (false == peerid_to_drop.empty())
        psk->send(peerid_to_drop, beltpp::isocket_drop());
}

void session_action_connections::initiate(meshpp::session_header& header)
{
    auto peerids = psk->open(header.address);
    if (peerids.size() != 1)
        throw std::logic_error("socket open by ip address must give just one peerid");

    header.peerid = peerids.front();
    expected_next_package_type = beltpp::isocket_join::rtt;
}

bool session_action_connections::process(beltpp::packet&& package, meshpp::session_header& header)
{
    bool code = true;

    if (expected_next_package_type == package.type() &&
        expected_next_package_type != size_t(-1))
    {
        switch (package.type())
        {
        case beltpp::isocket_join::rtt:
            std::cout << "session_action_connections - join" << std::endl;
            peerid_to_drop = header.peerid;
            completed = true;
            expected_next_package_type = size_t(-1);
            break;
        default:
            assert(false);
            break;
        }
    }
    else
    {
        switch (package.type())
        {
        case beltpp::isocket_drop::rtt:
            errored = true;
            peerid_to_drop.clear();
            std::cout << "action_connections - drop" << std::endl;
            break;
        case beltpp::isocket_protocol_error::rtt:
            errored = true;
            std::cout << "action_connections - protocol error" << std::endl;
            psk->send(header.peerid, beltpp::isocket_drop());
            peerid_to_drop.clear();
            break;
        case beltpp::isocket_open_error::rtt:
            std::cout << "action_connections - open error" << std::endl;
            errored = true;
            break;
        case beltpp::isocket_open_refused::rtt:
            std::cout << "action_connections - open refused" << std::endl;
            errored = true;
            break;
        default:
            code = false;
            break;
        }
    }

    return code;
}

bool session_action_connections::permanent() const
{
    return true;
}

// --------------------------- session_action_connections ---------------------------

session_action_p2pconnections::session_action_p2pconnections(meshpp::p2psocket& sk)
    : meshpp::session_action()
    , psk(&sk)
{}

session_action_p2pconnections::~session_action_p2pconnections()
{
}

void session_action_p2pconnections::initiate(meshpp::session_header& header)
{
    header.peerid = header.nodeid;
    expected_next_package_type = size_t(-1);
    completed = true;
}

bool session_action_p2pconnections::process(beltpp::packet&& package, meshpp::session_header& header)
{
    bool code = true;

    switch (package.type())
    {
    case beltpp::isocket_drop::rtt:
        errored = true;
        std::cout << "action_p2pconnections - drop" << std::endl;
        break;
    case beltpp::isocket_protocol_error::rtt:
        errored = true;
        std::cout << "action_p2pconnections - protocol error" << std::endl;
        psk->send(header.peerid, beltpp::isocket_drop());
        break;
    default:
        code = false;
        break;
    }

    return code;
}

bool session_action_p2pconnections::permanent() const
{
    return true;
}

// --------------------------- session_action_signatures ---------------------------

session_action_signatures::session_action_signatures(beltpp::socket& sk,
                                                     nodeid_service& service)
    : session_action()
    , psk(&sk)
    , pnodeid_service(&service)
    , address()
{}

session_action_signatures::~session_action_signatures()
{
    if (completed &&
        false == errored)
        erase(true, false);
    else if (expected_next_package_type != size_t(-1))
        erase(false, false);
}

void session_action_signatures::initiate(meshpp::session_header& header)
{
    psk->send(header.peerid, BlockchainMessage::Ping());
    expected_next_package_type = BlockchainMessage::Pong::rtt;

    nodeid = header.nodeid;
    address = header.address;
}

bool session_action_signatures::process(beltpp::packet&& package, meshpp::session_header& header)
{
    bool code = true;

    if (expected_next_package_type == package.type() &&
        expected_next_package_type != size_t(-1))
    {
        switch (package.type())
        {
        case BlockchainMessage::Pong::rtt:
        {
            //std::cout << "action_signatures -> pong received" << std::endl;
            BlockchainMessage::Pong msg;
            std::move(package).get(msg);

            auto diff = system_clock::from_time_t(msg.stamp.tm) - system_clock::now();
            string message = msg.node_address + ::beltpp::gm_time_t_to_gm_string(msg.stamp.tm);

            if ((chrono::seconds(-30) <= diff && diff < chrono::seconds(30)) &&
                meshpp::verify_signature(msg.node_address, message, msg.signature) &&
                header.nodeid == msg.node_address)
            {
                std::cout << "action_signatures - ok verify" << std::endl;

                erase(true, true);
            }
            else
            {
                errored = true;
                std::cout << "action_signatures -> signiture filed" << std::endl;

                erase(false, false);
            }

            completed = true;
            expected_next_package_type = size_t(-1);
            break;
        }
        default:
            assert(false);
            break;
        }
    }
    else
    {
        code = false;
    }

    return code;
}

bool session_action_signatures::permanent() const
{
    return true;
}

void session_action_signatures::erase(bool success, bool verified)
{
    auto it = pnodeid_service->nodeids.find(nodeid);
    if (it == pnodeid_service->nodeids.end())
    {
        assert(false);
        throw std::logic_error("session_action_signatures::process "
            "cannot find the expected entry");
    }
    else
    {
        auto& array = it->second.addresses;
        auto it_end = std::remove_if(array.begin(), array.end(),
            [this, success](nodeid_address_unit const& unit)
        {
            if (success)
                return unit.address != address;
            else
                return unit.address == address;
        });
        array.erase(it_end, array.end());

        if (success)
        {
            assert(array.size() == 1);
            array.front().verified = verified;
        }
    }
}

// --------------------------- session_action_broadcast_address_info ---------------------------

session_action_broadcast_address_info::session_action_broadcast_address_info(detail::node_internals* _pimpl,
                                                                             meshpp::p2psocket::peer_id const& _source_peer,
                                                                             BlockchainMessage::Broadcast&& _msg)
    : session_action()
    , pimpl(_pimpl)
    , source_peer(_source_peer)
    , msg(std::move(_msg))
{}

session_action_broadcast_address_info::~session_action_broadcast_address_info()
{}

void session_action_broadcast_address_info::initiate(meshpp::session_header&)
{
    std::cout << "action_broadcast - broadcasting" << std::endl;
    broadcast_message(std::move(msg),
                      pimpl->m_ptr_p2p_socket->name(),
                      source_peer,
                      false,
                      nullptr,
                      pimpl->m_p2p_peers,
                      pimpl->m_ptr_p2p_socket.get());

    expected_next_package_type = size_t(-1);
    completed = true;
}

bool session_action_broadcast_address_info::process(beltpp::packet&&, meshpp::session_header&)
{
    return false;
}

bool session_action_broadcast_address_info::permanent() const
{
    return false;
}

// --------------------------- session_action_storagefile ---------------------------

session_action_storagefile::session_action_storagefile(detail::node_internals* _pimpl,
                                                       string const& _file_uri)
    : session_action()
    , pimpl(_pimpl)
    , file_uri(_file_uri)
{}

session_action_storagefile::~session_action_storagefile()
{}

void session_action_storagefile::initiate(meshpp::session_header& header)
{
    BlockchainMessage::GetStorageFile get_storagefile;
    get_storagefile.uri = file_uri;

    pimpl->m_ptr_rpc_socket.get()->send(header.peerid, get_storagefile);
    expected_next_package_type = BlockchainMessage::StorageFile::rtt;
}

bool session_action_storagefile::process(beltpp::packet&& package, meshpp::session_header&)
{
    bool code = true;

    if (expected_next_package_type == package.type() &&
        expected_next_package_type != size_t(-1))
    {
        switch (package.type())
        {
        case BlockchainMessage::StorageFile::rtt:
        {
            std::cout << "action_storagefile -> File received" << std::endl;
            BlockchainMessage::StorageFile storage_file;
            package.get(storage_file);

            if (file_uri == meshpp::hash(storage_file.data))
            {
                std::cout << "action_storagefile -> File verified" << std::endl;

                if (!pimpl->m_slave_peer.empty())
                {
                    TaskRequest task_request;
                    task_request.task_id = ++pimpl->m_slave_taskid;
                    ::detail::assign_packet(task_request.package, package);
                    task_request.time_signed.tm = system_clock::to_time_t(system_clock::now());
                    meshpp::signature signed_msg = pimpl->m_pv_key.sign(std::to_string(task_request.task_id) +
                                                                        meshpp::hash(package.to_string()) +
                                                                        std::to_string(task_request.time_signed.tm));
                    task_request.signature = signed_msg.base58;

                    // send task to slave
                    pimpl->m_ptr_rpc_socket.get()->send(pimpl->m_slave_peer, task_request);

                    pimpl->m_slave_tasks.add(task_request.task_id, package);
                }
                else
                    pimpl->reconnect_slave();
            }
            else
            {
                errored = true;
                std::cout << "action_storagefile -> File verification filed" << std::endl;
            }

            completed = true;
            expected_next_package_type = size_t(-1);
            break;
        }
        default:
            assert(false);
            break;
        }
    }
    else
    {
        code = false;
    }

    return code;
}

bool session_action_storagefile::permanent() const
{
    return false;
}

// --------------------------- session_action_storagefile ---------------------------

session_action_sync_request::session_action_sync_request(detail::node_internals* _pimpl)
    : session_action()
    , pimpl(_pimpl)
{}

session_action_sync_request::~session_action_sync_request()
{
    if (false == current_peerid.empty())
        pimpl->all_sync_info.sync_responses.erase(current_peerid);
}

void session_action_sync_request::initiate(meshpp::session_header& header)
{
    pimpl->m_ptr_p2p_socket.get()->send(header.peerid, BlockchainMessage::SyncRequest2());
    expected_next_package_type = BlockchainMessage::SyncResponse2::rtt;
}

bool session_action_sync_request::process(beltpp::packet&& package, meshpp::session_header& header)
{
    bool code = true;

    if (expected_next_package_type == package.type() &&
        expected_next_package_type != size_t(-1))
    {
        switch (package.type())
        {
        case SyncResponse2::rtt:
        {
            SyncResponse2 sync_response;
            std::move(package).get(sync_response);

            current_peerid = header.peerid;
            pimpl->all_sync_info.sync_responses[current_peerid] = std::move(sync_response);

            expected_next_package_type = size_t(-1);
            completed = true;

            break;
        }
        default:
            assert(false);
            break;
        }
    }
    else
    {
        code = false;
    }

    return code;
}

bool session_action_sync_request::permanent() const
{
    return true;
}

}

