#include "SerialPort.h"
#include "serial/Win32SerialTransport.h"
#include <stdexcept>
#include <utility>
#include <algorithm>

namespace Cosmic
{
    // A stalled open never borrows SerialPort or app/plugin objects. Its worker
    // owns this block and the transport through late-result cleanup.
    struct SerialPort::OpenJob
    {
        HANDLE stop = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        HANDLE done = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        std::mutex mutex;
        bool abandoned = false;
        bool success = false;
        ~OpenJob() { if (stop) CloseHandle(stop); if (done) CloseHandle(done); }
    };

    SerialPort::SerialPort() : m_Transport(std::make_shared<Win32SerialTransport>()) {}
    SerialPort::SerialPort(std::unique_ptr<ISerialTransport> transport)
        : m_Transport(std::move(transport))
    {
        if (!m_Transport) throw std::invalid_argument("SerialPort requires a transport");
    }
    SerialPort::~SerialPort() { Close(); }

    bool SerialPort::IsOpen() const
    {
        const_cast<SerialPort*>(this)->AdoptOpen();
        return m_Connected.load();
    }
    SerialPort::State SerialPort::GetState() const
    {
        const_cast<SerialPort*>(this)->AdoptOpen();
        return m_State.load();
    }

    void SerialPort::AdoptOpen()
    {
        if (m_State.load() != State::Connecting || !m_OpenJob ||
            WaitForSingleObject(m_OpenJob->done, 0) != WAIT_OBJECT_0) return;
        if (m_ConnectThread.joinable()) m_ConnectThread.join();
        // Only the owner publishes sessions and creates readers. No worker tail
        // can overwrite the reader's Failed state (H2/H3).
        if (!m_OpenJob->success) { m_State.store(State::Failed); return; }
        m_StopEvent = m_OpenJob->stop;
        m_Connected.store(true);
        m_ConnectionGeneration.fetch_add(1);
        m_State.store(State::Open);
        m_ReadThread = std::thread(&SerialPort::ReadLoop, this);
    }

    bool SerialPort::Open(const std::string& portName, uint32_t baudRate)
    {
        // Lifecycle/status calls are owner-thread only. Write is the supported
        // concurrent operation. Open remains intentionally blocking.
        if (GetState() == State::Connecting) return false;
        BeginOpen(portName, baudRate);
        if (m_State.load() != State::Connecting) return false;
        WaitForSingleObject(m_OpenJob->done, INFINITE);
        AdoptOpen();
        return m_Connected.load();
    }

    void SerialPort::BeginOpen(const std::string& portName, uint32_t baudRate)
    {
        if (GetState() == State::Connecting) return;
        // A cancelled non-cooperative driver still owns this transport. Never
        // stack a worker or reuse its handle before late cleanup finishes.
        if (m_OpenJob && WaitForSingleObject(m_OpenJob->done, 0) != WAIT_OBJECT_0) return;
        if (m_ConnectThread.joinable()) m_ConnectThread.join();
        CloseReadSession();
        m_OpenJob = std::make_shared<OpenJob>();
        if (!m_OpenJob->stop || !m_OpenJob->done) { m_State.store(State::Failed); return; }
        m_StopEvent = m_OpenJob->stop;
        m_State.store(State::Connecting);
        auto job = m_OpenJob;
        auto transport = m_Transport;
        m_ConnectThread = std::thread([job, transport, portName, baudRate]()
        {
            bool ok = false;
            try
            {
                if (WaitForSingleObject(job->stop, 0) != WAIT_OBJECT_0)
                    ok = transport->Open(portName, baudRate, job->stop);
            }
            catch (...) { ok = false; }
            {
                std::lock_guard<std::mutex> lock(job->mutex);
                if (job->abandoned || !ok)
                {
                    ok = false;
                }
                job->success = ok;
            }
            if (!ok) transport->Close();
            SetEvent(job->done);
        });
    }

    void SerialPort::ReadLoop()
    {
        char buf[256];
        while (m_Connected.load())
        {
            const ReadResult r = m_Transport->Read(buf, sizeof(buf), m_StopEvent);
            if (r.status == ReadResult::Status::Aborted) break;
            if (r.status == ReadResult::Status::Dropped)
            {
                m_Connected.store(false);
                m_State.store(State::Failed);
                break;
            }
            if (r.bytes > 0)
            {
                std::lock_guard<std::mutex> lock(m_BufferMutex);
                m_ReceivedBytes.fetch_add(r.bytes);
                const size_t accepted = (std::min)(r.bytes, ReceiveCapacity - m_DataBuffer.size());
                m_DataBuffer.append(buf, accepted);
                m_OverflowBytes.fetch_add(r.bytes - accepted);
            }
        }
    }

    std::string SerialPort::FlushBuffer()
    {
        std::lock_guard<std::mutex> lock(m_BufferMutex);
        std::string result;
        result.swap(m_DataBuffer);
        return result;
    }

    bool SerialPort::Write(const void* data, size_t length)
    {
        // An app-owned writer may run while the owner closes. Stop is signalled
        // first; the device/event remain alive until this lock is released.
        std::lock_guard<std::mutex> lock(m_WriteMutex);
        if (!m_Connected.load() || !data || length == 0) return false;
        return m_Transport->Write(data, length, m_StopEvent);
    }

    void SerialPort::CloseReadSession()
    {
        m_Connected.store(false);
        if (m_StopEvent) SetEvent(m_StopEvent);
        if (m_ReadThread.joinable()) m_ReadThread.join();
        std::lock_guard<std::mutex> writeLock(m_WriteMutex);
        m_Transport->Close();
        m_StopEvent = nullptr; // event lifetime belongs to OpenJob
        std::lock_guard<std::mutex> bufferLock(m_BufferMutex);
        m_DiscardedOnCloseBytes.fetch_add(m_DataBuffer.size());
        m_DataBuffer.clear(); // partial bytes never survive reconnect
    }

    void SerialPort::Close()
    {
        if (m_OpenJob)
        {
            {
                std::lock_guard<std::mutex> lock(m_OpenJob->mutex);
                m_OpenJob->abandoned = true;
                SetEvent(m_OpenJob->stop); // cancellation BEFORE waiting (H1)
            }
            if (m_ConnectThread.joinable())
            {
                // Repeat to cover Close just before CreateFile enters the kernel.
                const ULONGLONG deadline = GetTickCount64() + 100;
                while (WaitForSingleObject(m_OpenJob->done, 5) == WAIT_TIMEOUT &&
                       GetTickCount64() < deadline)
                    CancelSynchronousIo(m_ConnectThread.native_handle());
                if (WaitForSingleObject(m_OpenJob->done, 0) == WAIT_OBJECT_0)
                    m_ConnectThread.join();
                else
                    m_ConnectThread.detach(); // owns ONLY job + transport; never this
            }
            if (WaitForSingleObject(m_OpenJob->done, 0) != WAIT_OBJECT_0)
            {
                // No reader exists. Late cleanup belongs exclusively to the open
                // worker; no app/plugin callback is reachable from it.
                m_StopEvent = nullptr;
                m_State.store(State::Idle);
                return;
            }
        }
        CloseReadSession();
        m_State.store(State::Idle);
    }

    std::vector<std::string> SerialPort::GetAvailablePorts() { return Win32SerialTransport{}.List(); }
    std::vector<std::string> SerialPort::ListPorts() { return m_Transport->List(); }
}
