cmake_minimum_required (VERSION 2.8)
project(holdmybeer-fcgi)
include_directories("./inc")

set(SOURCES holdmybeer.cpp base64.cpp)
add_executable(holdmybeer-fcgi  ${SOURCES})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++0x")
target_link_libraries (holdmybeer-fcgi fcgi fcgi++ crypto)
install(TARGETS holdmybeer-fcgi RUNTIME DESTINATION bin)