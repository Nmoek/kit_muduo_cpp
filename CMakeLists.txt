cmake_minimum_required(VERSION 3.0)
project(video_server)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include (cmake/utils.cmake)

set(CMAKE_CXX_STANDARD 17)


set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(CMAKE_CXX_FLAGS  "$ENV{CXXFLAGS} \
    -rdynamic \
    -std=c++17 \
    -fPIC \
    -g \
    -Wall \
    -Wno-deprecated \
    -Werror \
    -Wno-unused-function \
    -Wno-deprecated-declarations \
    -Wno-sign-compare \
    -Wno-builtin-macro-redefined\
    -Wno-unused-local-typedefs\
    -Wno-sequence-point \
    -Wcast-align")


# 测试 target 默认关闭。
# MUDUO_TEST=ON 构建全部 muduo 测试；MUDUO_TEST.HTTP=ON 只构建对应子测试。
option(MUDUO_TEST "build all muduo tests" OFF)
option(MUDUO_TEST.TIME_STAMP "build test_time_stamp" OFF)
option(MUDUO_TEST.INET_ADDRESS "build test_inet_address" OFF)
option(MUDUO_TEST.THREAD "build test_thread" OFF)
option(MUDUO_TEST.EVENT_LOOP_THREAD "build test_event_loop_thread" OFF)
option(MUDUO_TEST.POLL_POLLER "build test_poll_poller" OFF)
option(MUDUO_TEST.SOCKET "build test_socket" OFF)
option(MUDUO_TEST.BUFFER "build test_buffer" OFF)
option(MUDUO_TEST.TIMER "build test_timer" OFF)
option(MUDUO_TEST.LOG "build test_log" OFF)
option(MUDUO_TEST.TCP_SERVER "build test_tcp_server" OFF)
option(MUDUO_TEST.HTTP "build test_http" OFF)
option(MUDUO_TEST.LLHTTP "build test_llhttp" OFF)
option(MUDUO_TEST.THREAD_POOL "build test_thread_pool" OFF)
option(MUDUO_TEST.CONVERT "build test_convert" OFF)
option(MUDUO_TEST.ROUTER "build test_router" OFF)
option(MUDUO_TEST.VARIANT "build test_variant" OFF)
option(MUDUO_TEST.ASAN "build test_asan" OFF)
option(MUDUO_TEST.UDP "build test_udp" OFF)
option(MUDUO_TEST.LRU_CACHE "build test_lru_cache" OFF)
option(MEM_CHECK "make memory check flag" OFF)
option(COVERAGE_TEST "make coverage file" OFF)

# ASan环境变量 首个错误打断+检测泄漏+详细输出+快速退出+日志路径
# export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:verbosity=1:log_path=/mnt/nfs/proxy_log/kit_asan.log:hard_rss_limit_mb=4096

if(COVERAGE_TEST)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
elseif(MEM_CHECK)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -fsanitize=alignment,address -fno-omit-frame-pointer")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

# 添加第三方包搜索路径
list(APPEND CMAKE_PREFIX_PATH "${PROJECT_SOURCE_DIR}/pkg")
# gtest包添加
find_package(GTest REQUIRED)
enable_testing()

# 添加第三方库链接路径
set(PKG_LIB_PATH
    "pkg/llhttp/lib"
)

link_directories(${PKG_LIB_PATH})

