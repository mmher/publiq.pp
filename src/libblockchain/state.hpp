#pragma once

#include "coin.hpp"
#include "message.hpp"

#include <boost/filesystem/path.hpp>

#include <vector>
#include <string>

namespace publiqpp
{
namespace detail
{
class node_internals;
}
enum class state_layer { chain, pool };

namespace detail
{
class state_internals;
}
class state
{
public:
    state(boost::filesystem::path const& fs_state,
          detail::node_internals const& impl);
    ~state();

    void save();
    void commit();
    void discard();

    BlockchainMessage::Coin get_balance(std::string const& key, state_layer layer) const;
    void set_balance(std::string const& key, coin const& amount, state_layer layer);
    void increase_balance(std::string const& key, coin const& amount, state_layer layer);
    void decrease_balance(std::string const& key, coin const& amount, state_layer layer);

    std::vector<std::string> get_nodes_by_type(BlockchainMessage::NodeType const& node_type) const;
    bool get_role(std::string const& nodeid, BlockchainMessage::NodeType& node_type) const;
    void insert_role(BlockchainMessage::Role const& role);
    void remove_role(std::string const& nodeid);

private:
    std::unique_ptr<detail::state_internals> m_pimpl;
};
}
