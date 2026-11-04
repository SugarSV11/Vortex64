cmake_minimum_required(VERSION 3.16)
project(Common CXX)

add_library(Common STATIC
    CriticalSection.cpp
    DateTime.cpp
    DynamicLibrary.cpp
    File.cpp
    HighResTimeStamp.cpp
    IniFile.cpp
    Log.cpp
    md5.cpp
    MemoryManagement.cpp
    MemTest.cpp
    path.cpp
    Platform.cpp
    Random.cpp
    StdString.cpp
    SyncEvent.cpp
    Thread.cpp
    Trace.cpp
    Util.cpp
)

target_include_directories(Common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(Common PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(Common PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# Linux threading layer regression test (SyncEvent, CThread::Terminate)
add_executable(common-test EXCLUDE_FROM_ALL tests/common_test.cpp)
set_target_properties(common-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(common-test PRIVATE Common)

# The move from the folders of Project64 to those of Vortex64 (a copy that keeps the old ones)
add_executable(migration-test EXCLUDE_FROM_ALL tests/migration_test.cpp)
set_target_properties(migration-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(migration-test PRIVATE Common)
