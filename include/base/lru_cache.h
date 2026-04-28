/**
 * @file lru_cache.h
 * @brief LRU缓存(线程安全)
 * @author Kewin Li
 * @version 1.0
 * @date 2026-04-15 19:24:42
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_LRU_CACHE__
#define __KIT_LRU_CACHE__


#include "noncopyable.h"

#include <algorithm>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <cassert>

namespace kit_muduo {


template<class K, class V>
class LruCache: Noncopyable
{
public:
    using Node = typename std::pair<K, V>;
  
    using ListIterator = typename std::list<std::pair<K, V>>::iterator;

    LruCache(size_t capacity = kDefaultCapacity)
        :capacity_(capacity)
    {

    }

    void put(const K& key, const V& value)
    {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);

        ListIterator list_it;
        auto it = map_.find(key);
        if(it != map_.end())
        {
            // 更新当前值
            it->second->second = std::move(value);
            list_it = it->second;
        }
        else 
        {
            list_.emplace_front(key, value);
            map_[key] = list_.begin();
            list_it = list_.begin();
        }

        // 先把队列中的节点提升一次
        promoteUnLocked();

        //把当前的操作节点提升
        list_.splice(list_.begin(), list_, list_it);

        // 淘汰删除一定在最后
        if(map_.size() > capacity_)
        {
            eliminateUnLocked();
        }
    }

    bool tryGet(const K& key, V& out_value)
    {
        
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        auto it = map_.find(key);
        if(it == map_.end())
        {
            return false;
        }

        out_value = it->second->second;
        lock.unlock();

        // 延迟提升
        std::lock_guard<std::mutex> lock2(pending_mutex_);

        // 末尾刚访问过的key不进行提升
        if(pending_promotions_.empty()
            || pending_promotions_.back() != key)
        {
            pending_promotions_.push_back(key);
        }


        return true;
    }

    bool exist(const K& key)
    {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);

        return map_.find(key) != map_.end();
    }

    void erase(const K& key)
    {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        auto it = map_.find(key);
        if(it == map_.end())
        {
            return;
        }

        list_.erase(it->second);
        map_.erase(it);
        lock.unlock();

        {
            // 延迟队列中直接删除
            std::lock_guard<std::mutex> lock3(pending_mutex_);
            pending_promotions_.remove(key);
        }
    }

    bool head(V &out_value)
    {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        if(list_.empty())
        {
            return false;
        }
        // 由于是读头 不需要再提升了
        out_value = list_.front().second;
        return true;
    }

    bool tail(V &out_value)
    {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        if(list_.empty())
        {
            return false;
        }
        
        const K& key = list_.back().first;
        out_value = list_.back().second;
        lock.unlock();

        // 延迟提升
        std::lock_guard<std::mutex> lock2(pending_mutex_);
        // 末尾刚访问过的key不进行提升
        if(pending_promotions_.empty()
            || pending_promotions_.back() != key)
        {
            pending_promotions_.push_back(key);
        }

        return true;
    }

    void clear()
    {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        list_.clear();
        map_.clear();
        {
            std::lock_guard<std::mutex> lock2(pending_mutex_);
            pending_promotions_.clear();
        }
    }

    /**
     * @brief 手动触发提升操作
     */
    void promote()
    {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        promoteUnLocked();
    }

    size_t size() const
    {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        return map_.size();
    }

    size_t capacity() const 
    { 
        return capacity_; 
    }



private:
    void eliminateUnLocked()
    {
        auto it = list_.rbegin();
        auto n = map_.erase(it->first);
        assert(n == 1);
        list_.pop_back();
        assert(map_.size() == list_.size());
    }

    void promoteUnLocked()
    {
        std::list<K> promotions;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            if(pending_promotions_.empty())
            {
                return;
            }
            promotions.swap(pending_promotions_);
        }

        for(auto &key : promotions)
        {
            auto it = map_.find(key);
            if(it != map_.end())
            {
                list_.splice(list_.begin(), list_, it->second);
            }

            
        }

    }

private:
    static constexpr size_t kDefaultCapacity = 32;

    size_t capacity_;

    std::unordered_map<K, ListIterator> map_;
    std::list<Node> list_;
    mutable std::shared_mutex rw_mutex_;

    std::list<K> pending_promotions_;
    mutable std::mutex pending_mutex_;
};

}
#endif //__KIT_LRU_CACHE__