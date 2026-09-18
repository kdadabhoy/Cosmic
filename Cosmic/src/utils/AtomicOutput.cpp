#include "utils/AtomicOutput.h"
#include <Windows.h>
#include <atomic>
#include <locale>
namespace Cosmic
{
static thread_local WriteFault writeFault;
void SetWriteFaultForTesting(WriteFault fault)
{
    writeFault = std::move(fault);
}
WriteFault GetWriteFaultForTesting()
{
    return writeFault;
}
bool AtomicOutput::Fail(const char *stage)
{
    return fault && fault(displayPath, stage);
}
AtomicOutput::AtomicOutput(const std::string &path)
    : target(std::filesystem::u8path(path)), displayPath(path), fault(writeFault)
{
    static std::atomic<unsigned long long> serial{0};
    temporary = target;
    temporary += L".pending-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial);
    std::error_code ec;
    auto parent = target.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);
    if (ec || Fail("open"))
        return;
    stream.open(temporary, std::ios::binary | std::ios::trunc);
    stream.imbue(std::locale::classic());
}
AtomicOutput::~AtomicOutput()
{
    if (stream.is_open())
        stream.close();
    if (!published)
    {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
    }
}
bool AtomicOutput::Finish()
{
    if (!stream.is_open())
        return false;
    const bool writeFailed = Fail("write");
    if (writeFailed)
        stream.setstate(std::ios::badbit);
    stream.flush();
    const bool flushFailed = Fail("flush");
    if (flushFailed)
        stream.setstate(std::ios::badbit);
    const bool good = stream.good();
    stream.close();
    const bool closeFailed = Fail("close");
    if (closeFailed)
        stream.setstate(std::ios::badbit);
    if (Fail("partial-write"))
    {
        std::error_code ec;
        auto size = std::filesystem::file_size(temporary, ec);
        if (!ec)
            std::filesystem::resize_file(temporary, size / 2, ec);
        return false;
    }
    finished = good && !stream.fail() && !writeFailed && !flushFailed && !closeFailed;
    return finished;
}
bool AtomicOutput::Publish()
{
    if (!finished || Fail("publish"))
        return false;
    // Same-directory atomic replacement: an interrupted write never truncates target.
    published = MoveFileExW(temporary.c_str(), target.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    return published;
}
} // namespace Cosmic
