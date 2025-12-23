#pragma once

namespace Engine {
    enum class Key {
        Unknown = 0,

        // Mouse Buttons (using virtual key codes structure for consistency if needed, 
        // though usually handled separately. Keeping standard keys here)
        
        // Printable keys
        Space = 0x20,
        Apostrophe = 0xDE, // VK_OEM_7
        Comma = 0xBC,      // VK_OEM_COMMA
        Minus = 0xBD,      // VK_OEM_MINUS
        Period = 0xBE,     // VK_OEM_PERIOD
        Slash = 0xBF,      // VK_OEM_2
        
        D0 = 0x30,
        D1 = 0x31,
        D2 = 0x32,
        D3 = 0x33,
        D4 = 0x34,
        D5 = 0x35,
        D6 = 0x36,
        D7 = 0x37,
        D8 = 0x38,
        D9 = 0x39,

        Semicolon = 0xBA,  // VK_OEM_1
        Equal = 0xBB,      // VK_OEM_PLUS

        A = 0x41,
        B = 0x42,
        C = 0x43,
        D = 0x44,
        E = 0x45,
        F = 0x46,
        G = 0x47,
        H = 0x48,
        I = 0x49,
        J = 0x4A,
        K = 0x4B,
        L = 0x4C,
        M = 0x4D,
        N = 0x4E,
        O = 0x4F,
        P = 0x50,
        Q = 0x51,
        R = 0x52,
        S = 0x53,
        T = 0x54,
        U = 0x55,
        V = 0x56,
        W = 0x57,
        X = 0x58,
        Y = 0x59,
        Z = 0x5A,

        LeftBracket = 0xDB,  // VK_OEM_4
        Backslash = 0xDC,    // VK_OEM_5
        RightBracket = 0xDD, // VK_OEM_6
        GraveAccent = 0xC0,  // VK_OEM_3

        // Function keys
        Escape = 0x1B,      // VK_ESCAPE
        Enter = 0x0D,       // VK_RETURN
        Tab = 0x09,         // VK_TAB
        Backspace = 0x08,   // VK_BACK
        Insert = 0x2D,      // VK_INSERT
        Delete = 0x2E,      // VK_DELETE
        Right = 0x27,       // VK_RIGHT
        Left = 0x25,        // VK_LEFT
        Down = 0x28,        // VK_DOWN
        Up = 0x26,          // VK_UP
        PageUp = 0x21,      // VK_PRIOR
        PageDown = 0x22,    // VK_NEXT
        Home = 0x24,        // VK_HOME
        End = 0x23,         // VK_END
        CapsLock = 0x14,    // VK_CAPITAL
        ScrollLock = 0x91,  // VK_SCROLL
        NumLock = 0x90,     // VK_NUMLOCK
        PrintScreen = 0x2C, // VK_SNAPSHOT
        Pause = 0x13,       // VK_PAUSE

        F1 = 0x70,
        F2 = 0x71,
        F3 = 0x72,
        F4 = 0x73,
        F5 = 0x74,
        F6 = 0x75,
        F7 = 0x76,
        F8 = 0x77,
        F9 = 0x78,
        F10 = 0x79,
        F11 = 0x7A,
        F12 = 0x7B,

        LeftShift = 0xA0,   // VK_LSHIFT
        LeftControl = 0xA2, // VK_LCONTROL
        LeftAlt = 0xA4,     // VK_LMENU
        RightShift = 0xA1,  // VK_RSHIFT
        RightControl = 0xA3,// VK_RCONTROL
        RightAlt = 0xA5,    // VK_RMENU
    };

    enum class MouseButton {
        Left = 0,
        Right = 1,
        Middle = 2,
        Button4 = 3,
        Button5 = 4
    };
}
