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

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <sstream>
#include <utility>

namespace kit_muduo::http {

namespace {

static constexpr char kHeaderSepCRLF[] = "\r\n\r\n";
static constexpr char kHeaderSepLF[] = "\n\n";

void AppendBytes(std::vector<uint8_t>& output, const std::string& value)
{
    output.insert(output.end(), value.begin(), value.end());
}

void AppendBytes(std::vector<uint8_t>& output, const char* value)
{
    output.insert(output.end(), value, value + std::strlen(value));
}

bool ContainsLineBreak(const std::string& value)
{
    return value.find('\r') != std::string::npos || value.find('\n') != std::string::npos;
}

std::string EscapeQuotedValue(const std::string& value)
{
    if(ContainsLineBreak(value))
    {
        throw MultiFormException("multipart quoted value contains line break");
    }

    std::string escaped;
    escaped.reserve(value.size());
    for(const char ch : value)
    {
        if(ch == '\\' || ch == '"')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::vector<uint8_t> DecodeBase64(const std::string& encoded)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> output;
    int value = 0;
    int bits = -8;
    for(const unsigned char ch : encoded)
    {
        if(ch == '=')
        {
            break;
        }
        const char* position = std::find(std::begin(alphabet), std::end(alphabet) - 1, ch);
        if(position == std::end(alphabet) - 1)
        {
            continue;
        }
        value = (value << 6) | static_cast<int>(position - alphabet);
        bits += 6;
        if(bits >= 0)
        {
            output.push_back(static_cast<uint8_t>((value >> bits) & 0xff));
            bits -= 8;
        }
    }
    return output;
}

bool HeaderNameEquals(const std::string& left, const char* right)
{
    if(left.size() != std::strlen(right))
    {
        return false;
    }
    return std::equal(left.begin(), left.end(), right, [](char lhs, char rhs) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(lhs)))
            == static_cast<char>(std::tolower(static_cast<unsigned char>(rhs)));
    });
}

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


MultiForm::MultiForm(PartList parts)
{
    for(auto& part : parts)
    {
        addPart(std::move(part));
    }
}

MultiForm::MultiForm(const std::vector<char>& config_data)
{
    try
    {
        const auto root = nlohmann::json::parse(config_data.begin(), config_data.end());
        const auto fields_it = root.is_object() ? root.find("fields") : root.end();
        if(fields_it == root.end() || !fields_it->is_array())
        {
            throw MultiFormException("multipart config fields must be an array");
        }

        for(const auto& field : *fields_it)
        {
            if(!field.is_object() || field.value("enabled", true) == false)
            {
                continue;
            }

            FormPart part;
            part.name = field.value("name", "");
            if(part.name.empty())
            {
                continue;
            }

            const auto type = field.value("type", "text");
            if(type == "file")
            {
                part.filename = field.value("filename", "upload.bin");
                if(part.filename.empty())
                {
                    part.filename = "upload.bin";
                }
                auto content_type = field.value("content_type", "application/octet-stream");
                if(content_type.empty())
                {
                    content_type = "application/octet-stream";
                }
                part.meta = MakeContentMetaFromMediaType(content_type);
                if(part.meta.media_type.empty())
                {
                    throw MultiFormException("multipart file content_type invalid: " + content_type);
                }
                part.data = DecodeBase64(field.value("data_base64", ""));
            }
            else
            {
                part.meta = MakeContentMetaFromMediaType("text/plain");
                SetContentTypeParam(part.meta, "charset", "utf-8");
                const auto value = field.value("value", "");
                part.data.assign(value.begin(), value.end());
            }

            addPart(std::move(part));
        }
    }
    catch(const MultiFormException&)
    {
        throw;
    }
    catch(const std::exception& error)
    {
        throw MultiFormException(std::string("multipart config invalid: ") + error.what());
    }
}

std::vector<uint8_t> MultiForm::serialize(const std::string& boundary) const
{
    if(boundary.empty() || ContainsLineBreak(boundary))
    {
        throw MultiFormException("multipart boundary invalid");
    }

    std::vector<uint8_t> output;
    for(const auto& part : ordered_parts_)
    {
        if(part.name.empty())
        {
            throw MultiFormException("multipart field name missing");
        }

        AppendBytes(output, "--");
        AppendBytes(output, boundary);
        AppendBytes(output, "\r\nContent-Disposition: form-data; name=\"");
        AppendBytes(output, EscapeQuotedValue(part.name));
        AppendBytes(output, "\"");
        if(!part.filename.empty())
        {
            AppendBytes(output, "; filename=\"");
            AppendBytes(output, EscapeQuotedValue(part.filename));
            AppendBytes(output, "\"");
        }
        AppendBytes(output, "\r\n");

        const auto content_type = ToContentTypeHeaderValue(part.meta);
        if(!content_type.empty())
        {
            AppendBytes(output, "Content-Type: ");
            AppendBytes(output, content_type);
            AppendBytes(output, "\r\n");
        }

        std::vector<std::pair<std::string, std::string>> headers(part.headers.begin(), part.headers.end());
        std::sort(headers.begin(), headers.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        for(const auto& [name, value] : headers)
        {
            if(name.empty() || ContainsLineBreak(name) || ContainsLineBreak(value)
                || HeaderNameEquals(name, "Content-Disposition")
                || HeaderNameEquals(name, "Content-Type"))
            {
                if(name.empty() || ContainsLineBreak(name) || ContainsLineBreak(value))
                {
                    throw MultiFormException("multipart header invalid");
                }
                continue;
            }
            AppendBytes(output, name);
            AppendBytes(output, ": ");
            AppendBytes(output, value);
            AppendBytes(output, "\r\n");
        }

        AppendBytes(output, "\r\n");
        output.insert(output.end(), part.data.begin(), part.data.end());
        AppendBytes(output, "\r\n");
    }

    AppendBytes(output, "--");
    AppendBytes(output, boundary);
    AppendBytes(output, "--\r\n");
    return output;
}


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
    fields_[part.name].push_back(part);
    ordered_parts_.push_back(std::move(part));
}


}
