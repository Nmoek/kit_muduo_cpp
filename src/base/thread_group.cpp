/**
 * @file thread_group.cpp
 * @brief 线程组
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-31 20:30:51
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/thread_group.h"
#include "base/base_log.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>

namespace kit_muduo {

ThreadGroup::ThreadGroup(std::vector<Package> packages)
    :state_(State::kCreated)
    ,stopping_(std::make_shared<std::atomic_bool>(false))

{
    entries_.reserve(packages.size());
    for(auto &p: packages)
    {
        entries_.push_back(makeEntry(std::move(p)));
    }
}

ThreadGroup::~ThreadGroup()
{
    requestStop();
    joinAllNoExcept();
}

void ThreadGroup::startAll()
{
    if(entries_.empty())
    {
        throw std::invalid_argument("ThreadGroup entries empty!");
    }

    State expected_state = State::kCreated;
    if (!state_.compare_exchange_strong(expected_state, State::kRunning, std::memory_order_acq_rel))
    {
        throw std::logic_error("ThreadGroup::startAll() can only be called once");
    }

    size_t started_count = 0;
    try {
        for(auto &e : entries_)
        {
            e.thread->start();
            ++started_count;
        }
    }catch(...) {
        requestStop();
        joinAll();
        for(size_t i = started_count;i < entries_.size();++i)
        {
            entries_[i].thread.reset();
        }

        state_.store(State::kJoined, std::memory_order_release);
        throw;
    }


    return;
}

void ThreadGroup::wait() 
{
    if(State::kJoined == state_.load(std::memory_order_acquire))
    {
        return;
    }

    State expected_state = State::kRunning;
    if(!state_.compare_exchange_strong(expected_state, State::kJoining, std::memory_order_acq_rel))
    {
        throw std::logic_error("ThreadGroup::wait() must be running\n");
    }

    try {
        joinAll();

        state_.store(State::kJoined, std::memory_order_release);
    } catch (const std::exception &e) {
        // 不能标记为 Joined，否则尚未成功回收的线程
        // 将永久失去再次处理的机会。
        state_.store(State::kRunning, std::memory_order_release);
        THREAD_F_ERROR("ThreadGroup::wait() failed, try again: %s\n", e.what());
        throw;
    } catch(...) {
        state_.store(State::kRunning, std::memory_order_release);
        THREAD_F_ERROR("ThreadGroup::wait() failed, try again! unknow exception\n");
        throw;
    }
}


void ThreadGroup::requestStop() noexcept 
{
    stopping_->store(true, std::memory_order_release);
}


bool ThreadGroup::joined() const noexcept
{
    return state_.load(std::memory_order_acquire) == State::kJoined;
}

bool ThreadGroup::stopping() const noexcept
{
    return stopping_->load(std::memory_order_acquire);
}


ThreadGroup::Entry ThreadGroup::makeEntry(Package package)
{
    if(package.name.empty())
    {
        throw std::invalid_argument("ThreadGroup package name is empty");
    }

    if(!package.task)
    {
        throw std::invalid_argument("ThreadGroup package task is empty");
    }
    return Entry{
        package.name,
        std::make_unique<Thread>([stop = stopping_, task = std::move(package.task)]() {

            try {
                task([stop] { return stop->load(std::memory_order_acquire); });

            } catch (const std::exception& e) {
                THREAD_F_ERROR("ThreadGroup excute exception: %s \n", e.what());

            } catch (...) {
                THREAD_F_ERROR("ThreadGroup excute unknown exception\n");
            }

        }, package.name)
    };
}

void ThreadGroup::joinAll() 
{
    for (auto& entry : entries_) 
    {
        if (entry.thread && entry.thread->started() && !entry.thread->joined())
        {
            entry.thread->join();
        }
    }
}

void ThreadGroup::joinAllNoExcept() noexcept
{
    try {
        joinAll();
    } catch(...) {
        THREAD_F_WARN("ThreadGroup join except...\n");
    }
}


} // namespace kit_muduo