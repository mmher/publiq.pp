#pragma once

#include "global.hpp"

#include "blockchain.hpp"
#include "action_log.hpp"
#include "storage.hpp"

#include <boost/filesystem/path.hpp>

#include <memory>

namespace publiqpp
{
namespace detail
{
class state_internals;
}
class state
{
public:
    state(boost::filesystem::path const& fs_blockchain,
          boost::filesystem::path const& fs_action_log,
          boost::filesystem::path const& fs_storage);
    ~state();

    publiqpp::blockchain& blockchain();
    publiqpp::action_log& action_log();
    publiqpp::storage& storage();
private:
    std::unique_ptr<detail::state_internals> m_pimpl;
};
}
