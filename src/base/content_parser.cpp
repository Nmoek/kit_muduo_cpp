/**
 * @file content_parser.cpp
 * @brief 文档/报文解析
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-12 01:48:02
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/content_parser.h"
#include "net/http/http_util.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace kit_muduo {

static constexpr char k2CRLF[] = "\r\n\r\n";
static constexpr char k2LF[] = "\n\n";

using namespace http;

const std::string CONTENT_DISPOSITION = "Content-Disposition:";
const std::string CONTENT_TYPE = "Content-Type:";
const std::string NAME_ATTR = "name=\"";
const std::string FILENAME_ATTR = "filename=\"";

MultiFormConvert::PartMap MultiFormConvert::parse(const std::string& content, const std::string& content_type) {
    PartMap parts;

    std::string boundary = extract_boundary(content_type);
    if (boundary.empty()) {
        return parts;
    }

    std::string delimiter = "--" + boundary;
    std::string end_delimiter = delimiter + "--";
    
    size_t start_pos = content.find(delimiter);
    while (start_pos != std::string::npos) {
        start_pos += delimiter.length();
        if (start_pos >= content.length()) break;

        // Skip CRLF
        if (content[start_pos] == '\r') start_pos++;
        if (content[start_pos] == '\n') start_pos++;

        size_t end_pos = content.find(delimiter, start_pos);
        if (end_pos == std::string::npos) {
            // Check for final boundary
            end_pos = content.find(end_delimiter, start_pos);
            if (end_pos == std::string::npos) break;
        }

        std::string part_data = content.substr(start_pos, end_pos - start_pos);
        
        auto part = parse_part(part_data);
        if(!part.name.empty())
            parts[part.name] = std::move(part);
        
        start_pos = end_pos;
    }

    return parts;
}

std::string MultiFormConvert::extract_boundary(const std::string& content_type) {
    const std::string BOUNDARY_KEY = "boundary=";
    size_t pos = content_type.find(BOUNDARY_KEY);
    if (pos == std::string::npos) {
        return "";
    }
    
    pos += BOUNDARY_KEY.length();
    if (content_type[pos] == '"') {
        pos++;
        size_t end_pos = content_type.find('"', pos);
        if (end_pos == std::string::npos) {
            return "";
        }
        return content_type.substr(pos, end_pos - pos);
    } else {
        size_t end_pos = content_type.find(';', pos);
        if (end_pos == std::string::npos) {
            end_pos = content_type.length();
        }
        return content_type.substr(pos, end_pos - pos);
    }
}

FormPart MultiFormConvert::parse_part(const std::string& part_data) {
    FormPart part;
    size_t header_end = part_data.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        header_end = part_data.find("\n\n");
        if (header_end == std::string::npos) {
            return part;
        }
    }

    std::string headers_part = part_data.substr(0, header_end);
    std::string data_str = part_data.substr(header_end + (part_data[header_end] == '\r' ? 4 : 2));
    part.data.assign(data_str.begin(), data_str.end());

    std::vector<std::string> headers;
    std::istringstream iss(headers_part);
    std::string header;
    while (std::getline(iss, header)) {
        if (!header.empty()) {
            headers.push_back(header);
        }
    }

    for (const auto& line : headers) {
        if (line.empty()) continue;
        
        if (line.find(CONTENT_DISPOSITION) == 0) {
            parse_content_disposition(line, part);
        } else if (line.find(CONTENT_TYPE) == 0) {
            part.content_type = line.substr(CONTENT_TYPE.length());
            part.content_type.erase(0, part.content_type.find_first_not_of(" \t"));
            part.content_type.erase(part.content_type.find_last_not_of(" \t") + 1);
        } else {
            parse_header_line(line, part);
        }
    }

    return part;
}

void MultiFormConvert::parse_content_disposition(const std::string& line, FormPart& part) {
    size_t name_pos = line.find(NAME_ATTR);
    if (name_pos != std::string::npos) {
        name_pos += NAME_ATTR.length();
        size_t name_end = line.find('"', name_pos);
        if (name_end != std::string::npos) {
            part.name = line.substr(name_pos, name_end - name_pos);
        }
    }

    size_t filename_pos = line.find(FILENAME_ATTR);
    if (filename_pos != std::string::npos) {
        filename_pos += FILENAME_ATTR.length();
        size_t filename_end = line.find('"', filename_pos);
        if (filename_end != std::string::npos) {
            part.filename = line.substr(filename_pos, filename_end - filename_pos);
        }
    }
}

void MultiFormConvert::parse_header_line(const std::string& line, FormPart& part) {
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos || colon_pos == 0) {
        return;
    }

    std::string key = line.substr(0, colon_pos);
    std::string value = line.substr(colon_pos + 1);
    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    
    part.headers[key] = value;
}

/***********MultiPartParser************ */

