# generate message.gen.hpp from message.idl
add_custom_command (
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/message.gen.hpp ${CMAKE_CURRENT_SOURCE_DIR}/message.gen.tmpl.hpp ${CMAKE_CURRENT_SOURCE_DIR}/message.gen.cpp.hpp
  MAIN_DEPENDENCY message.idl
  COMMAND idl ${CMAKE_CURRENT_SOURCE_DIR}/message.idl ${CMAKE_CURRENT_SOURCE_DIR}/message.gen splitting BLOCKCHAINSHARED_EXPORT
)
# generate types.gen.hpp from types.idl
add_custom_command (
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/types.gen.hpp
  MAIN_DEPENDENCY types.idl
  COMMAND idl ${CMAKE_CURRENT_SOURCE_DIR}/types.idl ${CMAKE_CURRENT_SOURCE_DIR}/types.gen
)

add_definitions(-DBLOCKCHAIN_LIBRARY)

# define the library
add_library(blockchain
    global.hpp
    action_log.cpp
    action_log.hpp
    blockchain.cpp
    blockchain.hpp
    communication_rpc.cpp
    communication_rpc.hpp
    communication_p2p.cpp
    communication_p2p.hpp
    coin.cpp
    coin.hpp
    common.hpp
    documents.cpp
    documents.hpp
    exception.hpp
    exception.cpp
    http.hpp
    message.cpp
    message.gen.cpp.hpp
    message.tmpl.hpp
    message.gen.tmpl.hpp
    message.hpp
    message.gen.hpp
    node.cpp
    node.hpp
    node_internals.cpp
    node_internals.hpp
    node_synchronization.cpp
    node_synchronization.hpp
    nodeid_service.cpp
    nodeid_service.hpp
    open_container_packet.hpp
    sessions.cpp
    sessions.hpp
    state.cpp
    state.hpp
    storage.cpp
    storage.hpp
    storage_node.cpp
    storage_node.hpp
    storage_node_internals.hpp
    transaction_content.cpp
    transaction_content.hpp
    transaction_storageupdate.cpp
    transaction_storageupdate.hpp
    transaction_contentunit.cpp
    transaction_contentunit.hpp
    transaction_file.cpp
    transaction_file.hpp
    transaction_handler.cpp
    transaction_handler.hpp
    transaction_pool.cpp
    transaction_pool.hpp
    transaction_role.cpp
    transaction_role.hpp
    transaction_sponsoring.cpp
    transaction_sponsoring.hpp
    transaction_statinfo.cpp
    transaction_statinfo.hpp
    transaction_transfer.cpp
    transaction_transfer.hpp
    types.hpp
    types.gen.hpp)

# libraries this module links to
target_link_libraries(blockchain
    PUBLIC
        mesh.pp
        belt.pp
        publiq.pp
    PRIVATE
        packet
        utility
        p2psocket
        socket
        systemutility
        cryptoutility
        sessionutility
        storage_utility
        log)

#add_definitions(-DUSE_BOOST_TLS)
#target_link_libraries(blockchain PRIVATE Boost::system)
#find_package(OpenSSL REQUIRED SSL Crypto)
#target_link_libraries(blockchain PRIVATE OpenSSL::SSL OpenSSL::Crypto)

# what to do on make install
install(TARGETS blockchain
        EXPORT publiq.pp.package
        RUNTIME DESTINATION ${PUBLIQPP_INSTALL_DESTINATION_RUNTIME}
        LIBRARY DESTINATION ${PUBLIQPP_INSTALL_DESTINATION_LIBRARY}
        ARCHIVE DESTINATION ${PUBLIQPP_INSTALL_DESTINATION_ARCHIVE})

install(FILES
    coin.hpp
    global.hpp
    node.hpp
    message.hpp
    message.gen.hpp
    message.tmpl.hpp
    message.gen.tmpl.hpp
    storage_node.hpp
    DESTINATION ${PUBLIQPP_INSTALL_DESTINATION_INCLUDE}/libblockchain)
