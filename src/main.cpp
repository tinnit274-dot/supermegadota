#include <Windows.h>
#include <commctrl.h>
#include <string>
#include "BotApp.hpp"

#pragma comment(lib, "comctl32.lib")

namespace {
constexpr int IDC_START = 101;
constexpr int IDC_STOP = 102;
constexpr int IDC_RECORD = 103;
constexpr int IDC_STOP_REC = 104;
constexpr int IDC_TRAIN = 105;
constexpr int IDC_STATUS = 106;
constexpr int IDC_HINT = 107;

BotApp g_bot;
HFONT g_font = nullptr;
HBRUSH g_background = nullptr;
std::wstring g_statusText = L"Готов к работе";

std::wstring ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), size);
    return result;
}

void PaintText(HDC dc, const wchar_t* text, int x, int y, int w, int h,
               COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    RECT r{x, y, x + w, y + h};
    DrawTextW(dc, text, -1, &r, format);
}

void FillRoundRect(HDC dc, const RECT& r, COLORREF color, int radius = 14) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void PaintPreview(HDC dc, const RECT& target) {
    cv::Mat frame = g_bot.GetLastFrame();
    FillRoundRect(dc, target, RGB(24, 29, 39));
    if (frame.empty()) {
        PaintText(dc, L"Ожидание кадра Dota 2…", target.left, target.top,
                  target.right - target.left, target.bottom - target.top,
                  RGB(150, 160, 175), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    RECT inner{target.left + 2, target.top + 2, target.right - 2, target.bottom - 2};
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = frame.cols;
    bmi.bmiHeader.biHeight = -frame.rows;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, inner.left, inner.top,
        inner.right - inner.left, inner.bottom - inner.top,
        0, 0, frame.cols, frame.rows, frame.data, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

void DrawButton(const DRAWITEMSTRUCT* dis) {
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool focused = (dis->itemState & ODS_FOCUS) != 0;
    COLORREF fill = RGB(36, 44, 58);
    if (dis->CtlID == IDC_START || dis->CtlID == IDC_TRAIN) fill = RGB(213, 91, 56);
    if (pressed) fill = RGB(170, 65, 42);
    if (dis->CtlID == IDC_STOP || dis->CtlID == IDC_STOP_REC) fill = RGB(52, 59, 73);
    FillRoundRect(dis->hDC, dis->rcItem, fill, 10);
    wchar_t caption[128]{};
    GetWindowTextW(dis->hwndItem, caption, 127);
    PaintText(dis->hDC, caption, dis->rcItem.left, dis->rcItem.top,
              dis->rcItem.right - dis->rcItem.left,
              dis->rcItem.bottom - dis->rcItem.top,
              RGB(245, 247, 250), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (focused) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 170, 120));
        auto old = SelectObject(dis->hDC, pen);
        SelectObject(dis->hDC, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dis->hDC, dis->rcItem.left + 2, dis->rcItem.top + 2,
                  dis->rcItem.right - 2, dis->rcItem.bottom - 2);
        SelectObject(dis->hDC, old);
        DeleteObject(pen);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI");
            g_background = CreateSolidBrush(RGB(15, 18, 24));
            auto addButton = [&](int id, const wchar_t* text, int x, int y) {
                HWND button = CreateWindowW(L"BUTTON", text,
                    WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                    x, y, 148, 42, hwnd, reinterpret_cast<HMENU>(id),
                    GetModuleHandleW(nullptr), nullptr);
                SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            };
            addButton(IDC_START, L"Запустить бота", 32, 155);
            addButton(IDC_STOP, L"Остановить", 190, 155);
            addButton(IDC_RECORD, L"Записать демо", 32, 210);
            addButton(IDC_STOP_REC, L"Остановить запись", 190, 210);
            addButton(IDC_TRAIN, L"Обучить модель", 32, 265);
            SetTimer(hwnd, 1, 250, nullptr);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_START: g_bot.StartBot(); break;
                case IDC_STOP: g_bot.StopBot(); break;
                case IDC_RECORD: g_bot.StartRecording(); break;
                case IDC_STOP_REC: g_bot.StopRecording(); break;
                case IDC_TRAIN: g_bot.TrainOnDemos(); break;
                default: break;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_TIMER: {
            g_statusText = ToWide(g_bot.GetStatusString());
            RECT dirty{20, 320, 350, 440};
            InvalidateRect(hwnd, &dirty, FALSE);
            RECT preview{350, 100, 980, 480};
            InvalidateRect(hwnd, &preview, FALSE);
            return 0;
        }
        case WM_DRAWITEM:
            DrawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC paintDc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);

            // Render into an off-screen bitmap, then copy one complete frame
            // to the window. This removes the white/black flash caused by
            // painting the preview and background directly to the HWND.
            HDC dc = CreateCompatibleDC(paintDc);
            HBITMAP backBuffer = CreateCompatibleBitmap(paintDc,
                client.right, client.bottom);
            HGDIOBJ oldBitmap = SelectObject(dc, backBuffer);

            HBRUSH bg = CreateSolidBrush(RGB(12, 15, 21));
            FillRect(dc, &client, bg);
            DeleteObject(bg);

            RECT header{0, 0, client.right, 95};
            HBRUSH headerBrush = CreateSolidBrush(RGB(21, 26, 35));
            FillRect(dc, &header, headerBrush);
            DeleteObject(headerBrush);
            HPEN headerLine = CreatePen(PS_SOLID, 1, RGB(239, 102, 67));
            HGDIOBJ oldPen = SelectObject(dc, headerLine);
            MoveToEx(dc, 30, 91, nullptr);
            LineTo(dc, client.right - 30, 91);
            SelectObject(dc, oldPen);
            DeleteObject(headerLine);

            PaintText(dc, L"DotaBot", 32, 17, 300, 38, RGB(245, 247, 250));
            PaintText(dc, L"NEURAL GAMEPLAY LAB", 34, 56, 300, 20,
                      RGB(239, 112, 72));

            RECT leftCard{20, 112, 340, 470};
            FillRoundRect(dc, leftCard, RGB(22, 27, 36));
            PaintText(dc, L"Управление", 32, 125, 280, 24, RGB(245, 247, 250));
            PaintText(dc, L"КОНТРОЛЬ БОТА", 32, 149, 280, 18,
                      RGB(123, 137, 158));

            RECT statusCard{32, 323, 328, 365};
            FillRoundRect(dc, statusCard, RGB(29, 36, 49), 10);
            HBRUSH dotBrush = CreateSolidBrush(RGB(83, 207, 153));
            HGDIOBJ oldDot = SelectObject(dc, dotBrush);
            Ellipse(dc, 47, 337, 58, 348);
            SelectObject(dc, oldDot);
            DeleteObject(dotBrush);
            PaintText(dc, g_statusText.c_str(), 70, 327, 240, 34,
                      RGB(229, 236, 244));
            PaintText(dc, L"Запись → обучение → запуск", 32, 375, 285, 22,
                      RGB(150, 160, 175));
            PaintText(dc, L"Демо записываются вместе с кадрами, действиями и GSI.",
                      32, 430, 285, 30, RGB(150, 160, 175),
                      DT_LEFT | DT_WORDBREAK);

            RECT preview{360, 112, client.right - 20, 470};
            PaintPreview(dc, preview);
            PaintText(dc, L"ПРЕДПРОСМОТР ЭКРАНА", 382, 125, 300, 20,
                      RGB(245, 247, 250));
            PaintText(dc, L"Модель и правила работают в безопасном режиме ожидания.",
                      382, 438, 500, 20, RGB(150, 160, 175));

            PaintText(dc, L"Демо:", 20, 500, 55, 20, RGB(150, 160, 175));
            const std::wstring demos = ToWide(g_bot.GetDemosDir());
            PaintText(dc, demos.c_str(), 77, 500, 650, 20,
                      RGB(203, 210, 220));
            PaintText(dc, L"Модель:", 20, 526, 65, 20, RGB(150, 160, 175));
            const std::wstring models = ToWide(g_bot.GetModelsDir());
            PaintText(dc, models.c_str(), 88, 526, 650, 20,
                      RGB(203, 210, 220));

            BitBlt(paintDc, 0, 0, client.right, client.bottom, dc, 0, 0,
                   SRCCOPY);
            SelectObject(dc, oldBitmap);
            DeleteObject(backBuffer);
            DeleteDC(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            g_bot.StopBot();
            g_bot.StopRecording();
            if (g_font) DeleteObject(g_font);
            if (g_background) DeleteObject(g_background);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    InitCommonControls();
    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hInstance = instance;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"DotaBot.MainWindow";
    wc.hCursor = LoadCursorW(nullptr,
        reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(32512)));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_COMPOSITED, wc.lpszClassName,
        L"DotaBot • Neural Gameplay Lab",
        (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME) | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 610, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;
    if (!g_bot.Initialize()) {
        MessageBoxW(hwnd, L"Не удалось инициализировать DotaBot. Проверь bot.log.",
                    L"DotaBot", MB_OK | MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
