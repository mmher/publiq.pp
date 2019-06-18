#pragma once

#include "global.hpp"
#include "message.hpp"
#include "node.hpp"
#include "common.hpp"
#include "state.hpp"

#include <string>
#include <vector>

namespace publiqpp
{
std::vector<std::string> action_owners(BlockchainMessage::File const& file);

void action_validate(BlockchainMessage::SignedTransaction const& signed_transaction,
                     BlockchainMessage::File const& file,
                     bool check_complete);

bool action_is_complete(BlockchainMessage::SignedTransaction const& signed_transaction,
                        BlockchainMessage::File const& file);

bool action_can_apply(publiqpp::detail::node_internals const& impl,
                      BlockchainMessage::File const& file,
                      state_layer layer);

void action_apply(publiqpp::detail::node_internals& impl,
                  BlockchainMessage::File const& file,
                  state_layer layer);

void action_revert(publiqpp::detail::node_internals& impl,
                   BlockchainMessage::File const& file,
                   state_layer layer);
}
