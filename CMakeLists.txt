cmake_minimum_required(VERSION 3.10)

project(rfinder)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set base directory for outputs
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out) # Executables
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out/lib) # Libraries
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out/lib) # Static libraries

add_library(rfinder-protocol STATIC protocol.cpp protocol.hpp)
if(UNIX)
    target_compile_options(rfinder-protocol PRIVATE -O2 -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(rfinder-protocol PRIVATE ws2_32)
endif()


add_executable(rfinder-client client_main.cpp)
target_link_libraries(rfinder-client PUBLIC rfinder-protocol)
if(UNIX)
    target_compile_options(rfinder-client PRIVATE -O2 -Wall -Wextra -Wpedantic)
endif()

add_executable(rfinder-server server_main.cpp fs.cpp threading.cpp networking.cpp protocol.cpp)
target_link_libraries(rfinder-server PUBLIC rfinder-protocol)
if(UNIX)
    target_compile_options(rfinder-server PRIVATE -O2 -Wall -Wextra -Wpedantic)
endif()

