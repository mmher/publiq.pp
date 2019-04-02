#pragma once

#include "global.hpp"
#include "message.hpp"

#include <belt.pp/ilog.hpp>
#include <belt.pp/isocket.hpp>

#include <mesh.pp/cryptoutility.hpp>

#include <boost/filesystem/path.hpp>

#include <memory>
#include <string>

namespace publiqpp
{
class storage_node;
namespace detail
{
    class node_internals;
}

class BLOCKCHAINSHARED_EXPORT node
{
public:
    node(std::string const& genesis_signed_block,
         beltpp::ip_address const & public_address,
         beltpp::ip_address const& rpc_bind_to_address,
         beltpp::ip_address const& p2p_bind_to_address,
         std::vector<beltpp::ip_address> const& p2p_connect_to_addresses,
         boost::filesystem::path const& fs_blockchain,
         boost::filesystem::path const& fs_action_log,
         boost::filesystem::path const& fs_transaction_pool,
         boost::filesystem::path const& fs_state,
         boost::filesystem::path const& fs_documents,
         beltpp::ilog* plogger_p2p,
         beltpp::ilog* plogger_node,
         meshpp::private_key const& pv_key,
         BlockchainMessage::NodeType& n_type,
         bool log_enabled,
         bool transfer_only);
    node(node&& other) noexcept;
    ~node();

    void wake();
    std::string name() const;
    bool run();
    void set_slave_node(storage_node& slave_node);

private:
    std::unique_ptr<detail::node_internals> m_pimpl;
};

}

