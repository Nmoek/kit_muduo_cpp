/**
 * @file singleton.h
 * @brief  单例模版
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-21 16:16:07
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __SINGLETON_H__
#define __SINGLETON_H__

#include <memory>

namespace kit_muduo {

template<class T, class X = void, int N = 0>
class Singleton
{
public:
    static T& GetInstance()
    {
        static T v;
        return v;
    }
};

template<class T, class X = void, int N = 0>
class SingletonPtr
{
public:
    static std::shared_ptr<T> GetInstance()
    {
        static std::shared_ptr<T> v(std::make_shared<T>());
        return v;
    }

};

} // namespace kit
#endif