MultiFormParser::PartMap MultiFormParser::parse(const char* data, size_t len,
                                                 const std::string& content_type)
{
    PartMap parts;

    const std::string boundary = extract_boundary(content_type);
    if (boundary.empty()) return parts;

    const std::string bstr = "--" + boundary;
    const size_t bstr_len = bstr.size();

    const char* p = data;
    const char* const end = data + len;

    // --- 定位第一个 boundary（不强制要求 CRLF / LF 前缀） ---
    const char* first = std::search(p, end, bstr.c_str(), bstr.c_str() + bstr_len);
    if (first == end) return parts;

    // 跳过 preamble
    p = first + bstr_len;

    bool is_final = false;
    p = skip_boundary_suffix(p, end, is_final);
    if (is_final) return parts;

    // --- 后续 boundary 必须以 LF 或 CRLF 为前缀 ---
    // 搜索 "\n--boundary" 可同时覆盖 "\n--" 和 "\r\n--"。
    const std::string marker = "\n" + bstr;
    const size_t marker_len = marker.size();

    // boundary 行后缀合法字符集合：\r, \n, -, 空格, \t。
    // 若搜到 boundary 后下一个字符不在其中，说明是二进制 part 数据中的误匹配。
    auto is_valid_boundary_suffix_char = [](char c) {
        return c == '\r' || c == '\n' || c == '-' || c == ' ' || c == '\t';
    };

    while (p < end)
    {
        const char* body_start = p;
        const char* next = nullptr;

        // 扫描下一个真正的 boundary，跳过二进制 part 数据中的误匹配
        const char* scan = p;
        while (true)
        {
            scan = std::search(scan, end, marker.c_str(), marker.c_str() + marker_len);
            if (scan == end)
            {
                next = nullptr;
                break;
            }
            // 验证："\n--boundary" 之后必须是合法的后缀字符
            const char* after = scan + marker_len;
            if (after >= end || is_valid_boundary_suffix_char(*after))
            {
                next = scan;
                break;
            }
            // 误匹配，前进一个字节继续搜索
            ++scan;
        }

        if (!next) break; // epilogue（或格式错误）—— 忽略

        // body 区间为 [body_start, next)，但需剥离 boundary CRLF 前缀中的 '\r'
        size_t body_len = static_cast<size_t>(next - body_start);
        if (body_len > 0 && *(next - 1) == '\r')
        {
            --body_len;
        }

        FormPart part = parse_part(body_start, body_len);
        if (!part.name.empty())
        {
            parts[part.name] = std::move(part);
        }

        p = next + marker_len;
        p = skip_boundary_suffix(p, end, is_final);
        if (is_final) break;
    }

    return parts;
}

const char* MultiFormParser::skip_boundary_suffix(const char* p, const char* end,
                                                    bool& is_final)
{
    // 可选 LWSP（RFC 2046 §5.1.1）
    while (p < end && (*p == ' ' || *p == '\t')) ++p;

    is_final = false;
    if (p + 1 < end && p[0] == '-' && p[1] == '-')
    {
        is_final = true;
        p += 2;
        // "--" 之后可选 LWSP
        while (p < end && (*p == ' ' || *p == '\t')) ++p;
    }

    p = skip_crlf(p, end);
    return p;
}

const char* MultiFormParser::skip_crlf(const char* p, const char* end)
{
    if (p < end && *p == '\r') ++p;
    if (p < end && *p == '\n') ++p;
    return p;
}

std::string MultiFormParser::extract_boundary(const std::string& content_type)
{
    const std::string key = "boundary=";
    size_t pos = content_type.find(key);
    if (pos == std::string::npos) return {};

    pos += key.size();
    if (pos >= content_type.size()) return {};

    if (content_type[pos] == '"')
    {
        ++pos;
        size_t end_q = content_type.find('"', pos);
        if (end_q == std::string::npos) return {};
        return content_type.substr(pos, end_q - pos);
    }

    size_t end_semi = content_type.find(';', pos);
    if (end_semi == std::string::npos) end_semi = content_type.size();
    return trim(content_type.substr(pos, end_semi - pos));
}

FormPart MultiFormParser::parse_part(const char* data, size_t len)
{
    FormPart part;

    // 找到 header / body 分隔符
    const char* body_start = nullptr;

    const char* sep = std::search(data, data + len, k2CRLF, k2CRLF + strlen(k2CRLF));
    if (sep != data + len)
    {
        body_start = sep + 4;
    }
    else
    {
        sep = std::search(data, data + len, k2LF, k2LF + strlen(k2LF));
        if (sep != data + len)
        {
            body_start = sep + 2;
        }
        else
        {
            // 未找到分隔符 —— 整个内容都是 headers（罕见但合法）
            body_start = data + len;
        }
    }

    // --- 解析 headers（ASCII 文本） ---
    std::string headers_section(data, sep - data);
    {
        std::istringstream iss(headers_section);
        std::string line;
        while (std::getline(iss, line))
        {
            if (line.empty()) continue;

            // 去除行尾 \r
            if (line.size() >= 1 && line.back() == '\r')
                line.pop_back();

            if (line.find("Content-Disposition:") == 0)
            {
                parse_content_disposition(line, part);
            }
            else if (line.find("Content-Type:") == 0)
            {
                part.content_type = trim(line.substr(13)); // strlen("Content-Type:")
            }
            else
            {
                parse_custom_header(line, part);
            }
        }
    }

    // --- body：原始字节（二进制安全） ---
    size_t body_len = static_cast<size_t>((data + len) - body_start);
    part.data.assign(body_start, body_start + body_len);

    return part;
}

void MultiFormParser::parse_content_disposition(const std::string& line,
                                                  FormPart& part)
{
    auto extract_quoted = [&line](const char* attr) -> std::string {
        size_t pos = line.find(attr);
        if (pos == std::string::npos) return {};
        pos += std::strlen(attr); // 跳过 'name="' 或 'filename="'
        size_t end_q = line.find('"', pos);
        if (end_q == std::string::npos) return {};
        return line.substr(pos, end_q - pos);
    };

    part.name = extract_quoted("name=\"");
    part.filename = extract_quoted("filename=\"");
}

void MultiFormParser::parse_custom_header(const std::string& line,
                                            FormPart& part)
{
    size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) return;

    std::string key = trim(line.substr(0, colon));
    std::string value = trim(line.substr(colon + 1));

    if (!key.empty())
        part.headers[key] = value;
}

std::string MultiFormParser::trim(const std::string& s)
{
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

}   //kit_muduo