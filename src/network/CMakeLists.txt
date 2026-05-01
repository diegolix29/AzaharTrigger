add_library(network STATIC
    announce_multiplayer_session.cpp
    announce_multiplayer_session.h
    artic_base/artic_base_client.cpp
    artic_base/artic_base_client.h
    artic_base/artic_base_common.h
    lan_melon.cpp
    lan_melon.h
    network.cpp
    network.h
    network_settings.cpp
    network_settings.h
    packet.cpp
    packet.h
    precompiled_headers.h
    room.cpp
    room.h
    room_member.cpp
    room_member.h
    socket_manager.cpp
    socket_manager.h
    verify_user.cpp
    verify_user.h
)

create_target_directory_groups(network)

if (ENABLE_WEB_SERVICE)
    target_link_libraries(network PRIVATE web_service)
endif()

target_link_libraries(network PRIVATE citra_common Boost::serialization httplib)
target_link_libraries(network PUBLIC enet)
target_include_directories(network BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/externals/enet/include)

if (CITRA_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(network PRIVATE precompiled_headers.h)
endif()
