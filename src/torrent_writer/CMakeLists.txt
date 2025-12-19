add_executable(zit_torrent_writer torrent_writer.cpp)
target_link_libraries(zit_torrent_writer
    spdlog::spdlog
    zitlib
)
target_include_directories(zit_torrent_writer PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)