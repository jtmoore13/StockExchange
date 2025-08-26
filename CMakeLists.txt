cmake_minimum_required(VERSION 3.16)
project(StockExchange LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

option(ENABLE_LOGGING "Enable logging output" OFF)

find_package(Protobuf REQUIRED)
find_package(fmt REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/misc
    ${CMAKE_SOURCE_DIR}/src/core
)

set(PROTO_SRC ${CMAKE_SOURCE_DIR}/src/misc/proto/messages.pb.cc)

set(SHARED_SOURCES
    ${PROTO_SRC}
    src/misc/Logger.cpp
    src/misc/SharedMemoryRegion.cpp
    src/misc/TcpSocket/TcpSocketBase.cpp
)

add_executable(broker
    ${SHARED_SOURCES}
    src/core/Broker.cpp
    src/core/ExchangeOrder.cpp
    src/misc/TcpSocket/TcpClientSocket.cpp
    CompileAndRun/TerminalApps/BrokerTerminal.cpp
)

add_executable(exchange
    ${SHARED_SOURCES}
    src/core/Exchange.cpp
    src/core/MatchingEngine.cpp
    src/core/ExchangeOrder.cpp
    CompileAndRun/TerminalApps/ExchangeTerminal.cpp
)

add_executable(exchange_server
    ${SHARED_SOURCES}
    src/core/Exchange.cpp
    src/core/ExchangeServer.cpp
    src/core/MatchingEngine.cpp
    src/core/ExchangeOrder.cpp
    src/misc/TcpSocket/TcpServerSocket.cpp
    CompileAndRun/TerminalApps/ExchangeServerTerminal.cpp
)

foreach(target IN ITEMS broker exchange exchange_server)
    target_link_libraries(${target} PRIVATE protobuf::libprotobuf fmt::fmt)
    if (ENABLE_LOGGING)
        target_compile_definitions(${target} PRIVATE ENABLE_LOGGING)
    endif()
endforeach()