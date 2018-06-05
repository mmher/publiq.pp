#include "settings.hpp"

#include "pid.hpp"

#include <belt.pp/log.hpp>

#include <mesh.pp/fileutility.hpp>
#include <mesh.pp/processutility.hpp>

#include <publiq.pp/node.hpp>

#include <boost/program_options.hpp>
#include <boost/locale.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>

#include <memory>
#include <iostream>
#include <vector>
#include <sstream>
#include <chrono>
#include <memory>
#include <exception>

#include <signal.h>

namespace program_options = boost::program_options;

using std::unique_ptr;
using std::string;
using std::cout;
using std::endl;
using std::vector;
namespace chrono = std::chrono;
using chrono::steady_clock;
using std::unique_ptr;
using std::runtime_error;

bool process_command_line(int argc, char** argv,
                          beltpp::ip_address& p2p_bind_to_address,
                          vector<beltpp::ip_address>& p2p_connect_to_addresses,
                          beltpp::ip_address& rpc_bind_to_address,
                          string& data_directory,
                          string& greeting);

void add_port(Config::Port2PID& ob, unsigned short port)
{
    auto res = ob.reserved_ports.insert(std::make_pair(port, meshpp::current_process_id()));

    if (false == res.second)
    {
        string error = "port: ";
        error += std::to_string(res.first->first);
        error += " is locked by pid: ";
        error += std::to_string(res.first->second);
        throw runtime_error(error);
    }
}
void remove_port(Config::Port2PID& ob, unsigned short port)
{
    auto it = ob.reserved_ports.find(port);
    if (it == ob.reserved_ports.end())
        throw runtime_error("cannot find own port: " + std::to_string(port));

    ob.reserved_ports.erase(it);
}

bool g_termination_handled = false;
void termination_handler(int signum)
{
    g_termination_handled = true;
}


#include <cryptopp/sha.h>
#include <cryptopp/filters.h>
#include <cryptopp/base64.h>
#include <cryptopp/hex.h>

std::string SHA256HashString(std::string aString)
{
    byte digest[CryptoPP::SHA256::DIGESTSIZE];

    byte const* pb = (byte const*)aString.c_str();
    CryptoPP::SHA256().CalculateDigest(digest, pb, aString.length());

    CryptoPP::HexEncoder encoder;
    std::string output;

    encoder.Attach( new CryptoPP::StringSink( output ) );
    encoder.Put( digest, sizeof(digest) );
    encoder.MessageEnd();

    return output;
}

