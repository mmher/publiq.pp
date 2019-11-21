#pragma once

#include "types.hpp"

#include <belt.pp/parser.hpp>
#include <belt.pp/http.hpp>

#include <mesh.pp/cryptoutility.hpp>

#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <chrono>

using std::string;
using std::vector;
using std::pair;
using std::unordered_map;

namespace storage_utilitypp
{
namespace http
{
inline
string response(beltpp::detail::session_special_data& ssd,
                beltpp::packet const& pc)
{
    return beltpp::http::http_response(ssd, pc.to_string());
}
//inline
//string file_response(beltpp::detail::session_special_data& ssd,
//                     beltpp::packet const& pc)
//{
//    ssd.session_specal_handler = nullptr;

//    if (pc.type() == BlockchainMessage::StorageFile::rtt)
//    {
//        string str_result;
//        BlockchainMessage::StorageFile const* pFile = nullptr;
//        pc.get(pFile);

//        str_result += "HTTP/1.1 200 OK\r\n";
//        if (false == pFile->mime_type.empty())
//            str_result += "Content-Type: " + pFile->mime_type + "\r\n";
//        str_result += "Access-Control-Allow-Origin: *\r\n";
//        str_result += "Content-Length: ";
//        str_result += std::to_string(pFile->data.length());
//        str_result += "\r\n\r\n";
//        str_result += pFile->data;

//        return str_result;
//    }
//    else
//    {
//        string str_result;
//        string message;
//        if (pc.type() == BlockchainMessage::UriError::rtt)
//        {
//            BlockchainMessage::UriError const* pError = nullptr;
//            pc.get(pError);
//            if (pError->uri_problem_type == BlockchainMessage::UriProblemType::missing)
//                message = "404 Not Found\r\n"
//                          "requested file: " + pError->uri;
//        }

//        if (message.empty())
//            message = "internal error";

//        str_result += "HTTP/1.1 404 Not Found\r\n";
//        str_result += "Content-Type: text/plain\r\n";
//        str_result += "Access-Control-Allow-Origin: *\r\n";
//        str_result += "Content-Length: " + std::to_string(message.length()) + "\r\n\r\n";
//        str_result += message;
//        return str_result;
//    }
//}

template <beltpp::detail::pmsg_all (*fallback_message_list_load)(
        std::string::const_iterator&,
        std::string::const_iterator const&,
        beltpp::detail::session_special_data&,
        void*)>
beltpp::detail::pmsg_all message_list_load(
        std::string::const_iterator& iter_scan_begin,
        std::string::const_iterator const& iter_scan_end,
        beltpp::detail::session_special_data& ssd,
        void* putl)
{
    auto it_fallback = iter_scan_begin;

    ssd.session_specal_handler = nullptr;
    ssd.autoreply.clear();

    auto protocol_error = [&iter_scan_begin, &iter_scan_end, &ssd]()
    {
        ssd.ptr_data = beltpp::t_unique_nullptr<beltpp::detail::iscan_status>();
        ssd.session_specal_handler = nullptr;
        ssd.autoreply.clear();
        iter_scan_begin = iter_scan_end;
        return ::beltpp::detail::pmsg_all(size_t(-2),
                                          ::beltpp::void_unique_nullptr(),
                                          nullptr);
    };

    string posted;
    auto code = beltpp::http::protocol(ssd,
                                       iter_scan_begin,
                                       iter_scan_end,
                                       it_fallback,
                                       1000,
                                       64 * 1024,
                                       10 * 1024 * 1024,
                                       posted);

    beltpp::http::detail::scan_status* pss =
            dynamic_cast<beltpp::http::detail::scan_status*>(ssd.ptr_data.get());

    if (code == beltpp::e_three_state_result::error &&
        (nullptr == pss ||
         pss->status == beltpp::http::detail::scan_status::clean)
        )
    {
        if (pss)
        {
            ssd.parser_unrecognized_limit = pss->parser_unrecognized_limit_backup;
            ssd.ptr_data = beltpp::t_unique_nullptr<beltpp::detail::iscan_status>();
        }

        return fallback_message_list_load(iter_scan_begin, iter_scan_end, ssd, putl);
    }
    else if (code == beltpp::e_three_state_result::error)
        return protocol_error();
    else if (code == beltpp::e_three_state_result::attempt)
    {
        iter_scan_begin = it_fallback;
        return ::beltpp::detail::pmsg_all(size_t(-1),
                                          ::beltpp::void_unique_nullptr(),
                                          nullptr);
    }
    else// if (code == beltpp::e_three_state_result::success)
    {
        ssd.session_specal_handler = &response;
        ssd.autoreply.clear();

        if (pss->type == beltpp::http::detail::scan_status::post &&
            pss->resource.path.size() == 1 &&
            pss->resource.path.front() == "api")
        {
            std::string::const_iterator iter_scan_begin_temp = posted.cbegin();
            std::string::const_iterator const iter_scan_end_temp = posted.cend();

            ssd.ptr_data = beltpp::t_unique_nullptr<beltpp::detail::iscan_status>();
            ssd.parser_unrecognized_limit = 0;

            return fallback_message_list_load(iter_scan_begin_temp, iter_scan_end_temp, ssd, putl);
        }
        else if (pss->type == beltpp::http::detail::scan_status::get &&
                 pss->resource.path.size() == 1 &&
                 pss->resource.path.front() == "protocol")
        {
            ssd.session_specal_handler = nullptr;

            ssd.autoreply = beltpp::http::http_response(ssd, StorageTypes::detail::storage_json_schema());

            ssd.ptr_data = beltpp::t_unique_nullptr<beltpp::detail::iscan_status>();

            return ::beltpp::detail::pmsg_all(size_t(-1),
                                              ::beltpp::void_unique_nullptr(),
                                              nullptr);
        }
        else
        {
            ssd.session_specal_handler = nullptr;

            string message("noo! \r\n");

            for (auto const& dir : pss->resource.path)
                message += "/" + dir;
            message += "\r\n";
            for (auto const& arg : pss->resource.arguments)
                message += (arg.first + ": " + arg.second + "\r\n");
            message += "\r\n";
            message += "\r\n";
            for (auto const& prop : pss->resource.properties)
                message += (prop.first + ": " + prop.second + "\r\n");
            message += "that's an error! \r\n";
            message += "here's the protocol, by the way \r\n";

            ssd.autoreply = beltpp::http::http_not_found(ssd,
                                                         message +
                                                         StorageTypes::detail::storage_json_schema());

            ssd.ptr_data = beltpp::t_unique_nullptr<beltpp::detail::iscan_status>();

            return ::beltpp::detail::pmsg_all(size_t(-1),
                                              ::beltpp::void_unique_nullptr(),
                                              nullptr);
        }
    }
}
}
}
