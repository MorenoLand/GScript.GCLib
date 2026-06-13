cmake_minimum_required(VERSION 3.10)
project(gclib VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/gclib.cpp
    src/TClientPacketDispatch.cpp
    src/TClientProtocol.cpp
    src/TClientProtocolIO.cpp
    src/TClientProtocolV5.cpp
    src/TClientProtocolV6.cpp
    deps/miniz/src/miniz.c
    deps/miniz/src/miniz_tdef.c
    deps/miniz/src/miniz_tinfl.c
    deps/bzip2/src/bzlib.c
    deps/bzip2/src/blocksort.c
    deps/bzip2/src/compress.c
    deps/bzip2/src/decompress.c
    deps/bzip2/src/huffman.c
    deps/bzip2/src/crctable.c
    deps/bzip2/src/randtable.c
)

set(HEADERS
    include/gclib.h
    include/IEnums.h
    include/TClient.h
    include/TClientPacketDispatch.h
    include/TClientPlayerProps.h
    include/TClientProtocol.h
    include/TClientProtocolIO.h
    include/TClientProtocolV5.h
    include/TClientProtocolV6.h
    include/TClientSocket.h
)

add_library(gclib SHARED ${SOURCES} ${HEADERS})
target_include_directories(gclib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/miniz/include
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/bzip2/include
)

if(WIN32)
    target_link_libraries(gclib PRIVATE ws2_32 crypt32 ncrypt)
    target_compile_definitions(gclib PRIVATE GCLIB_EXPORTS)
endif()

set(GCLIB_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set_target_properties(gclib PROPERTIES
    OUTPUT_NAME gclib
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${GCLIB_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${GCLIB_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${GCLIB_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${GCLIB_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${GCLIB_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${GCLIB_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${GCLIB_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${GCLIB_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${GCLIB_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${GCLIB_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${GCLIB_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${GCLIB_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${GCLIB_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${GCLIB_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${GCLIB_OUTPUT_DIR}"
)

install(TARGETS gclib LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)
install(FILES ${HEADERS} DESTINATION include)
