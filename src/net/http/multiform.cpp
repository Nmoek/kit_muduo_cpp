/**
 * @file multiform.cpp
 * @brief MultiForm格式序列/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-17 23:50:42
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/http/http_content.h"
#include "net/net_log.h"
#include "net/http/multiform.h"

namespace kit_muduo::http {

namespace {

static constexpr char kHeaderSepCRLF[] = "\r\n\r\n";
static constexpr char kHeaderSepLF[] = "\n\n";

std::string Trim(const std::string& s)
{
    size_t first = s.find_first_not_of(" \t\r\n");
    if(first == std::string::npos)
    {
        return {};
    }
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string ToLower(std::string s)
{
    for(auto& ch : s)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}


const uint8_t* SkipCrlf(const uint8_t* p, const uint8_t* end)
{
    if(p < end && static_cast<char>(*p) == '\r') ++p;
    if(p < end && static_cast<char>(*p) == '\n') ++p;
    return p;
}

const uint8_t* SkipBoundarySuffix(const uint8_t* p, const uint8_t* end, bool& is_final)
{
    while(p < end && (*p == ' ' || *p == '\t'))
    {
        ++p;
    }

    is_final = false;
    if(p + 1 < end && p[0] == '-' && p[1] == '-')
    {
        is_final = true;
        p += 2;
        while(p < end && (*p == ' ' || *p == '\t'))
        {
            ++p;
        }
    }

    return SkipCrlf(p, end);
}


void ParseContentDisposition(const std::string& line, FormPart& part)
{
    auto extract_quoted = [&line](const char* attr) -> std::string {
        size_t pos = line.find(attr);
        if(pos == std::string::npos)
        {
            return {};
        }

        pos += std::strlen(attr);
        size_t end_q = line.find('"', pos);
        if(end_q == std::string::npos)
        {
            return {};
        }

        return line.substr(pos, end_q - pos);
    };

    part.name = extract_quoted("name=\"");
    part.filename = extract_quoted("filename=\"");
}

void ParseHeaderLine(const std::string& line, FormPart& part)
{
    size_t colon = line.find(':');
    if(colon == std::string::npos || colon == 0)
    {
        return;
    }

    std::string key = Trim(line.substr(0, colon));
    std::string value = Trim(line.substr(colon + 1));
    if(!key.empty())
    {
        part.headers[key] = value;
    }
}

FormPart ParsePart(const uint8_t* data, size_t len)
{
    FormPart part;

    const uint8_t* body_start = nullptr;
    const uint8_t* sep = std::search(data, data + len,
        kHeaderSepCRLF,
        kHeaderSepCRLF + std::strlen(kHeaderSepCRLF));

    if(sep != data + len)
    {
        body_start = sep + 4;
    }
    else
    {
        sep = std::search(data, data + len,
            kHeaderSepLF,
            kHeaderSepLF + std::strlen(kHeaderSepLF));
        if(sep != data + len)
        {
            body_start = sep + 2;
        }
        else
        {
            body_start = data + len;
        }
    }

    std::string headers_section(reinterpret_cast<const char*>(data), sep - data);
    std::istringstream iss(headers_section);
    std::string line;

    while(std::getline(iss, line))
    {
        if(line.empty())
        {
            continue;
        }

        if(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        size_t colon = line.find(':');
        if(colon == std::string::npos || colon == 0)
        {
            continue;
        }

        const std::string header_name = ToLower(Trim(line.substr(0, colon)));

        if(header_name == "content-disposition")
        {
            ParseContentDisposition(line, part);
        }
        else if(header_name == "content-type")
        {
            part.meta = ParseMultiformPartContentType(line.substr(colon + 1));
        }
        else
        {
            ParseHeaderLine(line, part);
        }
    }

    // 注意: Multiform中未表明'Content-Type'默认是文本
    if(part.meta.raw_content_type.empty())
    {
        part.meta = ParseMultiformPartContentType("");
    }

    part.data.assign(body_start, data + len);
    return part;
}

} // namespace


MultiForm MultiForm::parse(const std::string &body, std::string boundary)
{
    return parse(reinterpret_cast<const uint8_t*>(body.data()), body.size(), boundary);
}

MultiForm MultiForm::parse(const std::vector<uint8_t> &data, std::string boundary)
{
    return parse(data.data(), data.size(), boundary);
}



const MultiForm::PartList& MultiForm::all(const std::string &name) const
{
    auto it = fields_.find(name);
    if(it == fields_.end())
    {
        throw MultiFormException("missing multipart field: " + name);
    }

    return it->second;
}

const FormPart& MultiForm::at(const std::string &name) const
{
    const auto& list = all(name);
    if(list.size() > 1)
    {
        throw MultiFormException("duplicate multipart field: " + name);
    }
    return list.front();
}

MultiForm MultiForm::parse(const uint8_t *data, size_t len,std::string boundary)
{
    if(data == nullptr || 0 == len)
    {
        throw MultiFormException("multipart data is null");
    }

    if(boundary.empty())
    {
        throw MultiFormException("multipart boundary missing");
    }

    const std::string boundary_line = "--" + boundary;
    const size_t boundary_line_len = boundary_line.size();

    const uint8_t* p = data;
    const uint8_t* const end = data + len;

    const uint8_t* first = std::search(
        p, end,
        boundary_line.data(),
        boundary_line.data() + boundary_line_len);

    if(first == end)
    {
        throw MultiFormException("multipart boundary not found");
    }

    p = first + boundary_line_len;

    bool is_final = false;
    p = SkipBoundarySuffix(p, end, is_final);
    if(is_final)
    {
        return MultiForm{};
    }

    const std::string marker = "\n" + boundary_line;
    const size_t marker_len = marker.size();

    auto is_valid_boundary_suffix_char = [](char c) {
        return c == '\r' || c == '\n' || c == '-' || c == ' ' || c == '\t';
    };

    MultiForm form;

    while(p < end)
    {
        const uint8_t* body_start = p;
        const uint8_t* next = nullptr;
        const uint8_t* scan = p;

        while(true)
        {
            scan = std::search(scan, end,
                marker.c_str(),
                marker.c_str() + marker_len);
            if(scan == end)
            {
                next = nullptr;
                break;
            }

            const uint8_t* after = scan + marker_len;
            if(after >= end || is_valid_boundary_suffix_char(*after))
            {
                next = scan;
                break;
            }

            ++scan;
        }

        if(next == nullptr)
        {
            throw MultiFormException("multiform parse error! 'boundary' not complete!");
        }

        size_t body_len = static_cast<size_t>(next - body_start);
        if(body_len > 0 && *(next - 1) == '\r')
        {
            --body_len;
        }

        form.addPart(ParsePart(body_start, body_len));

        p = next + marker_len;
        p = SkipBoundarySuffix(p, end, is_final);
        if(is_final)
        {
            break;
        }
    }

    return form;
}

void MultiForm::addPart(FormPart part)
{
    if(part.name.empty())
    {
        return;
    }
    fields_[part.name].push_back(std::move(part));
}


}