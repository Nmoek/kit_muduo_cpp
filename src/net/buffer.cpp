/**
 * @file buffer.cpp
 * @brief 读写缓冲区
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-24 02:41:43
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/buffer.h"
#include <sys/uio.h>
#include <unistd.h>

namespace kit_muduo {

ssize_t Buffer::readFd(int32_t fd, int32_t *savedErrno)
{
    char extraBuf[64 * 1024] = {0}; // 64K
    struct iovec vec[2];
    const size_t writeable_len = writableBytes();

    vec[0].iov_base = begin() + _writeIndex;
    vec[0].iov_len = writeable_len;

    vec[1].iov_base = extraBuf;
    vec[1].iov_len = sizeof(extraBuf);

    int32_t count = writeable_len < sizeof(extraBuf) ? 2 : 1;
    ssize_t n = ::readv(fd, vec, count);
    if(n < 0)
    {
        *savedErrno = errno;
    }
    else if(n < writeable_len)
    {
        _writeIndex += n;
    }
    else    // n > writeable_len
    {
        _writeIndex = _buffer.size();
        append(extraBuf, n - writeable_len);
    }
    return n;
}

ssize_t Buffer::writeFd(int32_t fd, int32_t *savedErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if(n < 0)
    {
        *savedErrno = errno;
    }

    return n;
}
}   // kit_muduo