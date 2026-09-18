#include "utils/DataExport.h"
#include "core/Log.h"
#include "utils/AtomicOutput.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>

namespace Cosmic
{
namespace
{
std::string Trim(const std::string &s)
{
    auto a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
bool Number(const std::string &s, double &out)
{
    auto t = Trim(s);
    if (t.empty())
        return false;
    size_t i = 0;
    if (t[i] == '+' || t[i] == '-')
        ++i;
    bool digits = false;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9')
    {
        digits = true;
        ++i;
    }
    if (i < t.size() && t[i] == '.')
    {
        ++i;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9')
        {
            digits = true;
            ++i;
        }
    }
    if (!digits)
        return false;
    if (i < t.size() && (t[i] == 'e' || t[i] == 'E'))
    {
        ++i;
        if (i < t.size() && (t[i] == '+' || t[i] == '-'))
            ++i;
        size_t start = i;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9')
            ++i;
        if (i == start)
            return false;
    }
    if (i != t.size())
        return false;
    const char *begin = t.data();
    if (*begin == '+')
        ++begin;
    auto r = std::from_chars(begin, t.data() + t.size(), out, std::chars_format::general);
    return r.ec == std::errc{} && r.ptr == t.data() + t.size() && std::isfinite(out);
}
bool Headers(const std::vector<std::string> &h)
{
    std::set<std::string> names;
    for (const auto &s : h)
    {
        auto t = Trim(s);
        if (t == "nan" || t == "NaN" || t == "inf" || t == "Inf" || t == "infinity")
            return false;
        if (t.empty() || !((t[0] >= 'A' && t[0] <= 'Z') || (t[0] >= 'a' && t[0] <= 'z') || t[0] == '_') ||
            t.find_first_of(",\r\n\"\t") != std::string::npos || !names.insert(t).second)
            return false;
        for (unsigned char c : t)
            if (c < 32 || c > 126)
                return false;
    }
    return !h.empty();
}
void Header(std::ofstream &f, const std::vector<std::string> &h)
{
    for (size_t i = 0; i < h.size(); ++i)
    {
        if (i)
            f << ',';
        f << h[i];
    }
    f << '\n';
}
bool Complete(AtomicOutput &out, const std::string &path)
{
    if (out.Finish() && out.Publish())
        return true;
    CS_CORE_ERROR("DataExport: write/flush/close/publication failed for '{}'.", path);
    return false;
}
class NumericBuffer
{
    std::ofstream &stream;
    std::string bytes;

  public:
    explicit NumericBuffer(std::ofstream &s) : stream(s)
    {
        bytes.reserve(1024 * 1024 + 128);
    }
    void Cell(double v)
    {
        char text[64];
        auto result = std::to_chars(text, text + sizeof(text), v, std::chars_format::general,
                                    std::numeric_limits<double>::max_digits10);
        if (result.ec != std::errc{})
        {
            stream.setstate(std::ios::badbit);
            return;
        }
        bytes.append(text, result.ptr);
    }
    void Separator(char c)
    {
        bytes.push_back(c);
        if (bytes.size() >= 1024 * 1024)
            Flush();
    }
    void Flush()
    {
        if (!bytes.empty())
        {
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            bytes.clear();
        }
    }
};
} // namespace
bool DataExport::WriteCSV(const std::string &path, const std::vector<std::string> &headers,
                          const std::vector<std::vector<double>> &columns)
{
    if (headers.size() != columns.size() || !Headers(headers))
        return false;
    const size_t rows = columns[0].size();
    for (const auto &c : columns)
    {
        if (c.size() != rows)
            return false;
        for (double d : c)
            if (!std::isfinite(d))
                return false;
    }
    AtomicOutput output(path);
    auto &f = output.stream;
    if (!f.is_open())
        return false;
    f.precision(std::numeric_limits<double>::max_digits10);
    Header(f, headers);
    NumericBuffer buffer(f);
    for (size_t row = 0; row < rows; ++row)
    {
        for (size_t col = 0; col < columns.size(); ++col)
        {
            if (col)
                buffer.Separator(',');
            buffer.Cell(columns[col][row]);
        }
        buffer.Separator('\n');
    }
    buffer.Flush();
    return Complete(output, path);
}
bool DataExport::AppendRow(const std::string &path, const std::vector<double> &values)
{
    if (values.empty())
        return false;
    for (double v : values)
        if (!std::isfinite(v))
            return false;
    AtomicOutput output(path);
    auto &f = output.stream;
    if (!f.is_open())
        return false;
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::u8path(path), ec))
    {
        std::ifstream old(std::filesystem::u8path(path), std::ios::binary);
        if (!old.is_open())
            return false;
        char buf[8192];
        while (old.read(buf, sizeof(buf)) || old.gcount())
            f.write(buf, old.gcount());
        if (!old.eof())
            return false;
    }
    else if (ec)
        return false;
    f.precision(std::numeric_limits<double>::max_digits10);
    NumericBuffer buffer(f);
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i)
            buffer.Separator(',');
        buffer.Cell(values[i]);
    }
    buffer.Separator('\n');
    buffer.Flush();
    return Complete(output, path);
}
bool DataExport::WriteCircularBuffer(const std::string &path, const std::vector<std::string> &headers,
                                     const std::vector<const float *> &buffers, int count, int offset,
                                     int capacity)
{
    if (headers.size() != buffers.size() || !Headers(headers) || count < 0 || capacity <= 0 ||
        count > capacity || offset < 0 || offset >= capacity)
        return false;
    for (const auto *b : buffers)
    {
        if (!b)
            return false;
        for (int i = 0; i < count; ++i)
            if (!std::isfinite(b[(static_cast<size_t>(offset) + i) % capacity]))
                return false;
    }
    AtomicOutput output(path);
    auto &f = output.stream;
    if (!f.is_open())
        return false;
    f.precision(std::numeric_limits<double>::max_digits10);
    Header(f, headers);
    NumericBuffer buffer(f);
    for (int i = 0; i < count; ++i)
    {
        auto idx = (static_cast<size_t>(offset) + i) % capacity;
        for (size_t c = 0; c < buffers.size(); ++c)
        {
            if (c)
                buffer.Separator(',');
            buffer.Cell(buffers[c][idx]);
        }
        buffer.Separator('\n');
    }
    buffer.Flush();
    return Complete(output, path);
}
bool DataExport::LoadCSV(const std::string &path, std::vector<std::vector<double>> &out,
                         std::vector<std::string> *headers)
{
    out.clear();
    if (headers)
        headers->clear();
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f.is_open())
        return false;
    std::vector<std::vector<double>> cols;
    std::vector<std::string> names;
    std::string line;
    bool first = true, data = false;
    size_t lineNo = 0;
    auto fail = [&](const char *reason) {
        CS_CORE_ERROR("DataExport::LoadCSV '{}' line {}: {}", path, lineNo, reason);
        return false;
    };
    while (std::getline(f, line))
    {
        ++lineNo;
        if (first && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            line.erase(0, 3);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (Trim(line).empty())
            continue;
        if (line.find_first_of("\"\r") != std::string::npos)
            return fail("quoted cells or embedded CR are unsupported");
        std::vector<std::string> cells;
        size_t start = 0;
        for (;;)
        {
            auto comma = line.find(',', start);
            cells.push_back(line.substr(start, comma == std::string::npos ? comma : comma - start));
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        if (first)
        {
            first = false;
            cols.resize(cells.size());
            double v;
            bool numeric =
                std::all_of(cells.begin(), cells.end(), [&](const auto &s) { return Number(s, v); });
            if (!numeric)
            {
                if (!Headers(cells))
                    return fail("invalid header or first numeric row");
                names = cells;
                continue;
            }
        }
        if (cells.size() != cols.size())
            return fail("ragged row");
        for (size_t i = 0; i < cells.size(); ++i)
        {
            double v;
            if (!Number(cells[i], v))
                return fail("blank, nonfinite, out-of-range or malformed number");
            cols[i].push_back(v);
        }
        data = true;
    }
    if (!f.eof() || !data)
        return fail("unreadable file or no numeric data rows");
    out = std::move(cols);
    if (headers)
        *headers = std::move(names);
    return true;
}
} // namespace Cosmic