int main(int argc, char** argv)
{
    /*cout << SHA256HashString("a") << endl;
    return 0;*/
    //  boost filesystem UTF-8 support
    std::locale::global(boost::locale::generator().generate(""));
    boost::filesystem::path::imbue(std::locale());
    //
    settings::settings::set_application_name("publiqd");
    settings::settings::set_data_directory(settings::config_directory_path().string());

    beltpp::ip_address p2p_bind_to_address;
    beltpp::ip_address rpc_bind_to_address;
    vector<beltpp::ip_address> p2p_connect_to_addresses;
    string data_directory;
    string greeting;

    if (false == process_command_line(argc, argv,
                                      p2p_bind_to_address,
                                      p2p_connect_to_addresses,
                                      rpc_bind_to_address,
                                      data_directory,
                                      greeting))
        return 1;

    if (false == data_directory.empty())
        settings::settings::set_data_directory(data_directory);

    struct sigaction signal_handler;
    signal_handler.sa_handler = termination_handler;
    ::sigaction(SIGINT, &signal_handler, nullptr);
    ::sigaction(SIGINT, &signal_handler, nullptr);

    try
    {
        settings::create_config_directory();
        settings::create_data_directory();

        using port_toggler_type = void(*)(Config::Port2PID&, unsigned short);
        using FLPort2PID = meshpp::file_toggler<Config::Port2PID,
                                                port_toggler_type,
                                                port_toggler_type,
                                                &add_port,
                                                &remove_port,
                                                &Config::Port2PID::string_loader,
                                                &Config::Port2PID::string_saver,
                                                unsigned short>;

        FLPort2PID port2pid(settings::config_file_path("pid"), p2p_bind_to_address.local.port);

        using DataDirAttributeLoader = meshpp::file_locker<meshpp::file_loader<Config::DataDirAttribute, &Config::DataDirAttribute::string_loader, &Config::DataDirAttribute::string_saver>>;
        DataDirAttributeLoader dda(settings::data_file_path("running.txt"));
        Config::RunningDuration item;
        item.start.tm = item.end.tm = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        dda->history.push_back(item);
        dda.next_layer().commit();

        auto fs_blockchain = settings::data_directory_path("blockchain");

        cout << p2p_bind_to_address.to_string() << endl;
        for (auto const& item : p2p_connect_to_addresses)
            cout << item.to_string() << endl;

        beltpp::ilog_ptr plogger_p2p = beltpp::console_logger("exe_publiqd_p2p");
        plogger_p2p->disable();
        beltpp::ilog_ptr plogger_rpc = beltpp::console_logger("exe_publiqd_rpc");
        //plogger_rpc->disable();

        publiqpp::node node(rpc_bind_to_address,
                            p2p_bind_to_address,
                            p2p_connect_to_addresses,
                            fs_blockchain,
                            plogger_p2p.get(),
                            plogger_rpc.get());

        cout << endl;
        cout << "Node: " << node.name()/*.substr(0, 5)*/ << endl;
        cout << endl;

        while (true)
        {
            try
            {
            if (false == node.run())
                break;

            if (g_termination_handled)
                break;
            }
            catch (std::exception const& ex)
            {
                cout << "exception cought: " << ex.what() << endl;
            }
            catch (...)
            {
                cout << "always throw std::exceptions, will exit now" << endl;
                break;
            }
        }

        dda->history.back().end.tm = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        dda.next_layer().commit();
        port2pid.commit();
    }
    catch (std::exception const& ex)
    {
        cout << "exception cought: " << ex.what() << endl;
    }
    catch (...)
    {
        cout << "always throw std::exceptions" << endl;
    }
    return 0;
}

bool process_command_line(int argc, char** argv,
                          beltpp::ip_address& p2p_bind_to_address,
                          vector<beltpp::ip_address>& p2p_connect_to_addresses,
                          beltpp::ip_address& rpc_bind_to_address,
                          string& data_directory,
                          string& greeting)
{
    string p2p_local_interface;
    string rpc_local_interface;
    vector<string> hosts;
    program_options::options_description options_description;
    try
    {
        auto desc_init = options_description.add_options()
            ("help,h", "Print this help message and exit.")
            ("p2p_local_interface,i", program_options::value<string>(&p2p_local_interface)->required(),
                            "The local network interface and port to bind to")
            ("p2p_remote_host,p", program_options::value<vector<string>>(&hosts),
                            "Remote nodes addresss with port")
            ("rpc_local_interface,r", program_options::value<string>(&rpc_local_interface)->required(),
                            "The local network interface and port to bind to")
            ("data_directory,d", program_options::value<string>(&data_directory),
                            "Data directory path")
            ("greeting,g", program_options::value<string>(&greeting),
                            "send a greeting message to all peers");
        (void)(desc_init);

        program_options::variables_map options;

        program_options::store(
                    program_options::parse_command_line(argc, argv, options_description),
                    options);

        program_options::notify(options);

        if (options.count("help"))
        {
            throw std::runtime_error("");
        }

        p2p_bind_to_address.from_string(p2p_local_interface);
        rpc_bind_to_address.from_string(rpc_local_interface);
        for (auto const& item : hosts)
        {
            beltpp::ip_address address_item;
            address_item.from_string(item);
            p2p_connect_to_addresses.push_back(address_item);
        }
    }
    catch (std::exception const& ex)
    {
        std::stringstream ss;
        ss << options_description;

        string ex_message = ex.what();
        if (false == ex_message.empty())
            cout << ex.what() << endl << endl;
        cout << ss.str();
        return false;
    }
    catch (...)
    {
        cout << "always throw std::exceptions" << endl;
        return false;
    }

    return true;
}
