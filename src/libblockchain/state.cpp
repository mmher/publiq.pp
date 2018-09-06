#include "state.hpp"
#include "common.hpp"

#include <mesh.pp/fileutility.hpp>

using namespace BlockchainMessage;
namespace filesystem = boost::filesystem;

using std::string;
using std::vector;

namespace publiqpp
{
namespace detail
{
class state_internals
{
public:
    state_internals(filesystem::path const& path)
        :m_accounts("account", path, 10000, detail::get_putl())
    {}

    meshpp::map_loader<Coin> m_accounts;
};
}

state::state(filesystem::path const& fs_state)
    : m_pimpl(new detail::state_internals(fs_state))
{
}

state::~state()
{
}

void state::save()
{
    m_pimpl->m_accounts.save();
}

void state::commit()
{
    m_pimpl->m_accounts.commit();
}

void state::discard()
{
    m_pimpl->m_accounts.discard();
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
        throw low_balance_exception(transfer.from);

    // decrease "from" balance
    decrease_balance(transfer.from, transfer.amount);
    
    // increase "to" balance
    increase_balance(transfer.to, transfer.amount);
}

void state::apply_reward(Reward const& reward)
{
    if (coin(reward.amount).empty())
        throw std::runtime_error("0 amount reward is humiliatingly!");

    increase_balance(reward.to, reward.amount);
}

void state::increase_balance(string const& key, coin const& amount)
{
    if (amount.empty())
        return;

    if (m_pimpl->m_accounts.contains(key))
    {
        Coin& balance = m_pimpl->m_accounts.at(key);
        balance = (balance + amount).to_Coin();
    }
    else
        m_pimpl->m_accounts.insert(key, amount.to_Coin());
}

void state::decrease_balance(string const& key, coin const& amount)
{
    if (amount.empty())
        return;

    if (!m_pimpl->m_accounts.contains(key))
        throw low_balance_exception(key);

    Coin& balance = m_pimpl->m_accounts.at(key);

    if (coin(balance) < amount)
        throw low_balance_exception(key);

    balance = coin(balance - amount).to_Coin();

    if (coin(balance).empty())
        m_pimpl->m_accounts.erase(key);
}

uint64_t state::get_fraction()
{
    uint64_t fraction = 0;

    auto keys = m_pimpl->m_accounts.keys();

    for (auto key : keys)
    {
        Coin balance = m_pimpl->m_accounts.as_const().at(key);
        fraction += balance.fraction;
    }

    return fraction;
}

}

//---------------- Exceptions -----------------------
low_balance_exception::low_balance_exception(string const& _account)
    : runtime_error("Low balance! account: " + _account)
    , account(_account)
{}
low_balance_exception::low_balance_exception(low_balance_exception const& other) noexcept
    : runtime_error(other)
    , account(other.account)
{}
low_balance_exception& low_balance_exception::operator=(low_balance_exception const& other) noexcept
{
    dynamic_cast<runtime_error*>(this)->operator =(other);
    account = other.account;
    return *this;
}
low_balance_exception::~low_balance_exception() noexcept
{}

