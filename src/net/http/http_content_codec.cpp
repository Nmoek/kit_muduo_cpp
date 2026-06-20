/**
 * @file http_content_codec.cpp
 * @brief HTTP报文体序列化/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-20 16:35:46
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/bytes_codec.h"
#include "net/net_log.h"
#include "net/http/http_content_codec.h"

namespace kit_muduo::http {

ContentCodecResult ToHttpContentResult(const kit_muduo::CodecResult &result)
{
    ContentCodecResult r;
    r.ok = result.ok;
    r.message = result.message;
    switch (result.code) 
    {
        case kit_muduo::CodecErrorCode::kOk:
            r.code = ContentCodecErrorCode::kOk;
            break;
        case kit_muduo::CodecErrorCode::kEmptyInput:
            r.code = ContentCodecErrorCode::kEmptyContent;
            break;
        case kit_muduo::CodecErrorCode::kInvalidData:
            r.code = ContentCodecErrorCode::kInvalidField;
            break;
        case kit_muduo::CodecErrorCode::kUnsupportedTarget:
            r.code = ContentCodecErrorCode::kUnsupportedTarget;
            break;
        case kit_muduo::CodecErrorCode::kEncodeFailed:
            r.code = ContentCodecErrorCode::kEncodeFailed;
            break;
        case kit_muduo::CodecErrorCode::kDecodeFailed:
            r.code = ContentCodecErrorCode::kDecodeFailed;
            break;
        default:
            r.code = ContentCodecErrorCode
::kInternalError;
            break;
    
    }
    return r;
}



} // kit_muduo::http