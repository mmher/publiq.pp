# define the executable
add_executable(test_loader_simulation
    main.cpp)

# libraries this module links to
target_link_libraries(test_loader_simulation PRIVATE
    packet
    mesh.pp
    belt.pp
    utility
    systemutility
    blockchain)

# what to do on make install
install(TARGETS test_loader_simulation
        EXPORT publiq.pp.package
        RUNTIME DESTINATION ${PUBLIQPP_INSTALL_DESTINATION_RUNTIME}
        LIBRARY DESTINATION ${PUBLIQPP_INSTALL_DESTINATION_LIBRARY}
        ARCHIVE DESTINATION ${PUBLIQPP_INSTALL_DESTINATION_ARCHIVE})
