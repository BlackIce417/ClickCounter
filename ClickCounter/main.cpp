#include <windows.h>
#include <iostream>
#include <cmath>
#include <process.h>
#include <atomic>
#include <conio.h>
#include <shlwapi.h>
#include <ctime>

#include "resource.h"
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "user32.lib")

#define WM_TRAYICON (WM_USER + 1)
NOTIFYICONDATA g_nid = { sizeof(g_nid) };

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
HWND g_hDlg = nullptr;
CHAR g_path[MAX_PATH] = { 0 };


BOOL GetCurrentPath(_Out_ CHAR* szPath) {
    if (szPath == nullptr) {
        return FALSE;
    }
    GetModuleFileNameA(NULL, szPath, MAX_PATH);
    CHAR* p = strrchr(szPath, '\\');
    if (p) {
        *(p + 1) = '\0';
	}
	return TRUE;
}

void GetFormatTime(WCHAR* outStr, size_t size) {
    time_t now = time(0);
    struct tm ltm;
    localtime_s(&ltm, &now);

    swprintf_s(outStr, size, L"%04d/%02d/%02d %02d:%02d",
        ltm.tm_year + 1900,  
        ltm.tm_mon + 1,      
        ltm.tm_mday,
        ltm.tm_hour,
        ltm.tm_min);
}

VOID ExportData() {

    CHAR pszSavePath[MAX_PATH] = { 0 };
    GetCurrentPath(pszSavePath);
    PathAppendA(pszSavePath, "Export.txt");

    WCHAR pTimeBuf[64] = { 0 };
    GetFormatTime(pTimeBuf, 64);

    WCHAR content[256];
    swprintf_s(content, L"鼠标统计报告\n导出时间: %s\n----------\n左键: %lld\n右键: %lld\n中键: %lld\n距离: %.2f px",
        pTimeBuf, g_leftClick, g_rightClick, g_middleClick, g_totalMove);

    FILE* fp = nullptr;
    fopen_s(&fp, pszSavePath, "w, ccs=UTF-8");
    if (fp) {
        fwprintf(fp, L"%s", content);
        fclose(fp);
        WCHAR wbuf[256] = { 0 };
        swprintf_s(wbuf, L"导出成功，文件保存至：%S", pszSavePath);
        MessageBox(g_hDlg, wbuf, L"提示", MB_OK | MB_ICONINFORMATION);
    }
}

INT_PTR CALLBACK StatsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        // 设置一个定时器，每 100 毫秒刷新一次 UI
        SetTimer(hDlg, 1, 100, NULL);
        return (INT_PTR)TRUE;

    case WM_TIMER:
        if (wParam == 1) {
            // 将全局变量格式化并更新到对话框的 Static Text 控件上
            SetDlgItemInt(hDlg, IDC_LCLICK_VAL, (UINT)g_leftClick, FALSE);
            SetDlgItemInt(hDlg, IDC_RCLICK_VAL, (UINT)g_rightClick, FALSE);
			SetDlgItemInt(hDlg, IDC_MCLICK_VAL, (UINT)g_middleClick, FALSE);

            WCHAR distBuf[64];
            double meters = g_totalMove * 0.00026;
            if (meters < 1.0) {
                swprintf_s(distBuf, L"%.0f px", g_totalMove);
            }
            else {
                swprintf_s(distBuf, L"%.2f m", meters); 
            }
            SetDlgItemText(hDlg, IDC_MOUSEMOVE_VAL, distBuf);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) {
            KillTimer(hDlg, 1);
            DestroyWindow(hDlg);
            g_hDlg = NULL;
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_EXPORT) {
            ExportData();
        }
        break;
    }
    return (INT_PTR)FALSE;
}

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
        WCHAR buf[256] = {0};
        swprintf_s(buf, L"[按键] 左键: %lld 右键：%lld 中键：%lld\n", g_leftClick, g_rightClick, g_middleClick);
        OutputDebugString(buf);
		memset(buf, 0, sizeof(buf));
        swprintf_s(buf, L"[滚轮] 总滚动: %lld\n", g_wheelTotal);
		OutputDebugString(buf);
        memset(buf, 0, sizeof(buf));
        swprintf_s(buf, L"[移动] 累计距离: %lld px\n\n", (long long)g_totalMove);
		OutputDebugString(buf);
        if (_kbhit()) {
            g_running = false;
            char ch = _getch();
            if (ch == 'q' || ch == 'Q') {
                std::cout << "\n检测到退出指令，正在关闭程序...\n";
                PostThreadMessage(g_mainThreadId, WM_QUIT, 0, 0);
                break;
            }
        }
        Sleep(50);
    }
    return 0;
}

void CreateTrayIcon(HWND hwnd) {
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpy(g_nid.szTip, L"鼠标监测工具");
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_TRAYICON) {
        if (lParam == WM_RBUTTONUP) { // 右键点击图标
            POINT curPos;
            GetCursorPos(&curPos);
            SetForegroundWindow(hwnd);

            HMENU hMenu = CreatePopupMenu();
            WCHAR buf[128];

            swprintf_s(buf, L"左键点击: %lld 次", g_leftClick);
            AppendMenu(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, buf);

            swprintf_s(buf, L"右键点击: %lld 次", g_rightClick);
            AppendMenu(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, buf);

            swprintf_s(buf, L"中键点击: %lld 次", g_middleClick);
            AppendMenu(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, buf);

            swprintf_s(buf, L"移动距离: %0.1f px", g_totalMove);
            AppendMenu(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, buf);
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);

            AppendMenu(hMenu, MF_STRING, 1, L"退出程序");

            int id = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, curPos.x, curPos.y, 0, hwnd, NULL);
            if (id == 1) PostQuitMessage(0);
            DestroyMenu(hMenu);
        }
        else if (lParam == WM_LBUTTONUP) {
            if (g_hDlg == nullptr || !IsWindow(g_hDlg)) {
                g_hDlg = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_DIALOG), hwnd, StatsDlgProc);
                ShowWindow(g_hDlg, SW_SHOW);
                SetForegroundWindow(g_hDlg);
            }
            else {
                if (IsWindowVisible(g_hDlg)) {
                    ShowWindow(g_hDlg, SW_HIDE);
                }
                else {
                    ShowWindow(g_hDlg, SW_SHOW);
                    SetForegroundWindow(g_hDlg);
                }
            }
        }
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"MouseTrackerClass";
    WNDCLASS wc = { };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"Hidden Window", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (hwnd == NULL) return 0;

    CreateTrayIcon(hwnd);

    g_mainThreadId = GetCurrentThreadId(); 


    g_mouseHook = SetWindowsHookEx(
        WH_MOUSE_LL,
        LowLevelMouseProc,
        GetModuleHandle(nullptr),
        0
    );

    if (!g_mouseHook) {
		OutputDebugStringA("Fail to set mouse hook.\n");
        return -1;
    }

    _beginthreadex(nullptr, 0, PrintThread, nullptr, 0, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        //if (msg.message == WM_QUIT) break;
        if (g_hDlg == NULL || !IsDialogMessage(g_hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

    }

    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }

    return (int)msg.wParam;
}