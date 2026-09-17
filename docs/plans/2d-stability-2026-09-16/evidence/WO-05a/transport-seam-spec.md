# WO-05a → WO-04 — injectable serial transport seam specification

**Owner to build:** WO-04. **Consumer:** WO-05 (connected-state matrix) and the WO-05a regressions.
**Constraint (rule 5, honest gates):** inject **only at the OS boundary**. The parser, the connection
state machine (`BeginOpen`/`Open`/`Close`/`CloseReadSession`, `m_Abandon`, the stop-event, the `State`
enum and all transitions, both `std::thread`s), and the auto-reconnect policy in `SerialLink` **stay
exactly as they ship**. The seam replaces the four Win32 syscalls, nothing else. No private-field setters
to force a state; no second simulated app.

## 1. Why a seam is needed (recap of KI-2, extended)

Everything interesting about the reported bug lives in the **connected state** — a port that opens,
receives bytes, and later drops. Today that state has **no headless entry point**:

- `SerialLink` selects a port only via `RefreshPorts()` from `SerialPort::GetAvailablePorts()` (registry)
  — there is no public port setter (`SerialLink.cpp:21-27`, `.h:74-96`).
- `SerialPort` opens only via `CreateFileA` on a real device name (`SerialPort.cpp:99-100`).
- So the auto-reconnect `BeginOpen` at `SerialLink.cpp:67` and every connected-state edge
  (`IsReceiving`, `ConsumeJustConnected` RX reset, drop→`Failed`→retry) are unreachable without hardware.

The seam makes open/read/write/close and port-discovery injectable so tests can open a fake port, feed
bytes, and drop it deterministically — while the real threading/abandon/stop-event logic runs unchanged.

## 2. The boundary — exactly the calls to extract

Map of every OS call SerialPort makes and where the fake substitutes:

| Real call (file:line) | Seam method | Fake behaviour under test control |
| --- | --- | --- |
| `CreateFileA` + `GetCommState`/`SetCommState`/`SetCommTimeouts` (`SerialPort.cpp:99-127`) | `Open(port,baud,stopEvent)` | return Ok immediately / Failed immediately / **block N ms then Ok/Failed**, honouring `stopEvent` (for the abandon path) |
| overlapped `ReadFile` + `WaitForMultipleObjects(stop, readDone)` + `CancelIoEx` + `GetOverlappedResult` (`SerialPort.cpp:160-194`) | `Read(buf,cap,stopEvent) -> ReadResult{bytes, Dropped, Aborted}` | block until: test `PushBytes()` (return bytes), test `SignalDrop()` (return `Dropped` → drives `State::Failed`), or `stopEvent` (return `Aborted`) |
| overlapped `WriteFile` (`SerialPort.cpp:231-252`) | `Write(data,len) -> bool` | record bytes; return configurable success/failure (feeds WO-06 write-failure) |
| `CancelIoEx` + `CloseHandle` (`SerialPort.cpp:263-283`) | `Close()` | release the fake device; unblock any pending `Read`/`Open` |
| `GetAvailablePorts` registry scan (`SerialPort.cpp:311-341`) | `List() -> vector<string>` | return a test-set list so `SerialLink::RefreshPorts` can select a fake port |

**Keep real (do NOT move into the seam):** `m_StopEvent` and the overlapped-event orchestration are
*synchronization*, not transport — they are the state machine and must stay in `SerialPort` so the test
exercises the true abandon/stop path. The fake's `Open`/`Read` merely *observe* `stopEvent` to unblock.

## 3. Proposed shape

```cpp
// serial/ISerialTransport.h  (new)
namespace Cosmic {
struct ReadResult { size_t bytes = 0; enum class Status { Data, Dropped, Aborted } status = Status::Data; };

class ISerialTransport {
public:
    virtual ~ISerialTransport() = default;
    // Blocking open; must return promptly once `stopEvent` is signalled (abandon path).
    virtual bool Open(const std::string& port, uint32_t baud, void* stopEvent) = 0;
    // Blocking read; returns on data, a device drop, or `stopEvent`.
    virtual ReadResult Read(char* buf, size_t cap, void* stopEvent) = 0;
    virtual bool Write(const void* data, size_t len) = 0;
    virtual void Close() = 0;
    virtual std::vector<std::string> List() = 0;
};
} // Win32SerialTransport = today's code verbatim; the default.
```

Injection points (both default to the Win32 transport, so shipping behaviour is byte-identical):

```cpp
class COSMIC_API SerialPort {
public:
    SerialPort();                                              // = Win32 transport (unchanged default)
    explicit SerialPort(std::unique_ptr<ISerialTransport>);   // test seam
    // ... rest unchanged ...
};

class COSMIC_API SerialLink {
public:
    SerialLink() = default;                                   // unchanged default
    explicit SerialLink(std::unique_ptr<ISerialTransport>);   // forwards into its SerialPort
    // ... rest unchanged ...
};
```

`DoOpen`/`ReadLoop`/`Write`/`CloseReadSession` call `m_Transport->…` instead of the raw Win32 calls; the
thread creation, `m_Abandon`, `m_StopEvent`, and `State` stores stay exactly where they are.

## 4. `FakeSerialTransport` controls WO-05 needs

```cpp
class FakeSerialTransport : public ISerialTransport {
public:
    // open behaviour
    void SetOpenResult(bool ok);
    void SetOpenBlockMs(int ms);            // ← reproduce H1: block inside Open()
    // read stream
    void PushBytes(std::string_view);       // ← reach IsReceiving / ConsumeJustConnected RX reset
    void SignalDrop();                       // ← mid-session device drop → State::Failed → reconnect
    // discovery
    void SetAvailablePorts(std::vector<std::string>);  // ← let SerialLink select a fake port
    // observation
    int  OpenCount() const; int CloseCount() const; std::string Written() const;
};
```

## 5. Acceptance the seam unblocks (WO-05 matrix)

1. **H1 exit-hang regression** — `SetOpenBlockMs(20000)`, `Connect()`, then teardown mid-open; assert
   `Close()` returns within the abort budget after the fix. **Failing-before / passing-after.**
2. **Connected-state edges** — `SetAvailablePorts({"COM_FAKE"})` + `Connect()` + `PushBytes()`; assert
   `IsReceiving()` true, `ConsumeJustConnected()` one-shot, `Poll()` returns the exact bytes.
3. **H3 drop→reconnect churn** — `PushBytes` then `SignalDrop`; assert `State::Failed`, auto-reconnect
   re-opens, and repeated drop/reopen/close is race-free and leak-free under a soak.
4. **Write-failure (WO-06 shares the seam)** — `Write` returns false; assert the app surfaces it.

## 6. Scope guard
The seam is a **test seam**, shipped disabled-by-default (Win32 transport). It is not a refactor of the
connection logic and must not change any observable behaviour of the shipping build; WO-04 verifies that
by re-running `test_serial_lifecycle.cpp` green against the Win32 default before wiring any fake.
