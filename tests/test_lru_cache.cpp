/**
 * @file test_lru_cache.cpp
 * @brief LRU缓存测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-04-15 20:54:37
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "./test_log.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include "base/lru_cache.h"
#include "base/thread.h"


using namespace kit_muduo;

TEST(TestLruCache, PutAndEliminate)
{
    LruCache<int, int> cache(2);
    int val = -1;

    cache.put(1, 1);
    ASSERT_TRUE(cache.tryGet(1, val));
    ASSERT_EQ(val, 1);
    ASSERT_EQ(cache.size(), 1);

    
    cache.put(2, 2);
    ASSERT_EQ(cache.size(), 2);
    
    val = -1;
    ASSERT_TRUE(cache.tryGet(1, val));
    ASSERT_EQ(val, 1);

    
    cache.put(3, 3);
    ASSERT_EQ(cache.size(), 2);

    val = -1;
    ASSERT_FALSE(cache.tryGet(2, val));
    ASSERT_EQ(val, -1);

    val = -1;
    ASSERT_TRUE(cache.tryGet(3, val));
    ASSERT_EQ(val, 3);

    val = -1;
    ASSERT_TRUE(cache.tryGet(1, val));
    ASSERT_EQ(val, 1);

}

TEST(TestLruCache, PutAndPromote)
{
    LruCache<std::string, int> cache;
    cache.put("111", 1);
    cache.put("222", 2);
    cache.put("333", 3);
    cache.put("444", 4);
    ASSERT_EQ(cache.size(), 4);

    int val = -1;
    ASSERT_TRUE(cache.tryGet("222", val));
    ASSERT_EQ(val, 2);

    val= -1;
    ASSERT_TRUE(cache.tryGet("333", val));
    ASSERT_EQ(val, 3);

    val = -1;
    cache.head(val);
    ASSERT_EQ(val, 4);

    cache.promote(); 

    // 手动提升后 应该是: (333,2)在头部  (111,1)在尾部
    val = -1;
    ASSERT_TRUE(cache.head(val));
    ASSERT_EQ(val, 3);
    val = -1;
    ASSERT_TRUE(cache.tail(val));
    ASSERT_EQ(val, 1);

    cache.clear();
    ASSERT_EQ(cache.size(), 0);

    cache.put("555", 5);
    ASSERT_EQ(cache.size(), 1);
    val = -1;
    ASSERT_TRUE(cache.head(val));
    ASSERT_EQ(val, 5);
}

TEST(TestLruCache, EraseAndPromote)
{
    LruCache<int, int> cache(5);
    int val = -1;
    cache.put(1, 1);
    cache.put(2, 2);
    cache.put(3, 4);
    cache.put(4, 4);
    ASSERT_EQ(cache.size(), 4);

    /* 访问后立即删除 提升 */
    ASSERT_TRUE(cache.tryGet(2, val));
    ASSERT_EQ(val, 2);
    cache.erase(2);
    ASSERT_EQ(cache.size(), 3);

    cache.promote();

    val = -1;
    ASSERT_TRUE(cache.head(val));
    ASSERT_EQ(val, 4); // 2已经被删 不会提升

    /* 多次访问后删除 put */
    val = -1;
    ASSERT_TRUE(cache.tryGet(1, val));
    ASSERT_EQ(val, 1);
    ASSERT_TRUE(cache.tryGet(1, val));
    ASSERT_EQ(val, 1);
    ASSERT_TRUE(cache.tryGet(1, val));
    ASSERT_EQ(val, 1);

    cache.erase(1);
    ASSERT_EQ(cache.size(), 2);

    cache.put(5, 5);
    ASSERT_EQ(cache.size(), 3);

    val = -1;
    ASSERT_TRUE(cache.head(val));
    ASSERT_EQ(val, 5); 

    // 2获取不到
    val = -1;
    ASSERT_FALSE(cache.tryGet(2, val));
    ASSERT_EQ(val, -1);

    // 1获取不到
    val = -1;
    ASSERT_FALSE(cache.tryGet(1, val));
    ASSERT_EQ(val, -1);
}


TEST(TestLruCache, ClearAndPromote)
{
    LruCache<int, int> cache(5);
    for(int i = 1;i <= 4;++i)
    {
        cache.put(i, i);
    }
    ASSERT_EQ(cache.size(), 4);

    int val = -1;
    ASSERT_TRUE(cache.tryGet(1, val));
    ASSERT_EQ(val, 1);

    val = -1;
    ASSERT_TRUE(cache.tryGet(2, val));
    ASSERT_EQ(val, 2);

    cache.clear();
    ASSERT_EQ(cache.size(), 0);

    cache.put(5, 5);

    val = -1;
    ASSERT_TRUE(cache.tryGet(5, val));
    ASSERT_EQ(val, 5);

}

TEST(TestLruCache, MutilThreadTryGet)
{
    LruCache<int, int> cache(5);
    cache.put(1, 1);
    cache.put(2, 2);
    cache.put(3, 3);
    cache.put(4, 4);
    cache.put(5, 5);
    auto read_work = [&cache](int key){
        int val = -1;
        for(int i = 0;i < 10000;++i)
        {
            ASSERT_TRUE(cache.tryGet(key, val));
            ASSERT_EQ(val, key);
            usleep(1000);
        }

    };

    std::vector<std::shared_ptr<Thread>> threads;
    for(int i = 0;i < 10;++i)
    {
        auto t = std::make_shared<Thread>(std::bind(read_work, (i % 5) + 1));
        threads.push_back(t);
    }

    for(auto &t : threads)
        t->start();

    for(auto &t : threads)
        t->join();


}

TEST(TestLruCache, MutilThreadRW)
{
    LruCache<int, int> cache(10);
    for(int i = 1;i <= 10;++i)
    {
        cache.put(i, i);
    }

    auto read_work = [&cache](int key){
        int val = -1;
        for(int i = 0;i < 10000;++i)
        {
            cache.tryGet(key,val);
            usleep(1000);
        }

    };

    auto write_work = [&cache](int key){
        for(int i = 0;i < 10000;++i)
        {
            cache.put(key, key);

            usleep(1000);
        }

    };


    std::vector<std::shared_ptr<Thread>> read_threads;
    for(int i = 1;i <= 10;++i)
    {
        read_threads.push_back(std::make_shared<Thread>(std::bind(read_work, i)));
    }

    std::vector<std::shared_ptr<Thread>> write_threads;
    for(int i = 1;i <= 10;++i)
    {
        write_threads.push_back(std::make_shared<Thread>(std::bind(write_work, i + 9)));
    }


    for(auto &t : read_threads)
        t->start();

    for(auto &t : write_threads)
        t->start();

    for(auto &t : read_threads)
        t->join();

    for(auto &t : write_threads)
        t->join();

    ASSERT_EQ(cache.size(), 10);
}