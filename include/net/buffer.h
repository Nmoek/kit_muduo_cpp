/**
 * @file buffer.h
 * @brief 读写缓冲区
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-23 21:25:30
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_BUFFER_H__
#define __KIT_BUFFER_H__
#include <bits/stdint-intn.h>
#include <vector>
#include <string>


namespace kit_muduo {

class Buffer
{
public:
    explicit Buffer(size_t initSize = kInitSize)
        :_buffer(kCheapPrepend + initSize)
        ,_readIndex(kCheapPrepend)
        ,_writeIndex(kCheapPrepend)
        /*特别注意：初始化时读写指针是可以重合的，开始写入后就不能重合 */
    {}

    ~Buffer() = default;

    /**
     * @brief 读取可读长度
     * @return size_t
     */
    size_t readableBytes() const
    {
        return _writeIndex - _readIndex;
    }

    /**
     * @brief 写区剩余长度
     * @return size_t
     */
    size_t writableBytes() const
    {
        return _buffer.size() - _writeIndex;
    }

    /**
     * @brief 间隔区长度(8Byte + 已读过读区长度)
     * @return size_t
     */
    size_t prependBytes() const
    {
        return _readIndex;
    }

    /**
     * @brief 读区有效起始地址
     * @return const char*
     */
    const char* peek() const
    {
        return begin() + _readIndex;
    }

    void reset(size_t len)
    {
        if(len < readableBytes())
        {
            _readIndex += len;
        }
        else
        {
            resetAll();
        }
    }

    void resetAll()
    {
        _readIndex = _writeIndex = kCheapPrepend;
    }

    std::string resetAllAsString()
    {
        return resetAsString(readableBytes());
    }

    std::string resetAsString(size_t len)
    {
        std::string res(peek(), len);
        reset(len);
        return res;
    }

    void ensureWritableBytes(size_t len)
    {
        /*特别注意：
            待写入长度len > 写区剩余长度writableBytes
            并不代表真的写不下了
        */
        if(len > writableBytes())
        {
            // 空间足够写入函数
            makeSpace(len);
        }
    }

    void append(const char *data, size_t len)
    {
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        _writeIndex += len;
    }

    ssize_t readFd(int32_t fd, int32_t *savedErrno);

private:
    char* begin()
    {
        return &*_buffer.begin();
    }

    const char* begin() const
    {
        return &*_buffer.begin();
    }

    char *beginWrite()
    {
        return &*_buffer.begin() + _writeIndex;
    }

    const char *beginWrite()const
    {
        return &*_buffer.begin() + _writeIndex;
    }
    /*
        |8|                      DATA                           |
        |  P Bytes    |         R  Bytes        |   W  Bytes    |
        |8|  1已读区  |    2待读区    |  3已写区  |     3待写区   |
        0             r                         w             size

        挪动后(可以明显看出 P Bytes数据大小被压缩了 已读区被覆盖了):
        TODO: 分块写入的实现效率更好；挪动的逻辑比较清楚
        |P|          R  Bytes        |          W  Bytes         |
        |8|    2待读区    |  3已写区  |          新待写区          |
        0 新r                       新w                         size

        情况1: len < 3待写区  ==> 直接写入
        情况2: 3待写区 < len < 8 + 1已读区  ==> 移动分区
        情况3: 3待写区 + (8 + 1已读区) < len + 8  ==> 需要扩容
    */
    void makeSpace(size_t len)
    {
        /*
        writableBytes() + prependBytes()
            等价于=> 写区剩余长度 + (固定8 Byte + 已读过的读区长度)
        len + kCheapPrepend
            等价于=> 待写数据长度 + 固定8 Byte
        */
        if(writableBytes() + prependBytes() < len + kCheapPrepend)
        {
            // 注意：扩容是追加扩容
            _buffer.resize(_writeIndex + len);
        }
        else // 总的剩余空间足够 重新挪动区域
        {
            size_t readable_len = readableBytes();
            std::copy(begin() + _readIndex, begin() + _writeIndex, begin() + kCheapPrepend);
            _readIndex = kCheapPrepend;
            _writeIndex = _readIndex + readable_len;
        }
    }



private:
    /// @brief 间隔区初始固定长度 8 Bytes
    static const size_t kCheapPrepend = 8;
    /// @brief 有效数据区固定长度 1KB
    static const size_t kInitSize = 1024;

private:
    std::vector<char> _buffer;
    int32_t _readIndex;
    int32_t _writeIndex;
};

}   // kit_muduo


#endif