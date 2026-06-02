#ifndef __KIT_DAO_UTIL_H__
#define __KIT_DAO_UTIL_H__

#include <sstream>
#include <string>
#include <system_error>

namespace kit_dao {

inline std::string MakeSqliteErrorMsg(const std::system_error &e, const char *operation = nullptr)
{
    const std::error_code &ec = e.code();
    const std::error_condition condition = ec.default_error_condition();

    std::ostringstream oss;
    oss << "sqlite/system error:";
    if(operation && operation[0] != '\0')
    {
        oss << " operation[" << operation << "],";
    }
    oss << " code[" << ec.value() << "]"
        << ", category[" << ec.category().name() << "]"
        << ", message[" << ec.message() << "]"
        << ", default_condition_code[" << condition.value() << "]"
        << ", default_condition_category[" << condition.category().name() << "]"
        << ", default_condition_message[" << condition.message() << "]"
        << ", what[" << e.what() << "]!";
    return oss.str();
}

} // namespace kit_dao

#endif // __KIT_DAO_UTIL_H__
