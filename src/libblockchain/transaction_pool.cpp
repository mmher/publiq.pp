#include "transaction_pool.hpp"

#include "data.hpp"
#include "message.hpp"

#include <mesh.pp/cryptoutility.hpp>
#include <mesh.pp/fileutility.hpp>

#include <string>
#include <algorithm>

namespace filesystem = boost::filesystem;
using std::string;
using std::vector;
using hash_index_loader = meshpp::file_loader<Data::StringSet, 
                                              &Data::StringSet::string_loader,
                                              &Data::StringSet::string_saver>;
using hash_index_locked_loader = meshpp::file_locker<hash_index_loader>;
using transaction_data_loader = meshpp::file_loader<BlockchainMessage::TransactionFileData,
                                                    &BlockchainMessage::TransactionFileData::string_loader,
                                                    &BlockchainMessage::TransactionFileData::string_saver>;

namespace publiqpp
{

namespace detail
{
class transaction_pool_internals
{
public:
    transaction_pool_internals(filesystem::path const& path)
        : m_path(path)
        , m_index(path / "transactions.tpool.index")
    {

    }

    filesystem::path m_path;
    hash_index_locked_loader m_index;

    string get_df_id(string const& hash) const
    {
        //string s = hash.substr(hash.size()-3, 3);
        //int i = 10000 + std::stoi(s, 0, 16);
        //
        //s = std::to_string(i);
        //
        //return s.substr(1, 4);

        return "0000"; //TODO later
    }
};
}

transaction_pool::transaction_pool(filesystem::path const& fs_transaction_pool)
    : m_pimpl(new detail::transaction_pool_internals(fs_transaction_pool))
{

}
transaction_pool::~transaction_pool()
{

}

void transaction_pool::insert(beltpp::packet const& packet)
{
    if (packet.type() != BlockchainMessage::Transfer::rtt)
        throw std::runtime_error("Unknown object typeid to insert: " + std::to_string(packet.type()));

    vector<char> packet_vec = packet.save();
    string packet_hash = meshpp::hash(packet_vec.begin(), packet_vec.end());
    string hash_id = m_pimpl->get_df_id(packet_hash);
    string file_name("df" + hash_id + ".tpool");

    m_pimpl->m_index->dictionary.insert(packet_hash);
    transaction_data_loader file_data(m_pimpl->m_path / file_name);

    BlockchainMessage::Transfer transfer;
    packet.get(transfer);

    file_data->transactions[packet_hash] = transfer;
    
    file_data.save();
    m_pimpl->m_index.save();
}

bool transaction_pool::at(string const& key, beltpp::packet& transaction) const
{
    if (m_pimpl->m_index->dictionary.find(key) == m_pimpl->m_index->dictionary.end())
        return false;

    string hash_id = m_pimpl->get_df_id(key);
    string file_name("df" + hash_id + ".tpool");

    transaction_data_loader file_data(m_pimpl->m_path / file_name);
    transaction = file_data.as_const()->transactions.at(key);
    
    return true;
}

bool transaction_pool::remove(string const& key)
{
    if (m_pimpl->m_index->dictionary.find(key) == m_pimpl->m_index->dictionary.end())
        return false;

    string hash_id = m_pimpl->get_df_id(key);
    string file_name("df" + hash_id + ".tpool");

    m_pimpl->m_index->dictionary.erase(key);
    transaction_data_loader file_data(m_pimpl->m_path / file_name);
    file_data->transactions.erase(key);

    file_data.save();
    m_pimpl->m_index.save();

    return true;
}

}
