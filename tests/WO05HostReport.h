#pragma once
#include <atomic>
namespace Cosmic { class FakeSerialTransport; }
struct WO05HostReport
{
    int schedule = 0, transition = 0, storage = 0;
    std::atomic<int> failures{0}, detached{0}, destroyed{0};
    std::atomic<long long> closeMs{0};
    std::atomic<unsigned long long> closeRequestTick{0};
    bool policy = false;
    std::atomic<long long> closeRequestUnixMs{0};
    Cosmic::FakeSerialTransport* (*createTransport)() = nullptr;
};
