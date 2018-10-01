#pragma once

#include "coin.hpp"
#include "message.hpp"

// Blocks and headers max count per one request - 1
// corners are included
#define BLOCK_TR_LENGTH 9
#define HEADER_TR_LENGTH 49

// Maximum buffer length of blocks
// which can be cillected for sync
#define BLOCK_INSERT_LENGTH 50

// Block mine delay in seconds
#define BLOCK_MINE_DELAY 600
#define BLOCK_WAIT_DELAY 180

// Sync process request/response maximum dely
#define SYNC_FAILURE_TIMEOUT 30

// Sent packet will considered as not answered
// after PACKET_EXPIRY_STEPS x EVENT_TIMER seconds
#define PACKET_EXPIRY_STEPS 10

// Block maximum transactions count
#define BLOCK_MAX_TRANSACTIONS 1000

// Action log max response count
#define ACTION_LOG_MAX_RESPONSE 10000

// Timers in seconds
#define CHECK_TIMER 1
#define SYNC_TIMER  15
#define EVENT_TIMER 30
#define BROADCAST_TIMER 1800
#define CACHE_CLEANUP_TIMER 300
#define SUMMARY_REPORT_TIMER 600

// Transaction maximum lifetime in seconds (24 hours)
#define TRANSACTION_LIFETIME 86400

// Maximum time shift on seconds
// acceptable between nodes
#define NODES_TIME_SHIFT 60

// Consensus delta definitions
#define DELTA_STEP  10ull
#define DELTA_MAX   120000000ull
#define DELTA_UP    100000000ull
#define DELTA_DOWN  80000000ull

#define DIST_MAX    4294967296ull

static const coin MINE_AMOUNT_THRESHOLD(1, 0);
static const coin MINE_REWARD_BUDGET(250000000ull, 0);

#define MINE_INIT_PART      0.2
#define MINE_YEAR_STEP      52596

namespace publiqpp
{
namespace detail
{
inline
beltpp::void_unique_ptr get_putl()
{
    beltpp::message_loader_utility utl;
    BlockchainMessage::detail::extension_helper(utl);

    auto ptr_utl =
        beltpp::new_void_unique_ptr<beltpp::message_loader_utility>(std::move(utl));

    return ptr_utl;
}
}
}
