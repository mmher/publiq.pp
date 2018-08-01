﻿#include "../libblockchain/message.hpp"

#include <belt.pp/socket.hpp>

#include <iostream>
#include <chrono>

using namespace BlockchainMessage;
using peer_id = beltpp::socket::peer_id;

using std::cout;
using std::endl;
namespace chrono = std::chrono;
using std::chrono::system_clock;

peer_id openChannel(char** argv, beltpp::socket& sk, beltpp::event_handler& evH);
void SendReceive(beltpp::packet evType, beltpp::socket& sk, peer_id channel_id,
                                                beltpp::event_handler& evH);

void Send(beltpp::packet& send_package,
          beltpp::packet& receive_package,
          beltpp::socket& sk,
          peer_id peerid,
          beltpp::event_handler& eh);

//  MSVS does not instansiate template function only because its address
//  is needed, so let's force it
template beltpp::void_unique_ptr beltpp::new_void_unique_ptr<Error>();
template beltpp::void_unique_ptr beltpp::new_void_unique_ptr<Join>();
template beltpp::void_unique_ptr beltpp::new_void_unique_ptr<Drop>();

using sf = beltpp::socket_family_t<
    Error::rtt,
    Join::rtt,
    Drop::rtt,
    &beltpp::new_void_unique_ptr<Error>,
    &beltpp::new_void_unique_ptr<Join>,
    &beltpp::new_void_unique_ptr<Drop>,
    &Error::pvoid_saver,
    &Join::pvoid_saver,
    &Drop::pvoid_saver,
    &message_list_load
>;

int main(int argc, char** argv)
{
    beltpp::socket::peer_id peerid;
    beltpp::event_handler eh;

    beltpp::socket sk = beltpp::getsocket<sf>(eh);
    eh.add(sk);

    peerid = openChannel(argv, sk, eh);

    beltpp::packet send_package;
    beltpp::packet receive_package;

    cout << endl << peerid << endl;

    KeyPairRequest key_pair_request;
    key_pair_request.index = 0;
    key_pair_request.master_key = "TEST";

    send_package.set(key_pair_request);
    Send(send_package, receive_package, sk, peerid, eh);
    cout << endl << receive_package.to_string() << endl;

    KeyPair key_pair;
    receive_package.get(key_pair);

    Transfer transfer;
    transfer.from = key_pair.public_key;
    transfer.to = key_pair.public_key;
    transfer.amount = 0;

    Transaction transaction;
    transaction.creation.tm = system_clock::to_time_t(system_clock::now());
    transaction.expiry.tm = system_clock::to_time_t(system_clock::now() + chrono::hours(24));
    transaction.fee = 0;
    transaction.action = transfer;

    SignRequest sign_request;
    sign_request.private_key = key_pair.private_key;
    sign_request.package = transaction;

    send_package.set(sign_request);
    Send(send_package, receive_package, sk, peerid, eh);
    cout << endl << receive_package.to_string() << endl;

    Signature signature;
    receive_package.get(signature);

    SignedTransaction signed_transaction;
    signed_transaction.authority = key_pair.public_key;
    signed_transaction.signature = signature.signature;
    signed_transaction.transaction_details = transaction;

    Broadcast broadcast;
    broadcast.echoes = 2;
    broadcast.package = signed_transaction;
    
    send_package.set(broadcast);
    Send(send_package, receive_package, sk, peerid, eh);
    cout << endl << receive_package.to_string() << endl;

    return 0;
}

void Send(beltpp::packet& send_package,
          beltpp::packet& receive_package,
          beltpp::socket& sk,
          peer_id peerid,
          beltpp::event_handler& eh)
{
       sk.send(peerid, std::move(send_package));
    
       while (true)
       {
           beltpp::isocket::packets packets;
           std::unordered_set<beltpp::ievent_item const*> set_items;
    
           if (beltpp::ievent_handler::wait_result::event == eh.wait(set_items))
               packets = sk.receive(peerid);
    
           if (peerid.empty() || packets.empty())
               continue;
    
           ::detail::assign_packet(receive_package, packets.front());
           
           break;
       }
}

peer_id openChannel(char** argv, beltpp::socket& sk, beltpp::event_handler& eh)
{
    beltpp::ip_address address_item;
    address_item.from_string(argv[1]);
    beltpp::ip_address open_address("", 0, 
                                    address_item.local.address,
                                    address_item.local.port,
                                    beltpp::ip_address::e_type::ipv4);
    sk.open(open_address);

    peer_id channel_id;
    beltpp::isocket::packets pcs;
    std::unordered_set<beltpp::ievent_item const*> set_items;
    while(true)
    {
        if (beltpp::ievent_handler::wait_result::event == eh.wait(set_items))
            pcs = sk.receive(channel_id);

        if (channel_id.empty() && pcs.empty())
            continue;
        
        break;
    }

    return channel_id;
}

void SendReceive(beltpp::packet evType, beltpp::socket& sk, peer_id channel_id,
                                                    beltpp::event_handler& eh)
{
    sk.send(channel_id, std::move(evType));

    while(true)
    {
        beltpp::isocket::packets pcs;
        std::unordered_set<beltpp::ievent_item const*> set_items;
        if (beltpp::ievent_handler::wait_result::event == eh.wait(set_items))
            pcs = sk.receive(channel_id);
        if (channel_id.empty() || pcs.empty())
            continue;
        for(auto &pc:pcs)
        {
            switch(pc.type())
            {
            case Join::rtt:
                std::cout << "The event type is Join: " << endl << endl;
                break;
            case LoggedTransactions::rtt:
            {
                LoggedTransactions logTrans;
                pc.get(logTrans);

                uint64_t index{0};
                if(logTrans.actions.size() > 0)
                   index = logTrans.actions[0].index;
                cout << "The start_index is = " << index << endl << endl;
                break;
            }
            case ChainInfo::rtt:
            {
                ChainInfo chainInfo;
                pc.get(chainInfo);
                cout << "The ChainInfo length = " << chainInfo.length << endl << endl;
                break;
            }
            case KeyPair::rtt:
            {
                KeyPair keyP;
                pc.get(keyP);
                cout << "The KeyPair index = " << keyP.index << endl;
                cout << "The KeyPair public_key = " << keyP.public_key << endl;
                cout << "The KeyPair private_key = " << keyP.private_key << endl;
                cout << "The KeyPair master_key = " << keyP.master_key << endl << endl;
                break;
            }
            case Digest::rtt:
            {
                Digest digest;
                pc.get(digest);
                cout << "The Digest base58_hash = " << digest.base58_hash << endl << endl;
                break;
            }
            case Signature::rtt:
            {
                Signature sign;
                pc.get(sign);
                cout << "The Signature public_key = " << sign.public_key << endl;
                cout << "The Signature signature = " << sign.signature << endl << endl;
                break;
            }
            case RemoteError::rtt:
                //RemoteError error;
                //pc.get(error);
                //cout << "RemoteError: " << rError.message <<endl;
                cout << "Received a RemoteError!!!" << endl << endl;
                break;
            case Drop::rtt:
                cout << "The process was Dropped!" << endl << endl;
                break;
            case Done::rtt:
                cout << "The Send-Receive process was Shutted Down!" << endl << endl;
                break;
            default:
                cout << "Nothing for receive.!" << endl;
                break;
            }
        }
        break;
    }
}

