#include "action_log.hpp"

#include "data.hpp"
#include "message.hpp"

#include <mesh.pp/fileutility.hpp>

#include <string>

namespace filesystem = boost::filesystem;
using std::string;

namespace
{
using logged_action_loader = meshpp::file_loader<BlockchainMessage::LoggedTransaction,
&BlockchainMessage::LoggedTransaction::string_loader,
&BlockchainMessage::LoggedTransaction::string_saver>;
}

namespace publiqpp
{

namespace detail
{
class action_log_internals
{
public:
    action_log_internals(filesystem::path const& path)
        : m_path(path)
        , m_length(path / "action_log.act.length")
    {}

    using number_loader = meshpp::file_loader<Data::Number, &Data::Number::string_loader, &Data::Number::string_saver>;
    using number_locked_loader = meshpp::file_locker<number_loader>;

    filesystem::path m_path;
    number_locked_loader m_length;
};
}

action_log::action_log(boost::filesystem::path const& fs_action_log)
    : m_pimpl(new detail::action_log_internals(fs_action_log))
{

}
action_log::~action_log()
{

}

size_t action_log::length() const
{
    return m_pimpl->m_length.as_const()->value;
}

void action_log::insert(BlockchainMessage::LoggedTransaction const& action_info)
{
    size_t index = m_pimpl->m_length.as_const()->value;
    string file_name(std::to_string(index) + ".act");

    logged_action_loader fl(m_pimpl->m_path / file_name);
    *fl = action_info;
    if (true == action_info.applied_reverted)   //  apply
        fl->index = index;
    fl.save();

    m_pimpl->m_length->value++;
    m_pimpl->m_length.save();
}

bool action_log::at(size_t index, BlockchainMessage::LoggedTransaction& action_info) const
{
    if (index >= m_pimpl->m_length->value)
        return false;

    string file_name(std::to_string(index) + ".act");
    logged_action_loader fl(m_pimpl->m_path / file_name);
    action_info = *fl.as_const();

    return true;
}
}
