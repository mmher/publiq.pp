if(TARGET rocksdb)
    # define the executable
    add_executable(test_db_backed_container
        main.cpp)

    # libraries this module links to
    target_link_libraries(test_db_backed_container PRIVATE
        packet
        mesh.pp
        belt.pp
        utility
        systemutility
        rocksdb
        blockchain)

    # what to do on make install
    install(TARGETS test_db_backed_container
        EXPORT publiq.pp.package
        RUNTIME DESTINATION ${PUBLIQPP_INSTALL_DESTINATION_RUNTIME}
        LIBRARY DESTINATION ${PUBLIQPP_INSTALL_DESTINATION_LIBRARY}
        ARCHIVE DESTINATION ${PUBLIQPP_INSTALL_DESTINATION_ARCHIVE})
endif()
