cmake_minimum_required(VERSION 3.6)
project(kcptun)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -DASIO_STANDALONE")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
include_directories("${CMAKE_SOURCE_DIR}/asio/asio/include")
include_directories("${CMAKE_SOURCE_DIR}/gflags/include")
include_directories("${CMAKE_SOURCE_DIR}/kcp")

add_subdirectory("gflags")
add_subdirectory("kcp")

set(SOURCE_FILES
        sess.cpp
        sess.h
        utils.h
        utils.cpp
        frame.h
        smux.h
        smux.cpp
        config.h
        config.cpp
)

set(KCPTUN_CLIENT_SOURCE_FILES ${SOURCE_FILES} 
        kcptun_client_main.cpp
        kcptun_client.h
        kcptun_client.cpp
        local.cpp
        local.h)

set(KCPTUN_SERVER_SOURCE_FILES ${SOURCE_FILES}
        kcptun_server_main.cpp
        kcptun_server.h
        kcptun_server.cpp
        server.cpp
        server.h)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin/)

add_executable(kcptun_client ${KCPTUN_CLIENT_SOURCE_FILES})
target_link_libraries(kcptun_client gflags)
target_link_libraries(kcptun_client kcp)
target_link_libraries(kcptun_client z)
if(UNIX)
        target_link_libraries(kcptun_client -static-libstdc++)
        target_link_libraries(kcptun_client pthread)
endif()

add_executable(kcptun_server ${KCPTUN_SERVER_SOURCE_FILES})
target_link_libraries(kcptun_server gflags)
target_link_libraries(kcptun_server kcp)
target_link_libraries(kcptun_server z)
if(UNIX)
        target_link_libraries(kcptun_server -static-libstdc++)
        target_link_libraries(kcptun_server pthread)
endif()
