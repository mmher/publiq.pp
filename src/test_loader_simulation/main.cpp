#include "../libblockchain/message.hpp"

#include <belt.pp/global.hpp>

#include <mesh.pp/fileutility.hpp>

#include <iostream>
#include <chrono>
#include <string>

using namespace BlockchainMessage;

using std::cout;
using std::endl;
namespace chrono = std::chrono;
using std::chrono::steady_clock;
using std::string;

inline
beltpp::void_unique_ptr get_putl()
{
    beltpp::message_loader_utility utl;
    BlockchainMessage::detail::extension_helper(utl);

    auto ptr_utl =
        beltpp::new_void_unique_ptr<beltpp::message_loader_utility>(std::move(utl));

    return ptr_utl;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 4)
        {
            cout << "usage: test_loader_simulation folder index_from index_to" << endl;
            return 0;
        }

        string path = argv[1];
        size_t pos;
        size_t index_from = size_t(beltpp::stoi64(argv[2], pos));
        size_t index_to = size_t(beltpp::stoi64(argv[3], pos));

        cout << "path: " << path << endl;
        cout << "index from: " << index_from << endl;
        cout << "index to: " << index_to << endl;

        auto tp_start = steady_clock::now();

        meshpp::map_loader<Coin> actions("actions", path, get_putl());

        Coin a;// = actions.at("0");
        a.fraction = 4040;

        actions.erase("0");
        actions.erase("2");
        actions.erase("3");

        cout << a.to_string() << endl;

        actions.save();
        actions.commit();

        auto duration = steady_clock::now() - tp_start;
        auto  ms_duration = chrono::duration_cast<chrono::milliseconds>(duration);
        cout << ms_duration.count() << " ms" << endl;
    }
    catch(std::exception const& e)
    {
        cout << "exception: " << e.what() << endl;
    }

    return 0;
}
