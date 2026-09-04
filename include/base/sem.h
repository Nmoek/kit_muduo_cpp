/**
 * @file sem.h
 * @brief POSIX 信号量
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-31 11:09:48
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_SEM_H__
#define __KIT_SEM_H__

#include <cstdint>
#include <semaphore.h>

namespace kit_muduo {


class Sem
{
public:
    Sem();
    ~Sem();
    bool post();
    bool trypost();
    bool wait();
    bool trywait();
    bool waitTimeout(int64_t timeout_ms);

private:
    sem_t sem_;
};



}
#endif //__KIT_SEM_H__