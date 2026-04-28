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
    "least_byte_len": 26,
    "special_fields": {
        "start_magic_num_field": {
            "name": "起始标识",
            "idx": 0,
            "byte_pos": 0,
            "byte_len": 4,
            "type": "INT32",
            "value": "H23232323"
        },
        "function_code_field": {
            "name": "功能码",
            "idx": 3,
            "byte_pos": 12,
            "byte_len": 2,
            "type": "UINT16",
            "value": ""
        },
        "body_length_field": {
            "name": "报文体长度",
            "idx": 4,
            "byte_pos": 14,
            "byte_len": 4,
            "type": "UINT32",
            "value": ""
        }
    }
})";

const std::string req_cfg1 = R"({"function_code_filed_value":"H1000","common_fields":[{"name":"消息总长度","idx":1,"byte_pos":4,"byte_len":4,"type":"UINT32","value":"H0209"},{"name":"消息序列号","idx":2,"byte_pos":8,"byte_len":4,"type":"UINT32","value":"H0003"},{"name":"消息时间戳","idx":5,"byte_pos":18,"byte_len":8,"type":"UINT64","value":""}]})";

const std::string resp_cfg1 = R"({"function_code_filed_value":"H1080","common_fields":[{"name":"消息总长度","idx":1,"byte_pos":4,"byte_len":4,"type":"UINT32","value":"H02090000"},{"name":"消息序列号","idx":2,"byte_pos":8,"byte_len":4,"type":"UINT32","value":"H00000003"},{"name":"消息时间戳","idx":5,"byte_pos":18,"byte_len":8,"type":"UINT64","value":"HA86D9F9F9A010000"}]})";

const std::string& resp_body1 = R"({"msg":"hello world"})";

/** 广州协议 */
const std::string pattern_json_str2_1 = R"({
    "least_byte_len": 26,
    "special_fields": {
        "start_magic_num_field": {
            "name": "起始标识",
            "idx": 0,
            "byte_pos": 0,
            "byte_len": 4,
            "type": "INT32",
            "value": "H23232323"
        },
        "function_code_field": {
            "name": "功能码",
            "idx": 3,
            "byte_pos": 12,
            "byte_len": 2,
            "type": "UINT16",
            "value": ""
        },
        "total_length_field": {
            "name": "报文总长度",
            "idx": 1,
            "byte_pos": 4,
            "byte_len": 4,
            "type": "UINT32",
            "value": ""
        }
    }
})";

const std::string req_cfg2_1 = R"({"function_code_filed_value":"H1000","common_fields":[{"name":"报文体长度","idx":4,"byte_pos":14,"byte_len":4,"type":"UINT32","value":"H0000000F"},{"name":"消息序列号","idx":2,"byte_pos":8,"byte_len":4,"type":"UINT32","value":"H0003"},{"name":"消息时间戳","idx":5,"byte_pos":18,"byte_len":8,"type":"UINT64","value":"HA86D9F9F9A010000"}]})";

const std::string resp_cfg2_1 = R"({"function_code_filed_value":"H1080","common_fields":[{"name":"报文体长度","idx":4,"byte_pos":14,"byte_len":4,"type":"UINT32","value":"H0000000F"},{"name":"消息序列号","idx":2,"byte_pos":8,"byte_len":4,"type":"UINT32","value":"H00000003"},{"name":"消息时间戳","idx":5,"byte_pos":18,"byte_len":8,"type":"UINT64","value":"HA86D9F9F9A010008"}]})";

// 二进制Body
const std::string& resp_body2_1 = R"({"msg":"hello world"})";

/** 郑州邮政*/
const std::string pattern_json_str2_2 = R"({
    "least_byte_len": 4,  
    "special_fields": {
        "start_magic_num_field": {
            "name": "起始字符",
            "idx": 0,
            "byte_pos": 0,
            "byte_len": 1,
            "type": "INT8",
            "value": "H02"
        },
        "function_code_field": {
            "name": "功能码",
            "idx": 2,
            "byte_pos": 3,
            "byte_len": 1,
            "type": "INT8",
            "value": ""
        },
        "total_length_field": {
            "name": "报文总长度",
            "idx": 1,
            "byte_pos": 1,
            "byte_len": 2,
            "type": "UINT16",
            "value": ""
        }
    }
})";

const std::string req_cfg2_2 = R"({"function_code_filed_value":"H32","common_fields":[]})";

const std::string resp_cfg2_2 = R"({"function_code_filed_value":"H42","common_fields":[]})";

// 配置时候使用十六进制的字符串表示
// const std::string& resp_body2_2 = "H";


/** 峰复标准 */
const std::string pattern_json_str3 = R"({
    "least_byte_len": 4,
    "special_fields": {
        "start_magic_num_field": {
            "name": "起始字符",
            "idx": 0,
            "byte_pos": 0,
            "byte_len": 2, 
            "type": "STR",
            "value": "H023A"
        },
        "function_code_field": {
            "name": "功能码",
            "idx": 1,
            "byte_pos": 2,
            "byte_len": 2,
            "type": "STR",
            "value": ""
        }
    }
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
const std::string req_cfg3 = R"({"function_code_filed_value":"H3031","common_fields":[{"name":"分隔符","idx":2,"byte_pos":4,"byte_len":1,"type":"STR","value":"H7C"},{"name":"设备类型","idx":3,"byte_pos":5,"byte_len":2,"type":"STR","value":"H3031"},{"name":"分隔符","idx":4,"byte_pos":7,"byte_len":1,"type":"STR","value":"H7C"},{"name":"安检机站号","idx":5,"byte_pos":8,"byte_len":2,"type":"STR","value":"H3031"},{"name":"分隔符","idx":6,"byte_pos":10,"byte_len":1,"type":"STR","value":"H7C"},{"name":"心跳序号","idx":7,"byte_pos":11,"byte_len":10,"type":"STR","value":"H30303030303030303031"},{"name":"分隔符","idx":8,"byte_pos":21,"byte_len":1,"type":"STR","value":"H7C"},{"name":"结束魔数字","idx":9,"byte_pos":22,"byte_len":2,"type":"STR","value":"H0D0A"}]})";

const std::string resp_cfg3 = R"({"function_code_filed_value":"H3131","common_fields":[{"name":"分隔符","idx":2,"byte_pos":4,"byte_len":1,"type":"STR","value":"H7C"},{"name":"设备类型","idx":3,"byte_pos":5,"byte_len":2,"type":"STR","value":"H3031"},{"name":"分隔符","idx":4,"byte_pos":7,"byte_len":1,"type":"STR","value":"H7C"},{"name":"安检机站号","idx":5,"byte_pos":8,"byte_len":2,"type":"STR","value":"H3031"},{"name":"分隔符","idx":6,"byte_pos":10,"byte_len":1,"type":"STR","value":"H7C"},{"name":"心跳序号","idx":7,"byte_pos":11,"byte_len":10,"type":"STR","value":"H30303030303030303031"},{"name":"分隔符","idx":8,"byte_pos":21,"byte_len":1,"type":"STR","value":"H7C"},{"name":"结束魔数字","idx":9,"byte_pos":22,"byte_len":2,"type":"STR","value":"H0D0A"}]})";


#endif