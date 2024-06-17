set(FETCHCONTENT_QUIET OFF CACHE BOOL "" FORCE)
cmake_minimum_required(VERSION 3.24)
project(boost_asio_strandwrap_behaviour_change)

set (CMAKE_CXX_STANDARD 17)

option(ASIO_HANDLER_INVOKE "Whether to use an asio version that can do deprecated asio_handler_invoke" 0)

include(FetchContent)
if (NOT ASIO_HANDLER_INVOKE)
    FetchContent_Declare(
        Boost
        GIT_REPOSITORY https://github.com/boostorg/boost.git
        GIT_TAG        boost-1.85.0
        )
else()
    FetchContent_Declare(
        Boost
        GIT_REPOSITORY https://github.com/boostorg/boost.git
        GIT_TAG        boost-1.83.0
        )
endif() 
FetchContent_MakeAvailable(Boost)

add_executable(boost_asio_strandwrap boost_asio_strandwrap.cpp)
target_compile_definitions(boost_asio_strandwrap PUBLIC ASIO_HANDLER_INVOKE=${ASIO_HANDLER_INVOKE})
target_link_libraries(boost_asio_strandwrap Boost::asio )