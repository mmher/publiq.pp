#include "blockchain.hpp"
#include "common.hpp"

#include <belt.pp/utility.hpp>

#include <mesh.pp/fileutility.hpp>
#include <mesh.pp/cryptoutility.hpp>

using namespace BlockchainMessage;
namespace filesystem = boost::filesystem;

namespace publiqpp
{
namespace detail
{
class blockchain_internals
{
public:
    blockchain_internals(filesystem::path const& path)
        : m_header("header", path, 1000, 1, detail::get_putl())
        , m_blockchain("block", path, 10000, 1, detail::get_putl())
    {
    }

    std::string m_last_hash;
    BlockHeader m_last_header;
    meshpp::vector_loader<BlockHeader> m_header;
    meshpp::vector_loader<SignedBlock> m_blockchain;
};
}

blockchain::blockchain(boost::filesystem::path const& fs_blockchain)
    : m_pimpl(new detail::blockchain_internals(fs_blockchain))
{
    if (length() > 0)
        update_state();
}

blockchain::~blockchain() = default;

void blockchain::save()
{
    m_pimpl->m_header.save();
    m_pimpl->m_blockchain.save();
}

void blockchain::commit()
{
    m_pimpl->m_header.commit();
    m_pimpl->m_blockchain.commit();
}

void blockchain::discard()
{
    m_pimpl->m_header.discard();
    m_pimpl->m_blockchain.discard();

    update_state();
}

void blockchain::update_state()
{
    SignedBlock signed_block;
    at(length() - 1, signed_block);

    m_pimpl->m_last_header = signed_block.block_details.header;
    m_pimpl->m_last_hash = meshpp::hash(signed_block.block_details.to_string());
}

uint64_t blockchain::length() const
{
    return m_pimpl->m_blockchain.as_const().size();
}

std::string blockchain::last_hash() const
{
    return m_pimpl->m_last_hash;
}

void blockchain::last_header(BlockHeader& block_header) const
{
    block_header = m_pimpl->m_last_header;
}

void blockchain::insert(SignedBlock const& signed_block)
{
    Block const& block = signed_block.block_details;

    uint64_t block_number = block.header.block_number;

    if (block_number != length())
        throw std::runtime_error("Wrong block to insert!");

    m_pimpl->m_header.push_back(block.header);
    m_pimpl->m_blockchain.push_back(signed_block);

    update_state();
}

void blockchain::at(uint64_t number, SignedBlock& signed_block) const
{
    signed_block = m_pimpl->m_blockchain.as_const().at(number);
}

void blockchain::header_at(uint64_t number, BlockHeader& block_header) const
{
    block_header = m_pimpl->m_header.as_const().at(number);
}

void blockchain::remove_last_block()
{
    if (length() == 1)
        throw std::runtime_error("Nothing to remove!");

    m_pimpl->m_header.pop_back();
    m_pimpl->m_blockchain.pop_back();

    update_state();
}
}
