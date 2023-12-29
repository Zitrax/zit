add_executable(sock_server server.cpp)
target_include_directories(sock_server PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sock_server PRIVATE
    asio::asio
    spdlog::spdlog
)

add_executable(sock_client client.cpp)
