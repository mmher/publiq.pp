#include "transaction_content.hpp"
#include "common.hpp"
#include "node_internals.hpp"
#include "exception.hpp"

#include <mesh.pp/cryptoutility.hpp>

using namespace BlockchainMessage;

namespace publiqpp
{
void action_validate(SignedTransaction const& signed_transaction,
                     Content const& content)
{
    if (signed_transaction.authority != content.channel_address)
        throw authority_exception(signed_transaction.authority, content.channel_address);

    meshpp::public_key pb_key_channel(content.channel_address);

    for (auto const& uri : content.content_unit_uris)
    {
        string unit_hash = meshpp::from_base58(uri);
        if (unit_hash.length() != 32)
            throw std::runtime_error("invalid uri: " + uri);
    }
}

bool action_can_apply(std::unique_ptr<publiqpp::detail::node_internals> const& pimpl,
                      Content const& content)
{
    for (auto const& uri : content.content_unit_uris)
    {
        if (false == pimpl->m_documents.exist_unit(uri))
            return false;
    }
    return true;
}

void action_apply(std::unique_ptr<publiqpp::detail::node_internals>& pimpl,
                  Content const& content)
{
    for (auto const& uri : content.content_unit_uris)
    {
        if (false == pimpl->m_documents.exist_unit(uri))
            throw wrong_document_exception("Missing content_unit with uri: " + uri);
    }
}

void action_revert(std::unique_ptr<publiqpp::detail::node_internals>& /*pimpl*/,
                   Content const& /*content*/)
{
}
}