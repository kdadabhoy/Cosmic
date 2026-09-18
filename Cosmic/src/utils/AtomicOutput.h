#pragma once
#include "core/Core.h"
#include <filesystem>
#include <fstream>
#include <functional>

namespace Cosmic
{
// A seam at stream/OS outcomes, never an alternate serializer. Set only while
// no writes are active. Recorder captures the hook before starting its worker.
using WriteFault = std::function<bool(const std::string &, const char *)>;
COSMIC_API void SetWriteFaultForTesting(WriteFault fault);
COSMIC_API WriteFault GetWriteFaultForTesting();
class COSMIC_API AtomicOutput
{
  public:
    explicit AtomicOutput(const std::string &path);
    ~AtomicOutput();
    std::ofstream stream;
    bool Finish();
    bool Publish();

  private:
    std::filesystem::path target, temporary;
    std::string displayPath;
    WriteFault fault;
    bool finished = false, published = false;
    bool Fail(const char *stage);
};
} // namespace Cosmic
