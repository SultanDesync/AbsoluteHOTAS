#pragma once
#include <windows.h>

class InpUtils {
public:
    static void SendMouseMove(long dx, long dy) {
        INPUT input = { 0 };
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        SendInput(1, &input, sizeof(INPUT));
    }

    static void SendKey(uint16_t scanCode, bool keyUp) {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = scanCode;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (keyUp ? KEYEVENTF_KEYUP : 0);
        SendInput(1, &input, sizeof(INPUT));
    }
};
