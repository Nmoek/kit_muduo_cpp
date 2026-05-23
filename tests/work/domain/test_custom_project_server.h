/**
 * @file test_custom_project_server.h
 * @brief 自定义TCP服务器测试
 * @author ljk5
 * @version 1.0
 * @date 2025-11-20 20:33:01
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_TEST_CUSTOM_PROJECT_SERVER_H__
#define __KIT_TEST_CUSTOM_PROJECT_SERVER_H__

#include <string>

const std::string pattern_json_str1 = R"({
  "version": 2,
  "header_bytes": 26,
  "default_order": "big",
  "length_policy": "body_length",
  "fields": [
    {"name":"起始标识","byte_pos":0,"byte_len":4,"type":"UINT32","role":"start_magic","match":"H23232323"},
    {"name":"消息总长度","byte_pos":4,"byte_len":4,"type":"UINT32","role":"common"},
    {"name":"消息序列号","byte_pos":8,"byte_len":4,"type":"UINT32","role":"common"},
    {"name":"功能码","byte_pos":12,"byte_len":2,"type":"UINT16","role":"function_code"},
    {"name":"报文体长度","byte_pos":14,"byte_len":4,"type":"UINT32","role":"body_length"},
    {"name":"消息时间戳","byte_pos":18,"byte_len":8,"type":"UINT64","role":"common"}
  ]
})";

const std::string req_cfg1 = R"({"function_code":"H0100","fields":{"4":"H00000209","8":"H00000003","18":"H0000000000000000"}})";

const std::string resp_cfg1 = R"({"function_code":"H1080","fields":{"4":"H02090000","8":"H00000003","18":"HA86D9F9F9A010000"}})";

const std::string& resp_body1 = R"({"msg":"hello world"})";

/** 广州协议 */
const std::string pattern_json_str2_1 = R"({
  "version": 2,
  "header_bytes": 26,
  "default_order": "little",
  "length_policy": "total_length",
  "fields": [
    {"name":"起始标识","byte_pos":0,"byte_len":4,"type":"UINT32","role":"start_magic","match":"H23232323"},
    {"name":"报文总长度","byte_pos":4,"byte_len":4,"type":"UINT32","role":"total_length"},
    {"name":"消息序列号","byte_pos":8,"byte_len":4,"type":"UINT32","role":"common"},
    {"name":"功能码","byte_pos":12,"byte_len":2,"type":"UINT16","role":"function_code"},
    {"name":"报文体长度","byte_pos":14,"byte_len":4,"type":"UINT32","role":"common"},
    {"name":"消息时间戳","byte_pos":18,"byte_len":8,"type":"UINT64","role":"common"}
  ]
})";

const std::string req_cfg2_1 = R"({"function_code":"H0100","fields":{"8":"H03000000","14":"H0F000000","18":"HA86D9F9F9A010000"}})";

const std::string resp_cfg2_1 = R"({"function_code":"H1080","fields":{"8":"H03000000","14":"H0F000000","18":"HA86D9F9F9A010008"}})";

// 二进制Body
const std::string& resp_body2_1 = R"({"msg":"hello world"})";

/** 郑州邮政*/
const std::string pattern_json_str2_2 = R"({
  "version": 2,
  "header_bytes": 4,
  "default_order": "big",
  "length_policy": "total_length",
  "fields": [
    {"name":"起始字符","byte_pos":0,"byte_len":1,"type":"INT8","role":"start_magic","match":"H02"},
    {"name":"报文总长度","byte_pos":1,"byte_len":2,"type":"UINT16","role":"total_length"},
    {"name":"功能码","byte_pos":3,"byte_len":1,"type":"INT8","role":"function_code"}
  ]
})";

const std::string req_cfg2_2 = R"({"function_code":"H32","fields":{}})";

const std::string resp_cfg2_2 = R"({"function_code":"H42","fields":{}})";

// 配置时候使用十六进制的字符串表示
// const std::string& resp_body2_2 = "H";


/** 峰复标准 */
const std::string pattern_json_str3 = R"({
  "version": 2,
  "header_bytes": 24,
  "default_order": "raw",
  "length_policy": "no_length",
  "fields": [
    {"name":"起始字符","byte_pos":0,"byte_len":2,"type":"STR","role":"start_magic","match":"H023A"},
    {"name":"功能码","byte_pos":2,"byte_len":2,"type":"STR","role":"function_code"},
    {"name":"分隔符","byte_pos":4,"byte_len":1,"type":"STR","role":"common"},
    {"name":"设备类型","byte_pos":5,"byte_len":2,"type":"STR","role":"common"},
    {"name":"分隔符","byte_pos":7,"byte_len":1,"type":"STR","role":"common"},
    {"name":"安检机站号","byte_pos":8,"byte_len":2,"type":"STR","role":"common"},
    {"name":"分隔符","byte_pos":10,"byte_len":1,"type":"STR","role":"common"},
    {"name":"心跳序号","byte_pos":11,"byte_len":10,"type":"STR","role":"common"},
    {"name":"分隔符","byte_pos":21,"byte_len":1,"type":"STR","role":"common"},
    {"name":"结束魔数字","byte_pos":22,"byte_len":2,"type":"STR","role":"common"}
  ]
})";
// 7c 
// 30 31 
// 7c 
// 30 31 
// 7c 
// 30 30 30 30 30 30 30 30 30 31 
// 7c 
// 0D0A
// 代码生成
const std::string req_cfg3 = R"({"function_code":"H3031","fields":{"4":"H7C","5":"H3031","7":"H7C","8":"H3031","10":"H7C","11":"H30303030303030303031","21":"H7C","22":"H0D0A"}})";

const std::string resp_cfg3 = R"({"function_code":"H3131","fields":{"4":"H7C","5":"H3031","7":"H7C","8":"H3031","10":"H7C","11":"H30303030303030303031","21":"H7C","22":"H0D0A"}})";


#endif
