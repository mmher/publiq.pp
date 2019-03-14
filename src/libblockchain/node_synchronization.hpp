#pragma once

#include "global.hpp"
#include "message.hpp"

#include <belt.pp/isocket.hpp>

#include <vector>
#include <utility>
#include <unordered_map>

namespace publiqpp
{
namespace detail
{
class node_internals;
}
}

namespace publiqpp
{
class node_synchronization
{
public:
    node_synchronization(detail::node_internals& impl);
    detail::node_internals* pimpl;
    std::unordered_map<beltpp::isocket::peer_id, BlockchainMessage::SyncResponse> sync_responses;
    std::unordered_map<beltpp::isocket::peer_id, std::vector<BlockchainMessage::BlockHeader>> sync_headers;
    BlockchainMessage::SyncInfo net_sync_info() const;
    BlockchainMessage::SyncInfo own_sync_info() const;
};
}
