// scene/FlowKeyBridge.cpp — keyboard edges into "key:<Name>" flow signals (AP-01).

#include "scene/FlowKeyBridge.h"
#include "scene/FlowMachine.h"
#include "core/Input.h"
#include "core/Log.h"
#include "codes/KeyCodes.h"

namespace Cosmic
{
    int FlowKeyBridge::KeyCodeFor(const std::string& name)
    {
        if (name == "Escape")    return CS_KEY_ESCAPE;
        if (name == "Space")     return CS_KEY_SPACE;
        if (name == "Enter")     return CS_KEY_ENTER;
        if (name == "Tab")       return CS_KEY_TAB;
        if (name == "Backspace") return CS_KEY_BACKSPACE;
        if (name == "Up")        return CS_KEY_UP;
        if (name == "Down")      return CS_KEY_DOWN;
        if (name == "Left")      return CS_KEY_LEFT;
        if (name == "Right")     return CS_KEY_RIGHT;
        if (name.size() == 1)
        {
            const char c = name[0];
            if (c >= 'A' && c <= 'Z') return CS_KEY_A + (c - 'A');
            if (c >= '0' && c <= '9') return CS_KEY_0 + (c - '0');
            return -1;
        }
        if ((name.size() == 2 || name.size() == 3) && name[0] == 'F')
        {
            int n = 0;
            for (size_t i = 1; i < name.size(); ++i)
            {
                if (name[i] < '0' || name[i] > '9') return -1;
                n = n * 10 + (name[i] - '0');
            }
            if (n >= 1 && n <= 12) return CS_KEY_F1 + (n - 1);
        }
        return -1;
    }

    void FlowKeyBridge::Bind(const FlowAsset& asset, Probe probe)
    {
        m_Keys.clear();
        m_Probe = std::move(probe);
        for (const std::string& signal : FlowMachine::KeySignals(asset))
        {
            const std::string name = signal.substr(4);   // after "key:"
            const int code = KeyCodeFor(name);
            if (code < 0)
            {
                if (m_Warned.insert(name).second)
                    CS_CORE_WARN("FlowKeyBridge: unknown key name '{0}' in transition '{1}' — not bound.", name, signal);
                continue;
            }
            m_Keys.push_back({ signal, code, false });
        }
    }

    void FlowKeyBridge::Poll(FlowMachine& machine)
    {
        for (Bound& b : m_Keys)
        {
            const bool pressed = m_Probe ? m_Probe(b.Code) : Input::IsKeyPressed(b.Code);
            if (pressed && !b.Prev)
                machine.FeedSignal(b.Signal);
            b.Prev = pressed;
        }
    }

    void FlowKeyBridge::Clear()
    {
        m_Keys.clear();
        m_Probe = nullptr;
    }
}
