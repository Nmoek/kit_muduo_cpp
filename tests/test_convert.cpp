/**
 * @file test_multiform.cpp
 * @brief multiform-data格式解析
 * @author ljk5
 * @version 1.0
 * @date 2025-07-28 18:19:34
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "gtest/gtest.h"
#include "test_log.h"

#include "nlohmann/json.hpp"
#include "net/net_data_converter.h"


#include <fstream>
#include <iostream>
#include <sstream>

using nljson = nlohmann::json;
using namespace kit_muduo;


static void ShowByes(const std::vector<uint8_t> bytes)
{
    std::cout << "raw hex: ";
    for(auto &x : bytes)
        printf("%02x ", x);
    std::cout << std::endl;
}

static std::string ShowBytesStr(const std::vector<uint8_t> bytes)
{
    std::stringstream ss;
    char tmp[8] = {0};
    for(auto &x : bytes)
    {
        memset(tmp, 0, sizeof(tmp));
        snprintf(tmp, 8, "%02x", x);
        ss << tmp;
        ss << " ";
    }

    return ss.str();
}

TEST(TestNetDataConvert, test)
{
    NetDataType net_data;
    std::cout << "set after: " << net_data.set(NetDataType::E::UINT8).toString() << std::endl;

    std::cout << "from after: " << NetDataType::FromString("STR").toString() << std::endl;

    std::cout << "本机模式: " << (KIT_IS_LOCAL_BIG_ENDIAN() ? "大端" : "小端") << std::endl;

    std::string after_hex;
    std::cout << "============INT8/UINT8============\n";

    // 字面量 默认小端展示
    std::string before_hex1 = "H12";

    // 按照大端转换
    auto val1 = HexToDataConverter<int8_t>()(before_hex1, true);
    printf("big endian: \"%s\" --> %s --> %d \n", before_hex1.c_str(), ShowBytesStr(ValueToBytes(val1)).c_str(), val1);

    // 按小端转换
    auto val2 = HexToDataConverter<uint8_t>()(before_hex1, false);
    printf("little endian: \"%s\" --> %s --> %d \n", before_hex1.c_str(), ShowBytesStr(ValueToBytes(val2)).c_str(), val2);

    // 小端端数据 按照大端转换
    after_hex = DataToHexConverter<uint8_t>()(val2, true);
    printf("from little => big: %s --> %d --> \"%s\" \n", ShowBytesStr(ValueToBytes(val2)).c_str() ,val2, after_hex.c_str());

    // 小端端数据 按照小端转换
    after_hex = DataToHexConverter<uint8_t>()(val2, false);
    printf("from little => little: %s --> %d --> \"%s\" \n", ShowBytesStr(ValueToBytes(val2)).c_str(), val2, after_hex.c_str());


    std::cout << "============INT16/UINT16============\n";
    // 小端字面量
    std::string before_hex2 = "H1234";

    // 转为大端实际数据 0x3412
    auto val3 = HexToDataConverter<int16_t>()(before_hex2, true);
    printf("big endian: \"%s\" --> %s --> %d \n", before_hex2.c_str(), ShowBytesStr(ValueToBytes(val3)).c_str(), val3);

    // 转为小端实际数据 0x1234
    auto val4 = HexToDataConverter<uint16_t>()(before_hex2, false);
    printf("little endian: \"%s\" --> %s --> %d \n", before_hex2.c_str(), ShowBytesStr(ValueToBytes(val4)).c_str(), val4);

    // 数据是小端 按照大端转换出字符串 "0x3412"
    after_hex = DataToHexConverter<uint16_t>()(val4, true);
    printf("little --> big: %s --> %d --> \"%s\" \n", ShowBytesStr(ValueToBytes(val4)).c_str() ,val4, after_hex.c_str());

    // 数据是小端 按照小端转换出字符串 "0x1234"
    after_hex = DataToHexConverter<uint16_t>()(val4, false);
    printf("little --> lillte: %s --> %d --> \"%s\" \n", ShowBytesStr(ValueToBytes(val4)).c_str(), val4, after_hex.c_str());

    std::cout << "============INT32/UINT32============\n";
    // 小段字面量
    std::string before_hex3 = "H12345678";

    // 转为大端实际数据
    auto val5 = HexToDataConverter<int32_t>()(before_hex3, true);
    printf("big endian: \"%s\" --> %s --> %d \n", before_hex3.c_str(), ShowBytesStr(ValueToBytes(val5)).c_str(), val5);

    // 转为小端
    auto val6 = HexToDataConverter<uint32_t>()(before_hex3, false);
    printf("little endian: \"%s\" --> %s --> %d \n", before_hex3.c_str(), ShowBytesStr(ValueToBytes(val6)).c_str(), val6);

    // 数据是小端 按照大端转换
    after_hex = DataToHexConverter<uint32_t>()(val6, true);
    printf("little --> big: %s --> %d --> \"%s\" \n", ShowBytesStr(ValueToBytes(val6)).c_str() ,val6, after_hex.c_str());

    // 数据是小端 按照小端转换
    after_hex = DataToHexConverter<uint32_t>()(val6, false);
    printf("little --> lillte: %s --> %d --> \"%s\" \n", ShowBytesStr(ValueToBytes(val6)).c_str(), val6, after_hex.c_str());


    std::cout << "============float============\n";
    // 字面量
    float f1 = 12.375f;
    std::string before_hex4 = BytesToHexString(ValueToBytes(f1), "");
  
    // 转为大端
    auto val7 = HexToDataConverter<float>()(before_hex4, true);
    printf("big endian: \"%s\" --> %s --> %f \n", before_hex4.c_str(), ShowBytesStr(ValueToBytes(val7)).c_str(), val7);

    // 转为小端
    auto val8 = HexToDataConverter<float>()(before_hex4, false);
    printf("little endian: \"%s\" --> %s --> %f \n", before_hex4.c_str(), ShowBytesStr(ValueToBytes(val8)).c_str(), val8);

    // 数据是小端 按照大端转换
    after_hex = DataToHexConverter<float>()(val8, true);
    printf("little --> big: %s --> %f --> \"%s\" \n", ShowBytesStr(ValueToBytes(val8)).c_str() ,val8, after_hex.c_str());

    // 数据是小端 按照小端转换
    after_hex = DataToHexConverter<float>()(val8, false);
    printf("little --> lillte: %s --> %f --> \"%s\" \n", ShowBytesStr(ValueToBytes(val8)).c_str(), val8, after_hex.c_str());

    std::cout << "============INT64/UINT64============\n";
    // 小段字面量
    std::string before_hex5 = "H0101020203030404";

    // 转为大端实际数据
    auto val9 = HexToDataConverter<int64_t>()(before_hex5, true);
    printf("big endian: \"%s\" --> %s --> %ld \n", before_hex5.c_str(), ShowBytesStr(ValueToBytes(val9)).c_str(), val9);
    
    // 转为小端
    auto val10 = HexToDataConverter<uint64_t>()(before_hex5, false);
    printf("little endian: \"%s\" --> %s --> %ld \n", before_hex5.c_str(), ShowBytesStr(ValueToBytes(val10)).c_str(), val10);

    // 数据是小端 按照大端转换
    after_hex = DataToHexConverter<uint64_t>()(val10, true);
    printf("little --> big: %s --> %ld --> \"%s\" \n", ShowBytesStr(ValueToBytes(val10)).c_str() ,val10, after_hex.c_str());

    // 数据是小端 按照小端转换
    after_hex = DataToHexConverter<uint64_t>()(val10, false);
    printf("little --> lillte: %s --> %ld --> \"%s\" \n", ShowBytesStr(ValueToBytes(val10)).c_str(), val10, after_hex.c_str());


    std::cout << "============double============\n";
    // 字面量
    double f2 = 3.141592;
    std::string before_hex6 = BytesToHexString(ValueToBytes(f2), "");

    // 转为大端
    auto val11 = HexToDataConverter<double>()(before_hex6, true);
    printf("big endian: \"%s\" --> %s --> %f \n", before_hex6.c_str(), ShowBytesStr(ValueToBytes(val11)).c_str(), val11);

    // 转为小端
    auto val12 = HexToDataConverter<double>()(before_hex6, false);
    printf("little endian: \"%s\" --> %s --> %f \n", before_hex6.c_str(), ShowBytesStr(ValueToBytes(val12)).c_str(), val12);

    // 数据是小端 按照大端转换
    after_hex = DataToHexConverter<double>()(val12, true);
    printf("little --> big: %s --> %f --> \"%s\" \n", ShowBytesStr(ValueToBytes(val12)).c_str() ,val12, after_hex.c_str());

    // 数据是小端 按照小端转换
    after_hex = DataToHexConverter<double>()(val12, false);
    printf("little --> lillte: %s --> %f --> \"%s\" \n", ShowBytesStr(ValueToBytes(val12)).c_str(), val12, after_hex.c_str());

    std::cout << "============string============\n";
    // 字面量
    std::string s = "H123456";
    std::string before_hex7 = BytesToHexString(ValueToBytes(s), "");
    std::cout << "before_hex7:: " <<  before_hex7 << std::endl;

    auto val13 = HexToDataConverter<std::string>()(before_hex7);
    printf("after string: \"%s\"\n", val13.c_str());
}


int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}