#pragma once
// Win32SerialTransport.h
//
// The default, shipping ISerialTransport: the exact Win32 serial syscalls that
// SerialPort used before the WO-04 seam, moved verbatim behind the boundary.
// SerialPort's default constructor installs one of these, so production behaviour
// is unchanged. It is instantiated only inside Cosmic.dll (by SerialPort and by
// the static SerialPort::GetAvailablePorts helper); tests never touch it.

#include "serial/ISerialTransport.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Cosmic
{
	class Win32SerialTransport final : public ISerialTransport
	{
	public:
		Win32SerialTransport() = default;
		~Win32SerialTransport() override;

		bool Open(const std::string& portName, std::uint32_t baudRate, void* stopEvent) override;
		ReadResult Read(char* buf, std::size_t cap, void* stopEvent) override;
		bool Write(const void* data, std::size_t length) override;
		void Close() override;
		std::vector<std::string> List() override;

	private:
#ifdef _WIN32
		// The device handle and the overlapped-read state live here now — they are
		// pure transport. The stop event stays in SerialPort (it is synchronization,
		// part of the state machine) and is passed into Open/Read as an opaque handle.
		HANDLE     m_Handle = INVALID_HANDLE_VALUE;
		OVERLAPPED m_ReadOv = {};   // reused across reads; its hEvent is created in Open
#endif
	};
}
