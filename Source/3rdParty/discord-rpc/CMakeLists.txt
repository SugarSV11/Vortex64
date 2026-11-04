# Rich presence client of Discord (the vendored discord-rpc library): the connection to the Discord
# client is the Unix socket of connection_unix.cpp, the Windows counterpart being connection_win.cpp.
add_library(discord_rpc STATIC
    discord_rpc.cpp
    rpc_connection.cpp
    serialization.cpp
    connection_unix.cpp
    discord_register_linux.cpp
)
target_include_directories(discord_rpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(discord_rpc PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
# the vendored rapidjson (2016) has a template body that GCC 14 and later reject without -fpermissive
target_compile_options(discord_rpc PRIVATE -w -fpermissive)
target_link_libraries(discord_rpc PUBLIC Threads::Threads)
