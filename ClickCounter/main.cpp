#include <windows.h>
#include <iostream>
#include <cmath>
#include <process.h>
#include <atomic>
#include <conio.h>

#pragma comment(lib, "user32.lib")

std::atomic<bool> g_running(true);
DWORD g_mainThreadId = 0; 
HHOOK g_mouseHook = nullptr;

long long g_leftClick = 0;
long long g_rightClick = 0;
long long g_middleClick = 0;
long long g_wheelTotal = 0;
double g_totalMove = 0.0;
POINT g_lastPos = { 0, 0 };
bool g_firstMove = true;

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT* p = (MSLLHOOKSTRUCT*)lParam;
        switch (wParam) {
        case WM_LBUTTONDOWN: g_leftClick++; break;
        case WM_RBUTTONDOWN: g_rightClick++; break;
        case WM_MBUTTONDOWN: g_middleClick++; break;
        case WM_MOUSEWHEEL: g_wheelTotal++; break;
        case WM_MOUSEMOVE:
            if (!g_firstMove) {
                double dx = p->pt.x - g_lastPos.x;
                double dy = p->pt.y - g_lastPos.y;
                g_totalMove += std::sqrt(dx * dx + dy * dy);
            }
            else g_firstMove = false;
            g_lastPos = p->pt;
            break;
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

unsigned WINAPI PrintThread(LPVOID) {
    while (g_running) {
        system("cls");
        std::cout << "====== 鼠标使用统计 ======\n\n";
        std::cout << "[按键] 左键: " << g_leftClick << " | 右键: " << g_rightClick << " | 中键: " << g_middleClick << "\n";
        std::cout << "[滚轮] 总滚动: " << g_wheelTotal << "\n";
        std::cout << "[移动] 累计距离: " << (long long)g_totalMove << " px\n\n";
        std::cout << ">> 提示：请确保窗口激活，按 [Q] 键安全退出 <<\n";
        if (_kbhit()) {
            g_running = false;
            char ch = _getch();
            if (ch == 'q' || ch == 'Q') {
                std::cout << "\n检测到退出指令，正在关闭程序...\n";
                PostThreadMessage(g_mainThreadId, WM_QUIT, 0, 0);
                break;
            }
        }

        Sleep(200);
    }
    return 0;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    g_mainThreadId = GetCurrentThreadId(); 

    std::cout << "正在安装鼠标 Hook..." << std::endl;

    g_mouseHook = SetWindowsHookEx(
        WH_MOUSE_LL,
        LowLevelMouseProc,
        GetModuleHandle(nullptr),
        0
    );

    if (!g_mouseHook) {
        std::cerr << "Hook 安装失败！" << std::endl;
        return -1;
    }

    _beginthreadex(nullptr, 0, PrintThread, nullptr, 0, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    std::cout << "\n正在卸载钩子并关闭程序..." << std::endl;
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }

    return 0;
}