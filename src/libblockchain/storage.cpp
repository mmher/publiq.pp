#include "storage.hpp"
#include "common.hpp"

#include "types.hpp"
#include "message.hpp"

#include <mesh.pp/fileutility.hpp>
#include <mesh.pp/cryptoutility.hpp>

#include <belt.pp/utility.hpp>

#include <string>

namespace filesystem = boost::filesystem;
using std::string;
using std::unordered_map;
using std::unordered_set;

namespace publiqpp
{

namespace detail
{
class storage_internals
{
public:
    storage_internals(filesystem::path const& path)
        : map("storage", path, 10000, detail::get_putl())
    {}

    meshpp::map_loader<BlockchainMessage::StorageFile> map;
};
}

storage::storage(boost::filesystem::path const& fs_storage)
    : m_pimpl(new detail::storage_internals(fs_storage))
{}
storage::~storage()
{}

bool storage::put(BlockchainMessage::StorageFile&& file, string& uri)
{
    bool code = false;
    uri = meshpp::hash(file.data);
    file.data = meshpp::to_base64(file.data, true);
    beltpp::on_failure guard([this]
    {
        m_pimpl->map.discard();
    });

    if (m_pimpl->map.insert(uri, file))
        code = true;

    m_pimpl->map.save();

    guard.dismiss();
    m_pimpl->map.commit();
    return code;
}

bool storage::get(string const& uri, BlockchainMessage::StorageFile& file)
{
    if (false == m_pimpl->map.contains(uri))
        return false;

    file = m_pimpl->map.as_const().at(uri);

    file.data = meshpp::from_base64(file.data);

    if (beltpp::chance_one_of(1000))
        m_pimpl->map.discard();

    return true;
}

bool storage::remove(string const& uri)
{
    if (false == m_pimpl->map.contains(uri))
        return false;

    beltpp::on_failure guard([this]
    {
        m_pimpl->map.discard();
    });
    m_pimpl->map.erase(uri);
    m_pimpl->map.save();

    guard.dismiss();
    m_pimpl->map.commit();

    return true;
}

namespace detail
{
inline
beltpp::void_unique_ptr get_putl_types()
{
    beltpp::message_loader_utility utl;
    StorageTypes::detail::extension_helper(utl);

    auto ptr_utl =
        beltpp::new_void_unique_ptr<beltpp::message_loader_utility>(std::move(utl));

    return ptr_utl;
}

class storage_controller_internals
{
public:
    storage_controller_internals(filesystem::path const& path)
        : map("file_requests", path, 100, get_putl_types())
    {}

    meshpp::map_loader<StorageTypes::FileRequest> map;
    unordered_map<string, unordered_set<string>> channels_files_requesting;
};

}

storage_controller::storage_controller(boost::filesystem::path const& path_storage)
    : m_pimpl(path_storage.empty() ? nullptr : new detail::storage_controller_internals(path_storage))
{}
storage_controller::~storage_controller()
{}


void storage_controller::save()
{
    if (nullptr == m_pimpl)
        return;
    m_pimpl->map.save();
}

void storage_controller::commit()
{
    if (nullptr == m_pimpl)
        return;
    m_pimpl->map.commit();
}

void storage_controller::discard()
{
    if (nullptr == m_pimpl)
        return;
    m_pimpl->map.discard();
}

void storage_controller::clear()
{
    if (nullptr == m_pimpl)
        return;
    m_pimpl->map.clear();
}

void storage_controller::enqueue(string const& file_uri, string const& channel_address)
{
    if (nullptr == m_pimpl)
        return;

    StorageTypes::FileRequest fr;
    fr.file_uri = file_uri;
    fr.channel_address = channel_address;
    m_pimpl->map.insert(file_uri, fr);
}

void storage_controller::pop(string const& file_uri, string const& channel_address)
{
    if (nullptr == m_pimpl)
        return;

    auto const& fr = m_pimpl->map.as_const().at(file_uri);
    if (fr.channel_address == channel_address)
        m_pimpl->map.erase(file_uri);
}

bool storage_controller::initiate(string const& file_uri,
                                  string const& channel_address,
                                  initiate_type e_initiate_type)
{
    if (nullptr == m_pimpl)
        return false;

    auto it_channel = m_pimpl->channels_files_requesting.find(channel_address);
    if (it_channel == m_pimpl->channels_files_requesting.end())
        return false;

    auto it_file = it_channel->second.find(file_uri);
    if (it_file == it_channel->second.end())
        return false;

    if (e_initiate_type == revert)
    {
        it_channel->second.erase(it_file);
        if (it_channel->second.empty())
            m_pimpl->channels_files_requesting.erase(it_channel);
    }

    return true;
}

unordered_map<string, string> storage_controller::get_file_requests(unordered_set<string> const& resolved_channels)
{
    unordered_map<string, string> file_to_channel;
    if (nullptr == m_pimpl)
        return file_to_channel;

    auto file_uris = m_pimpl->map.as_const().keys();

    unordered_set<string> unresolved_channels;

    for (auto const& file_uri : file_uris)
    {
        auto const& file_request = m_pimpl->map.as_const().at(file_uri);

        if (0 == resolved_channels.count(file_request.channel_address))
        {
            if (10 == unresolved_channels.size())
            {
                if (0 == unresolved_channels.count(file_request.channel_address))
                    m_pimpl->map.erase(file_uri);
            }
            else
                unresolved_channels.insert(file_request.channel_address);

            continue;
        }

        if (STORAGE_MAX_FILE_REQUESTS == m_pimpl->channels_files_requesting.size() &&
            0 == m_pimpl->channels_files_requesting.count(file_request.channel_address))
            continue;

        auto insert_res = m_pimpl->channels_files_requesting[file_request.channel_address].insert({file_uri, false});

        if (insert_res.second)
            file_to_channel[file_uri] = file_request.channel_address;
    }

    return file_to_channel;
}

}
