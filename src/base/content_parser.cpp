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

}   //kit_muduo