# include
include_directories(
    ${GTEST_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/work/
    ${PROJECT_SOURCE_DIR}/include/work/service/
    ${PROJECT_SOURCE_DIR}/include/work/repository/
    ${PROJECT_SOURCE_DIR}/include/work/dao/

    ${PROJECT_SOURCE_DIR}/pkg/gtest/include
    ${PROJECT_SOURCE_DIR}/pkg/llhttp/include
    ${PROJECT_SOURCE_DIR}/pkg/
    ${PROJECT_SOURCE_DIR}/include/db/sqlite3/
    ${PROJECT_SOURCE_DIR}/include/db/sqlite3/
    ${PROJECT_SOURCE_DIR}/pkg/nanoid_cpp/include/
)

set(BASE_SRC
    src/base/log.cpp
    src/base/log_level.cpp
    src/base/log_formatter.cpp
    src/base/log_attr.cpp
    src/base/log_appender.cpp

    src/base/util.cpp
    src/base/time_stamp.cpp
    src/base/content_parser.cpp
    # src/base/multi_form_data_parser.cpp

    src/base/thread.cpp
    src/base/thread_pool.cpp
    src/base/event_loop_thread.cpp
    src/base/event_loop_thread_pool.cpp

)

set(NET_HTTP_SRC
    src/net/http/http_request.cpp
    src/net/http/http_context.cpp
    src/net/http/http_response.cpp
    src/net/http/http_server.cpp
    src/net/http/http_custom_parser.cpp
    src/net/http/http_llhttp_parser.cpp
    src/net/http/http_router.cpp
    src/net/http/http_servlet.cpp
    src/net/http/http_util.cpp
)


set(NET_SRC
    src/net/inet_address.cpp
    src/net/channel.cpp
    src/net/poller.cpp
    src/net/default_poller.cpp
    src/net/epoll_poller.cpp
    src/net/poll_poller.cpp
    src/net/event_loop.cpp
    src/net/socket.cpp
    src/net/acceptor.cpp
    src/net/tcp_server.cpp
    src/net/buffer.cpp
    src/net/tcp_connection.cpp
    src/net/timer.cpp
    src/net/sample_timer_queue.cpp
    src/net/net_data_converter.cpp
    src/net/udp_datagram.cpp
    src/net/udp_server.cpp
    
    ${NET_HTTP_SRC}
)


set(MUDUO_SRC
    ${BASE_SRC}
    ${NET_SRC}
)


set(WORK_SRC

)

# defines
set(DEF
    # ${SQLITE3_DEF}
)

add_library(kit_muduo SHARED ${MUDUO_SRC})
redefine_file_macro(kit_muduo)   #重定义 __FILE__


# lib 
set(LIB_LIB
    kit_muduo
    dl
    pthread
    GTest::gtest 
    GTest::gtest_main 
    GTest::gmock 
    GTest::gmock_main
    llhttp
)

set(DEPENDS
    kit_muduo
)

#dependency
set(DEPENDS
    kit_muduo
)

# **********************************tests**********************************#

function(add_kit_test module_option test_option target_name)
    set(_module_enabled OFF)
    set(_test_enabled OFF)

    if(DEFINED ${module_option})
        set(_module_enabled "${${module_option}}")
    endif()

    if(DEFINED ${test_option})
        set(_test_enabled "${${test_option}}")
    endif()

    if(_module_enabled OR _test_enabled)
        add_executable(${target_name} ${ARGN})
        add_dependencies(${target_name} ${DEPENDS})
        redefine_file_macro(${target_name})   #重定义 __FILE__
        target_link_libraries(${target_name} ${LIB_LIB})
    endif()
endfunction()

# time_stamp  时间戳测试 test_time_stamp
add_kit_test(MUDUO_TEST MUDUO_TEST.TIME_STAMP test_time_stamp tests/test_time_stamp.cpp)

# inet_address 网络地址测试 test_inet_address
add_kit_test(MUDUO_TEST MUDUO_TEST.INET_ADDRESS test_inet_address tests/test_inet_address.cpp)

# Thread 线程测试 test_thread
add_kit_test(MUDUO_TEST MUDUO_TEST.THREAD test_thread tests/test_thread.cpp)

# EventLoopThread 线程测试 test_event_loop_thread
add_kit_test(MUDUO_TEST MUDUO_TEST.EVENT_LOOP_THREAD test_event_loop_thread tests/test_event_loop_thread.cpp)

# PollPoller 回归测试 test_poll_poller
add_kit_test(MUDUO_TEST MUDUO_TEST.POLL_POLLER test_poll_poller tests/test_poll_poller.cpp)
if(MUDUO_TEST OR MUDUO_TEST.POLL_POLLER)
    add_test(NAME test_poll_poller COMMAND test_poll_poller)
endif()

# socket测试 test_socket
add_kit_test(MUDUO_TEST MUDUO_TEST.SOCKET test_socket tests/test_socket.cpp)

# Buffer测试 test_buffer
add_kit_test(MUDUO_TEST MUDUO_TEST.BUFFER test_buffer tests/test_buffer.cpp)
if(MUDUO_TEST OR MUDUO_TEST.BUFFER)
    add_test(NAME test_buffer COMMAND test_buffer)
endif()

# Timer测试 test_timer
add_kit_test(MUDUO_TEST MUDUO_TEST.TIMER test_timer tests/test_timer.cpp)

# Log测试 test_log
add_kit_test(MUDUO_TEST MUDUO_TEST.LOG test_log tests/test_log.cpp)
if(MUDUO_TEST OR MUDUO_TEST.LOG)
    add_test(NAME test_log COMMAND test_log)
endif()

# TcpServer测试 test_tcp_server
add_kit_test(MUDUO_TEST MUDUO_TEST.TCP_SERVER test_tcp_server tests/test_tcp_server.cpp)

# http测试 test_http
add_kit_test(MUDUO_TEST MUDUO_TEST.HTTP test_http tests/http/test_http.cpp)

# llhttp测试 test_llhttp
add_kit_test(MUDUO_TEST MUDUO_TEST.LLHTTP test_llhttp tests/http/test_llhttp.cpp)

# thread_pool 测试 test_thread_pool
add_kit_test(MUDUO_TEST MUDUO_TEST.THREAD_POOL test_thread_pool tests/test_thread_pool.cpp ${WORK_SRC})

# 格式转换 测试 test_convert
add_kit_test(MUDUO_TEST MUDUO_TEST.CONVERT test_convert tests/test_convert.cpp ${WORK_SRC})

# http router 测试 test_router
add_kit_test(MUDUO_TEST MUDUO_TEST.ROUTER test_router tests/http/test_router.cpp)

# variant 自定义类型包装器
add_kit_test(MUDUO_TEST MUDUO_TEST.VARIANT test_variant tests/test_variant.cpp ${WORK_SRC})
if(MUDUO_TEST OR MUDUO_TEST.VARIANT)
    target_compile_definitions(test_variant PRIVATE ${DEF})
endif()

# test_asan 内存泄漏工具
add_kit_test(MUDUO_TEST MUDUO_TEST.ASAN test_asan tests/test_asan.cpp ${WORK_SRC})

# test_udp udp集成测试
add_kit_test(MUDUO_TEST MUDUO_TEST.UDP test_udp tests/test_udp.cpp ${WORK_SRC})
if(MUDUO_TEST OR MUDUO_TEST.UDP)
    add_test(NAME test_udp COMMAND test_udp)
endif()

# test_lru_cache LRU缓存测试
add_kit_test(MUDUO_TEST MUDUO_TEST.LRU_CACHE test_lru_cache tests/test_lru_cache.cpp ${WORK_SRC})
if(MUDUO_TEST OR MUDUO_TEST.LRU_CACHE)
    add_test(NAME test_lru_cache COMMAND test_lru_cache)
endif()


# **********************************example**********************************#
# http服务器实例
add_executable(example_http_server example/example_http_server.cpp)
add_dependencies(example_http_server ${DEPENDS})
redefine_file_macro(example_http_server)   #重定义 __FILE__
target_link_libraries(example_http_server ${LIB_LIB})


if(COVERAGE_TEST)
    set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin/coverage)
    set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib/coverage)
else()
    set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
    set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
endif()
