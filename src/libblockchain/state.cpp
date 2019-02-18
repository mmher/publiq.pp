#include "state.hpp"
#include "common.hpp"
#include "exception.hpp"

#include <mesh.pp/fileutility.hpp>

using namespace BlockchainMessage;
namespace filesystem = boost::filesystem;

using std::string;

namespace publiqpp
{
namespace detail
{
class state_internals
{
public:
    state_internals(filesystem::path const& path)
        : m_accounts("account", path, 10000, detail::get_putl())
        , m_contracts("contract", path, 10, detail::get_putl())
    {}

    meshpp::map_loader<Coin> m_accounts;
    meshpp::map_loader<Contract> m_contracts;
};
}

state::state(filesystem::path const& fs_state)
    : m_pimpl(new detail::state_internals(fs_state))
{
}

state::~state() = default;

void state::save()
{
    m_pimpl->m_accounts.save();
    m_pimpl->m_contracts.save();
}

void state::commit()
{
    m_pimpl->m_accounts.commit();
    m_pimpl->m_contracts.commit();
}

void state::discard()
{
    m_pimpl->m_accounts.discard();
    m_pimpl->m_contracts.discard();
}

Coin state::get_balance(string const& key) const
{
    if (m_pimpl->m_accounts.as_const().contains(key))
        return m_pimpl->m_accounts.as_const().at(key);

    return Coin(); // all accounts not included have 0 balance
}

void state::apply_transfer(Transfer const& transfer, Coin const& fee)
{
    if (coin(transfer.amount).empty())
        throw std::runtime_error("0 amount transfer is not allowed!");

    Coin balance = get_balance(transfer.from);
    if (coin(balance) < transfer.amount + fee)
        throw not_enough_balance_exception(coin(balance), transfer.amount + fee);

    // decrease "from" balance
    decrease_balance(transfer.from, transfer.amount);
    
    // increase "to" balance
    increase_balance(transfer.to, transfer.amount);
}

void state::increase_balance(string const& key, coin const& amount)
{
    if (amount.empty())
        return;

    if (m_pimpl->m_accounts.contains(key))
    {
        Coin& balance = m_pimpl->m_accounts.at(key);
        (balance + amount).to_Coin(balance);
    }
    else
    {
        Coin temp;
        amount.to_Coin(temp);
        m_pimpl->m_accounts.insert(key, temp);
    }
}

void state::decrease_balance(string const& key, coin const& amount)
{
    if (amount.empty())
        return;

    if (!m_pimpl->m_accounts.contains(key))
        throw not_enough_balance_exception(coin(), amount);

    Coin& balance = m_pimpl->m_accounts.at(key);

    if (coin(balance) < amount)
        throw not_enough_balance_exception(coin(balance), amount);

    coin(balance - amount).to_Coin(balance);

    if (coin(balance).empty())
        m_pimpl->m_accounts.erase(key);
}

void state::get_contracts(std::vector<Contract>& contracts, NodeType const& role) const
{
    contracts.clear();

    for (auto& key : m_pimpl->m_contracts.as_const().keys())
    {
        Contract contract = m_pimpl->m_contracts.as_const().at(key);
        
        if (role == contract.role)
            contracts.push_back(contract);
    }
}

NodeType state::get_contract_type(string const& key) const
{
    if (m_pimpl->m_contracts.as_const().contains(key))
        return m_pimpl->m_contracts.as_const().at(key).role;

    return NodeType::miner;
}

void state::insert_contract(Contract const& contract)
{
    if (contract.role == NodeType::miner)
        throw std::runtime_error("TODO");

    if(get_contract_type(contract.owner) != NodeType::miner)
        throw std::runtime_error("TODO");

    m_pimpl->m_contracts.insert(contract.owner, contract);
}

void state::remove_contract(Contract const& contract)
{
    m_pimpl->m_contracts.erase(contract.owner);
}

}

