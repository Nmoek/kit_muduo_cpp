cmake_minimum_required(VERSION 3.0)
project(protocol-test-platform)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include (cmake/utils.cmake)

set(CMAKE_CXX_STANDARD 17)


set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(CMAKE_CXX_FLAGS  "$ENV{CXXFLAGS} \
    -rdynamic \
    -std=c++17 \
    -fPIC \
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

# Sqlite3 配置参数
set(SQLITE3_DEF
    SQLITE_ENABLE_JSON1
    SQLITE_ENABLE_MEMSYS5
    SQLITE_DEFAULT_CACHE_SIZE=20000  
    SQLITE_DEFAULT_MMAP_SIZE=268435456
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1  
    SQLITE_DEFAULT_SYNCHRONOUS=1
    SQLITE_DEFAULT_JOURNAL_MODE=WAL 
)


# 测试 target 默认关闭。
# MUDUO_TEST=ON 构建全部 muduo 测试；MUDUO_TEST.HTTP_MESSAGE=ON 只构建对应子测试。
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
option(MUDUO_TEST.CONFIG "build test_config" OFF)

option(MUDUO_TEST.TCP_SERVER "build test_tcp_server" OFF)
option(MUDUO_TEST.HTTP_MESSAGE "build test_http_message" OFF)
option(MUDUO_TEST.HTTP_CONTEXT "build test_http_context" OFF)
option(MUDUO_TEST.HTTP_SERVER "build test_http_server" OFF)
option(MUDUO_TEST.HTTP_CONTENT "build test_http_content" OFF)
option(MUDUO_TEST.HTTP_CONTENT_CODEC "build test_http_content_codec" OFF)
option(MUDUO_TEST.LLHTTP "build test_llhttp" OFF)
option(MUDUO_TEST.THREAD_POOL "build test_thread_pool" OFF)
option(MUDUO_TEST.CONVERT "build test_convert" OFF)
option(MUDUO_TEST.ROUTER "build test_router" OFF)
option(MUDUO_TEST.VARIANT "build test_variant" OFF)
option(MUDUO_TEST.ASAN "build test_asan" OFF)
option(MUDUO_TEST.UDP "build test_udp" OFF)
option(MUDUO_TEST.LRU_CACHE "build test_lru_cache" OFF)
option(MUDUO_TEST.BYTES_CODEC "build test_bytes_codec" OFF)
option(MUDUO_TEST.BOUNDED_LOCK_FREE_QUEUE "build test_bounded_lock_free_queue" OFF)
option(MUDUO_TEST.MULTIFORM "build test_multiform" OFF)
option(MUDUO_TEST.WS_UTIL "build test_websocket_util" OFF)
option(MUDUO_TEST.WS_FRAME "build test_websocket_frame" OFF)
option(MUDUO_TEST.WS_CONTEXT "build test_websocket_context" OFF)
option(MUDUO_TEST.WS_SESSION "build test_websocket_session" OFF)
option(MUDUO_TEST.WS_SERVER "build test_websocket_server" OFF)


option(WORK_TEST "build all work tests" OFF)
option(WORK_TEST.SQLITE3 "build test_sqlite3" OFF)
option(WORK_TEST.SQLITE_ORM "build test_sqlite_orm" OFF)
option(WORK_TEST.SQLITE_ORM_POOL "build test_sqlite_orm_pool" OFF)
option(WORK_TEST.WEB_PROJECT "build test_web_project" OFF)
option(WORK_TEST.WEB_USER "build test_web_user" OFF)
option(WORK_TEST.WEB_PROTOCOL "build test_web_protocol" OFF)
option(WORK_TEST.DAO_PROTOCOL "build test_dao_protocol" OFF)
option(WORK_TEST.WEB_PROTOCOL_INTERACTION "build test_web_protocol_interaction" OFF)
option(WORK_TEST.CUSTOM_PJ_SERVER "build test_custom_project_server" OFF)
option(WORK_TEST.PJ_SERVER_FAC "build test_project_server_factory" OFF)
option(WORK_TEST.RUNTIME_RESULT "build test_runtime_result" OFF)
option(WORK_TEST.PROTOCOL_RUNTIME_KEY "build test_protocol_runtime_key" OFF)
option(WORK_TEST.PROTOCOL_CONFIG_PIPELINE "build test_protocol_config_pipeline" OFF)
option(WORK_TEST.PROTOCOL_BODY_PIPELINE "build test_protocol_body_pipeline" OFF)
option(WORK_TEST.PROTOCOL_INTERACTION "build test_protocol_interaction" OFF)
option(WORK_TEST.RUNTIME "build test_runtime" OFF)
option(WORK_TEST.PROJECT_RUNTIME_MANAGER "build test_project_runtime_manager" OFF)
option(WORK_TEST.CUSTOM_TCP_FIELD "build test_custom_tcp_field" OFF)
option(WORK_TEST.CUSTOM_TCP_PATTERN_SPEC "build test_custom_tcp_pattern_spec" OFF)
option(WORK_TEST.CUSTOM_TCP_PATTERN "build test_custom_tcp_pattern" OFF)
option(WORK_TEST.USER "build test_user_auth" OFF)

# SQLite3数据库基准测试
option(WORK_BENCH.SQLITE_INTERACTION "build SQLite interaction history storage benchmark" OFF)

# 日志系统基准测试
option(MUDUO_BENCH.LOG "build log benchmark" OFF)

option(EXAMPLE "build all example" OFF)
option(EXAMPLE.HTTP "build example http_serverv" OFF)


option(MEM_CHECK "make memory check flag" OFF)
option(COVERAGE_TEST "make coverage file" OFF)

# ASan环境变量 首个错误打断+检测泄漏+详细输出+快速退出+日志路径
# export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:verbosity=1:log_path=/mnt/nfs/proxy_log/kit_asan.log:hard_rss_limit_mb=4096



if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()


if(COVERAGE_TEST)
    set(CMAKE_BUILD_TYPE Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

if(MEM_CHECK)
    set(CMAKE_BUILD_TYPE Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -fsanitize=alignment,address  -fno-omit-frame-pointer")
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug"
    OR CMAKE_BUILD_TYPE MATCHES "RelWithDebInfo")
    message("Debug/RelWithDebInfo mode")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")

elseif(CMAKE_BUILD_TYPE MATCHES "Release")
    message("Release mode")
endif()

# 添加第三方包搜索路径
list(APPEND CMAKE_PREFIX_PATH "${PROJECT_SOURCE_DIR}/pkg")
# gtest包添加
find_package(GTest REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

if(MUDUO_BENCH.LOG)
    find_package(benchmark CONFIG REQUIRED)
endif()

enable_testing()



# 添加第三方库链接路径
set(PKG_LIB_PATH
    "pkg/llhttp/lib"
    "pkg/pugixml/lib"
    "pkg/simdutf/lib"
    "pkg/HowardHinnant_date/lib"
    "pkg/yaml-cpp/lib"
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
    ${PROJECT_SOURCE_DIR}/pkg/nanoid_cpp/include/
    ${PROJECT_SOURCE_DIR}/pkg/pugixml/include
    ${PROJECT_SOURCE_DIR}/pkg/simdutf/include
    ${PROJECT_SOURCE_DIR}/pkg/HowardHinnant_date/include
    ${PROJECT_SOURCE_DIR}/pkg/yaml-cpp/include
)

set(BASE_SRC
    src/base/log.cpp
    src/base/log_level.cpp
    src/base/log_formatter.cpp
    src/base/log_attr.cpp
    src/base/log_appender.cpp
    src/base/log_config.cpp
    src/base/log_file_sink.cpp
    src/base/log_file_backend.cpp
    src/base/regular_log_file_backend.cpp
    
    src/base/util.cpp
    src/base/time_stamp.cpp
    src/base/bytes_codec.cpp

    src/base/thread.cpp
    src/base/thread_pool.cpp
    src/base/event_loop_thread.cpp
    src/base/event_loop_thread_pool.cpp

    src/base/lexical_cast.cpp
    src/base/config_context.cpp
    src/base/config.cpp
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
    src/net/http/http_content.cpp
    src/net/http/http_content_codec.cpp
    src/net/http/multiform.cpp
)

set(NET_WEBSOCKET_SRC
    src/net/websocket/websocket_util.cpp
    src/net/websocket/websocket_frame.cpp
    src/net/websocket/websocket_context.cpp
    src/net/websocket/websocket_session.cpp
    src/net/websocket/websocket_server.cpp
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
    src/net/async_udp_datagram.cpp
    src/net/udp_server.cpp
    
    ${NET_HTTP_SRC}
    ${NET_WEBSOCKET_SRC}
)

set(DB_SRC
    src/db/sqlite3/sqlite3.c
)

set(MUDUO_SRC
    ${BASE_SRC}
    ${NET_SRC}
    ${DB_SRC}
)


set(WORK_DAO_SRC
    src/work/repository/dao/init.cpp
    src/work/repository/dao/sqlite_orm_pool.cpp

    src/work/repository/dao/dao_project.cpp
    src/work/repository/dao/dao_protocol.cpp
    src/work/repository/dao/dao_session.cpp
    src/work/repository/dao/dao_user.cpp
)


set(WORK_REPO_SRC
    src/work/repository/repo_project.cpp
    src/work/repository/repo_protocol.cpp
    src/work/repository/repo_session.cpp
    src/work/repository/repo_user.cpp

)

set(WORK_SVC_SRC
    src/work/service/svc_project.cpp
    src/work/service/svc_protocol.cpp
    src/work/service/password_hasher.cpp
    src/work/service/svc_auth.cpp
    src/work/service/svc_user.cpp
)

set(WORK_DOMAIN_SRC
    src/work/domain/type.cpp
    src/work/domain/protocol_runtime_key.cpp
    src/work/domain/user.cpp
    src/work/domain/runtime_result.cpp
    src/work/domain/runtime_loop_pool.cpp
    
    src/work/domain/project_server.cpp
    src/work/domain/protocol_item.cpp
    src/work/domain/http_protocol_item.cpp
    src/work/domain/custom_tcp_protocol_item.cpp
    src/work/domain/protocol_config_pipeline.cpp
    src/work/domain/protocol_body_pipeline.cpp

    src/work/domain/http_project_server.cpp
    src/work/domain/custom_tcp_project_server.cpp
    src/work/domain/project_server_factory.cpp

    src/work/domain/custom_tcp_context.cpp
    src/work/domain/custom_tcp_message.cpp

    src/work/domain/custom_tcp_field_model.cpp
    src/work/domain/custom_tcp_field_type_traits.cpp
    src/work/domain/custom_tcp_field_codec.cpp
    src/work/domain/custom_tcp_pattern_spec.cpp
    src/work/domain/custom_tcp_pattern.cpp

    src/work/domain/protocol_interaction.cpp
    src/work/domain/protocol_interaction_hub.cpp
    src/work/domain/protocol_interaction_publisher.cpp
    src/work/domain/protocol_interaction_observation.cpp
)

set(WORK_RUNTIME_SRC
    src/work/runtime/runtime_controller.cpp
)

set(WORK_WEB_SRC
    src/work/web/web_test.cpp
    src/work/web/web_project.cpp
    src/work/web/web_protocol.cpp
    src/work/web/web_auth.cpp
    src/work/web/web_user.cpp
    src/work/web/web_protocol_interaction.cpp
    src/work/web/web_common.cpp
    src/work/web/project_vo.cpp
    src/work/web/protocol_vo.cpp
    src/work/web/write_response.cpp

)

set(IOC_SRC
    src/ioc/web.cpp
    src/ioc/log.cpp
    src/ioc/interaction.cpp
)

set(WORK_APP_SRC
    ${WORK_DAO_SRC}
    ${WORK_REPO_SRC}
    ${WORK_SVC_SRC}
    ${WORK_RUNTIME_SRC}
    ${WORK_WEB_SRC}
    ${IOC_SRC}
    src/application.cpp
    src/app_config.cpp
)

set(WORK_SRC
    ${WORK_APP_SRC}
    ${WORK_DOMAIN_SRC}
)

# defines options


# defines
set(DEF

)

add_library(kit_muduo SHARED ${MUDUO_SRC})
redefine_file_macro(kit_muduo)   #重定义 __FILE__
target_compile_definitions(kit_muduo PRIVATE ${DEF} ${SQLITE3_DEF})

# 运行时依赖库
set(KIT_RUNTIME_LIBS
    kit_muduo
    dl
    pthread
    llhttp
    crypt
    OpenSSL::Crypto
    pugixml
    simdutf
    yaml-cpp
)

# 测试目标额外依赖 GTest。工作层源码集中编译到静态库，避免每个工作层
# 测试目标都重新编译一遍业务实现。domain 单测可只链接 kit_work_domain。
add_library(kit_work_domain STATIC ${WORK_DOMAIN_SRC})
redefine_file_macro(kit_work_domain)   #重定义 __FILE__
target_compile_definitions(kit_work_domain PRIVATE ${DEF})
target_link_libraries(kit_work_domain PUBLIC ${KIT_RUNTIME_LIBS})

add_library(kit_work STATIC ${WORK_APP_SRC})
redefine_file_macro(kit_work)   #重定义 __FILE__
target_compile_definitions(kit_work PRIVATE ${DEF})
target_link_libraries(kit_work PUBLIC kit_work_domain ${KIT_RUNTIME_LIBS})

# lib
set(LIB_LIB
    ${KIT_RUNTIME_LIBS}
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    GTest::gmock_main
)

# **********************************tests**********************************#

if(WORK_BENCH.SQLITE_INTERACTION)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    add_executable(interaction_history_storage_bench
        tests/benchmarks/interaction_history_storage_bench/interaction_history_storage_bench.cpp)
    redefine_file_macro(interaction_history_storage_bench)
    target_compile_definitions(interaction_history_storage_bench PRIVATE ${DEF})
    target_link_libraries(interaction_history_storage_bench PRIVATE
        ${KIT_RUNTIME_LIBS}
        OpenSSL::Crypto
    )
endif()

if(MUDUO_BENCH.LOG)
    add_executable(log_benchmark
        tests/benchmarks/log/log_benchmark.cpp)
    redefine_file_macro(log_benchmark)
    target_compile_definitions(log_benchmark PRIVATE ${DEF})
    target_link_libraries(log_benchmark PRIVATE
        ${KIT_RUNTIME_LIBS}
        benchmark::benchmark
    )

    add_executable(log_benchmark_optimize
        tests/benchmarks/log/log_benchmark.cpp)
    redefine_file_macro(log_benchmark_optimize)
    target_compile_definitions(log_benchmark_optimize PRIVATE ${DEF})
    target_link_libraries(log_benchmark_optimize PRIVATE
        ${KIT_RUNTIME_LIBS}
        benchmark::benchmark
    )
endif()


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
        redefine_file_macro(${target_name})   #重定义 __FILE__
        target_compile_definitions(${target_name} PRIVATE ${DEF})
        target_link_libraries(${target_name} PRIVATE ${LIB_LIB})
    endif()
endfunction()

function(add_work_test test_option target_name)
    add_kit_test(WORK_TEST ${test_option} ${target_name} ${ARGN})
    if(TARGET ${target_name})
        target_link_libraries(${target_name} PRIVATE kit_work)
    endif()
endfunction()

function(add_work_domain_test test_option target_name)
    add_kit_test(WORK_TEST ${test_option} ${target_name} ${ARGN})
    if(TARGET ${target_name})
        target_link_libraries(${target_name} PRIVATE kit_work_domain)
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


# socket测试 test_socket
add_kit_test(MUDUO_TEST MUDUO_TEST.SOCKET test_socket tests/test_socket.cpp)

# Buffer测试 test_buffer
add_kit_test(MUDUO_TEST MUDUO_TEST.BUFFER test_buffer tests/test_buffer.cpp)


# Timer测试 test_timer
add_kit_test(MUDUO_TEST MUDUO_TEST.TIMER test_timer tests/test_timer.cpp)

# Log测试 test_log
add_kit_test(MUDUO_TEST MUDUO_TEST.LOG test_log tests/test_log.cpp)

# Config测试 test_config
add_kit_test(MUDUO_TEST MUDUO_TEST.CONFIG test_config
    tests/test_config.cpp
    src/app_config.cpp)

# TcpServer测试 test_tcp_server
add_kit_test(MUDUO_TEST MUDUO_TEST.TCP_SERVER test_tcp_server tests/test_tcp_server.cpp)

# HTTP request/response 报文模型与解析测试
add_kit_test(MUDUO_TEST MUDUO_TEST.HTTP_MESSAGE test_http_message tests/http/test_http_message.cpp)

# HttpContext body bind facade 测试
add_kit_test(MUDUO_TEST MUDUO_TEST.HTTP_CONTEXT test_http_context tests/http/test_http_context.cpp)

# HTTP server 行为测试
add_kit_test(MUDUO_TEST MUDUO_TEST.HTTP_SERVER test_http_server tests/http/test_http_server.cpp)

# llhttp测试 test_llhttp
add_kit_test(MUDUO_TEST MUDUO_TEST.LLHTTP test_llhttp tests/http/test_llhttp.cpp)

# thread_pool 测试 test_thread_pool
add_kit_test(MUDUO_TEST MUDUO_TEST.THREAD_POOL test_thread_pool tests/test_thread_pool.cpp)

# 格式转换 测试 test_convert
add_kit_test(MUDUO_TEST MUDUO_TEST.CONVERT test_convert tests/test_convert.cpp)

# http router 测试 test_router
add_kit_test(MUDUO_TEST MUDUO_TEST.ROUTER test_router tests/http/test_router.cpp)

# variant 自定义类型包装器
add_kit_test(MUDUO_TEST MUDUO_TEST.VARIANT test_variant tests/test_variant.cpp)


# test_asan 内存泄漏工具
add_kit_test(MUDUO_TEST MUDUO_TEST.ASAN test_asan tests/test_asan.cpp)

# test_udp udp集成测试
add_kit_test(MUDUO_TEST MUDUO_TEST.UDP test_udp tests/test_udp.cpp)


# test_lru_cache LRU缓存测试
add_kit_test(MUDUO_TEST MUDUO_TEST.LRU_CACHE test_lru_cache tests/test_lru_cache.cpp)

# test_bytes_codec 纯字节格式 codec 测试
add_kit_test(MUDUO_TEST MUDUO_TEST.BYTES_CODEC test_bytes_codec tests/test_bytes_codec.cpp)

# test_bounded_lock_free_queue 有界无锁队列测试
add_kit_test(MUDUO_TEST MUDUO_TEST.BOUNDED_LOCK_FREE_QUEUE test_bounded_lock_free_queue tests/test_bounded_lock_free_queue.cpp)

# test_http_content HTTP content meta 解析测试
add_kit_test(MUDUO_TEST MUDUO_TEST.HTTP_CONTENT test_http_content tests/http/test_http_content.cpp)

# test_http_content_codec HTTP content codec 测试
add_kit_test(MUDUO_TEST MUDUO_TEST.HTTP_CONTENT_CODEC test_http_content_codec tests/http/test_http_content_codec.cpp)

# test_multiform multipart/form-data 解析器测试
add_kit_test(MUDUO_TEST MUDUO_TEST.MULTIFORM test_multiform tests/http/test_multiform.cpp)


# test_websocket_util websocket辅助工具测试
add_kit_test(MUDUO_TEST MUDUO_TEST.WS_UTIL test_websocket_util tests/websocket/test_websocket_util.cpp)

# test_websocket_util websocket帧数据测试
add_kit_test(MUDUO_TEST MUDUO_TEST.WS_FRAME test_websocket_frame tests/websocket/test_websocket_frame.cpp)

# test_websocket_context websocket客户端帧解析上下文测试
add_kit_test(MUDUO_TEST MUDUO_TEST.WS_CONTEXT test_websocket_context tests/websocket/test_websocket_context.cpp)

# test_websocket_session websocket会话状态和收发行为测试
add_kit_test(MUDUO_TEST MUDUO_TEST.WS_SESSION test_websocket_session tests/websocket/test_websocket_session.cpp)

# test_websocket_server websocket Upgrade接入测试
add_kit_test(MUDUO_TEST MUDUO_TEST.WS_SERVER test_websocket_server tests/websocket/test_websocket_server.cpp)




#sqlite3测试相关配置是否生效
add_work_test(WORK_TEST.SQLITE3 test_sqlite3 tests/db/test_sqlite3.cpp)

# sqlite_orm 测试 test_sqlite_orm
add_work_test(WORK_TEST.SQLITE_ORM test_sqlite_orm tests/db/test_sqlite_orm.cpp)

# sqlite_orm pool 测试 test_sqlite_orm_pool
add_kit_test(WORK_TEST WORK_TEST.SQLITE_ORM_POOL test_sqlite_orm_pool
    tests/db/test_sqlite_orm_pool.cpp
    src/work/repository/dao/init.cpp
    src/work/repository/dao/sqlite_orm_pool.cpp)

# web端测试服务 测试 test_web_project
add_work_test(WORK_TEST.WEB_PROJECT test_web_project tests/work/web/test_web_project.cpp)

# web端用户候选接口测试 test_web_user
add_work_test(WORK_TEST.WEB_USER test_web_user tests/work/web/test_web_user.cpp)

# web端协议项接口测试 test_web_protocol
add_work_test(WORK_TEST.WEB_PROTOCOL test_web_protocol tests/work/web/test_web_protocol.cpp)

# dao端协议项分页测试 test_dao_protocol
add_work_test(WORK_TEST.DAO_PROTOCOL test_dao_protocol tests/work/repository/dao/test_dao_protocol.cpp)

# 协议项实时交互详情 WebSocket 接口测试 test_web_protocol_interaction
add_work_test(WORK_TEST.WEB_PROTOCOL_INTERACTION test_web_protocol_interaction
    tests/work/web/test_web_protocol_interaction.cpp)

# custom_tcp服务器实例 test_custom_project_server
add_work_test(WORK_TEST.CUSTOM_PJ_SERVER test_custom_project_server tests/work/domain/test_custom_project_server.cpp)

# 测试服务实例工厂测试 test_project_server_factory
add_work_test(WORK_TEST.PJ_SERVER_FAC test_project_server_factory tests/work/domain/test_project_server_factory.cpp)

# runtime 操作结果类型测试 test_runtime_result
add_kit_test(WORK_TEST WORK_TEST.RUNTIME_RESULT test_runtime_result
    tests/work/domain/test_runtime_result.cpp
    src/work/domain/runtime_result.cpp)

# 协议运行键生成测试 test_protocol_runtime_key
add_kit_test(WORK_TEST WORK_TEST.PROTOCOL_RUNTIME_KEY test_protocol_runtime_key
    tests/work/domain/test_protocol_runtime_key.cpp
    src/work/domain/protocol_runtime_key.cpp)

# 协议配置校验与运行态构建 pipeline 测试 test_protocol_config_pipeline
add_work_domain_test(WORK_TEST.PROTOCOL_CONFIG_PIPELINE test_protocol_config_pipeline
    tests/work/domain/test_protocol_config_pipeline.cpp)

# 协议 Body 类型与字节内容匹配校验测试 test_protocol_body_pipeline
add_work_domain_test(WORK_TEST.PROTOCOL_BODY_PIPELINE test_protocol_body_pipeline
    tests/work/domain/test_protocol_body_pipeline.cpp)

# 协议项实时交互详情领域模型与 payload 捕获测试 test_protocol_interaction
add_kit_test(WORK_TEST WORK_TEST.PROTOCOL_INTERACTION test_protocol_interaction
    tests/work/domain/test_protocol_interaction.cpp
    src/work/domain/type.cpp
    src/work/domain/protocol_interaction.cpp
    src/work/domain/protocol_interaction_observation.cpp
    src/work/domain/protocol_interaction_hub.cpp
    src/work/domain/protocol_interaction_publisher.cpp)

# 自定义tcp字段测试 test_custom_tcp_field
add_work_test(WORK_TEST.CUSTOM_TCP_FIELD test_custom_tcp_field tests/work/domain/test_custom_tcp_field.cpp)

# 自定义tcp格式定义测试 test_custom_pattern_spec
add_work_test(WORK_TEST.CUSTOM_TCP_PATTERN_SPEC test_custom_pattern_spec tests/work/domain/test_custom_pattern_spec.cpp)

# 自定义tcp格式测试 test_custom_pattern
add_work_test(WORK_TEST.CUSTOM_TCP_PATTERN test_custom_pattern tests/work/domain/test_custom_pattern.cpp)

# 运行态测试 test_runtime
add_work_test(WORK_TEST.RUNTIME test_runtime tests/work/domain/test_runtime.cpp)

# 项目运行态管理器测试 test_project_runtime_manager
add_work_test(WORK_TEST.PROJECT_RUNTIME_MANAGER test_project_runtime_manager tests/work/runtime/test_project_runtime_manager.cpp)

# 用户登录、会话和用户管理服务测试 test_user_auth
add_work_test(WORK_TEST.USER test_user_auth tests/work/service/test_user_auth.cpp)


if(EXAMPLE.HTTP)

# http服务器示例
add_executable(example_http_server example/example_http_server.cpp)
target_link_libraries(example_http_server PRIVATE kit_work ${KIT_RUNTIME_LIBS})
redefine_file_macro(example_http_server)   #重定义 __FILE__
target_compile_definitions(example_http_server PRIVATE ${DEF})

endif()


# app服务器实例
add_executable(kit_protocol_test_platform main.cpp)
target_link_libraries(kit_protocol_test_platform PRIVATE kit_work ${KIT_RUNTIME_LIBS})
redefine_file_macro(kit_protocol_test_platform)   #重定义 __FILE__
target_compile_definitions(kit_protocol_test_platform PRIVATE ${DEF})

if(COVERAGE_TEST)
    set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin/coverage)
    set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib/coverage)
else()
    set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
    set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
endif()
