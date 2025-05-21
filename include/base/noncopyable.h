/**
 * @file noncopyable.h
 * @brief 禁止拷贝类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 20:12:41
 * @copyright Copyright (c) 2025 Kewin Li
 */

#ifndef __KIT_NONCOPYABLE_H__
#define __KIT_NONCOPYABLE_H__

namespace kit_muduo {

/**
 * @brief 子类继承后禁止拷贝操作
 */
class Noncopyable
{
public:
    Noncopyable(const Noncopyable&) = delete;
    Noncopyable& operator=(const Noncopyable&) = delete;
protected:
    Noncopyable() = default;
    ~Noncopyable() = default;
};



}   // kit_muoduo
#endif