#include "transaction_contentunit.hpp"
#include "common.hpp"
#include "node_internals.hpp"
#include "exception.hpp"

#include <mesh.pp/cryptoutility.hpp>

using namespace BlockchainMessage;

namespace publiqpp
{
void action_validate(SignedTransaction const& signed_transaction,
                     ContentUnit const& content_unit,
                     bool check_complete)
{
    //  this is checked in signed_transaction_validate
    assert(false == signed_transaction.authorizations.empty());

    if (content_unit.author_addresses.empty())
        throw wrong_data_exception("a content unit has to have at least one author");

    if (check_complete)
    {
        if (signed_transaction.authorizations.size() > content_unit.author_addresses.size())
            throw authority_exception(signed_transaction.authorizations.back().address, string());
        else if (signed_transaction.authorizations.size() < content_unit.author_addresses.size())
            throw authority_exception(string(), content_unit.author_addresses.back());

        for (size_t index = 0; index != signed_transaction.authorizations.size(); ++index)
        {
            auto const& signer = signed_transaction.authorizations[index].address;
            auto const& author = content_unit.author_addresses[index];

            meshpp::public_key pb_key_author(author);

            if (signer != author)
                throw authority_exception(signer, author);
        }
    }
    else
    {
        for (size_t index = 0;
             index != signed_transaction.authorizations.size() &&
             index != content_unit.author_addresses.size();
             ++index)
        {
            auto const& signer = signed_transaction.authorizations[index].address;
            auto const& author = content_unit.author_addresses[index];

            meshpp::public_key pb_key_author(author);

            if (signer != author)
                throw authority_exception(signer, author);
        }
    }
    meshpp::public_key pb_key_channel(content_unit.channel_address);

    string unit_hash = meshpp::from_base58(content_unit.uri);
    if (unit_hash.length() != 32)
        throw uri_exception(content_unit.uri, uri_exception::invalid);

    for (auto const& file_uri : content_unit.file_uris)
    {
        string file_hash = meshpp::from_base58(file_uri);
        if (file_hash.length() != 32)
            throw uri_exception(file_uri, uri_exception::invalid);
    }
}

authorization_process_result action_authorization_process(SignedTransaction& signed_transaction,
                                                          ContentUnit const& content_unit)
{
    authorization_process_result code;
    code.complete = (signed_transaction.authorizations.size() == content_unit.author_addresses.size());
    code.modified = false;

    return code;
}

bool action_can_apply(publiqpp::detail::node_internals const& impl,
                      ContentUnit const& content_unit)
{
    if (impl.m_documents.exist_unit(content_unit.uri))
        return false;

    for (auto const& uri : content_unit.file_uris)
    {
        if (false == impl.m_documents.exist_file(uri))
            return false;
    }
    return true;
}

void action_apply(publiqpp::detail::node_internals& impl,
                  ContentUnit const& content_unit)
{
    if (impl.m_documents.exist_unit(content_unit.uri))
        throw uri_exception(content_unit.uri, uri_exception::duplicate);

    for (auto const& uri : content_unit.file_uris)
    {
        if (false == impl.m_documents.exist_file(uri))
            throw uri_exception(uri, uri_exception::missing);
    }

    impl.m_documents.insert_unit(content_unit);
}

void action_revert(publiqpp::detail::node_internals& impl,
                   ContentUnit const& content_unit)
{
    impl.m_documents.remove_unit(content_unit.uri);
}
}
