#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#define MAX_ALARMS 50
#define MAX_ITEMS 10
#define TXT 128
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY 1001
#define ID_NAME_EDIT 360
#define ID_COPY_ALARM 505

typedef struct {
    char text[TXT];
} Item;#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#define MAX_ALARMS 50
#define MAX_ITEMS 10
#define TXT 128
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY 1001
#define ID_NAME_EDIT 360
#define ID_COPY_ALARM 505

/* ---------- monochrome palette ---------- */
#define COL_BG        RGB(244,244,245)
#define COL_TEXT      RGB(32,32,34)
#define COL_BORDER    RGB(196,196,199)
#define COL_BTN       RGB(228,228,230)
#define COL_BTN_PRESS RGB(208,208,210)
#define COL_ACCENT      RGB(40,40,44)
#define COL_ACCENT_PRS  RGB(20,20,23)
#define COL_ACCENT_TEXT RGB(255,255,255)
#define COL_DISABLED  RGB(214,214,216)
#define COL_DISABLED_TEXT RGB(150,150,152)
#define COL_LIST_BG   RGB(255,255,255)

typedef struct {
    char text[TXT];
} Item;

typedef struct {
    int used;
    char time[6];
    char label[TXT];
    int repeat;
    int doneToday;
    int itemCount;
    Item items[MAX_ITEMS];
} Alarm;

static Alarm g_alarms[MAX_ALARMS];
static int g_alarmCount = 0;
static HWND g_hMain, g_hList, g_hClock, g_hTopmostChk, g_hAddBtnMain;
static HWND g_hPopup = NULL;
static int g_editIndex = -1;
static char g_lastMinute[6] = "";
static NOTIFYICONDATA g_nid;

static HWND g_popupChecks[MAX_ITEMS];
static HWND g_popupDismiss;
static HWND g_popupName;
static int g_popupAlarmIdx = -1;
static int g_popupIsTest = 0;
static int g_flashOn = 0;
static int g_soundPlaying = 0;

static HWND g_dComboHour, g_dComboMinute, g_dLabel, g_dRepeat;
static HWND g_dItems[MAX_ITEMS], g_dItemDel[MAX_ITEMS];
static HWND g_dSave, g_dCancel, g_dAddItem;
static int g_dItemVisible = 0;

static HFONT g_hFont = NULL;
static HFONT g_hFontBig = NULL;
static HBRUSH g_bgBrush = NULL;
static HBRUSH g_listBrush = NULL;

/* ================= helpers ================= */

void InitTheme(void) {
    if (!g_hFont)
        g_hFont = CreateFont(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    if (!g_hFontBig)
        g_hFontBig = CreateFont(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    if (!g_bgBrush) g_bgBrush = CreateSolidBrush(COL_BG);
    if (!g_listBrush) g_listBrush = CreateSolidBrush(COL_LIST_BG);
}

void ApplyFontToChildren(HWND hwnd) {
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        SendMessage(child, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

/* Draws a flat, monochrome button. primary=1 gives the dark "call to action" style */
void DrawFlatButton(LPDRAWITEMSTRUCT dis, int primary) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int pressed = (dis->itemState & ODS_SELECTED) != 0;
    int disabled = (dis->itemState & ODS_DISABLED) != 0;

    COLORREF bg, fg;
    if (disabled) { bg = COL_DISABLED; fg = COL_DISABLED_TEXT; }
    else if (primary) { bg = pressed ? COL_ACCENT_PRS : COL_ACCENT; fg = COL_ACCENT_TEXT; }
    else { bg = pressed ? COL_BTN_PRESS : COL_BTN; fg = COL_TEXT; }

    HBRUSH b = CreateSolidBrush(bg);
    FillRect(hdc, &rc, b);
    DeleteObject(b);

    HPEN pen = CreatePen(PS_SOLID, 1, COL_BORDER);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    char text[160];
    GetWindowText(dis->hwndItem, text, sizeof(text));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_hFont);
    DrawText(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

void RefreshList(void) {
    SendMessage(g_hList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_alarmCount; i++) {
        if (!g_alarms[i].used) continue;
        char line[256];
        snprintf(line, sizeof(line), "  %s    %s    %s%s",
                 g_alarms[i].time,
                 g_alarms[i].label[0] ? g_alarms[i].label : "(tanpa label)",
                 g_alarms[i].repeat ? "harian" : "sekali",
                 g_alarms[i].doneToday ? "   [selesai]" : "");
        int idx = SendMessage(g_hList, LB_ADDSTRING, 0, (LPARAM)line);
        SendMessage(g_hList, LB_SETITEMDATA, idx, (LPARAM)i);
    }
}

int GetSelectedAlarmIndex(void) {
    int sel = SendMessage(g_hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return -1;
    return (int)SendMessage(g_hList, LB_GETITEMDATA, sel, 0);
}

void CopyAlarm(int idx) {
    if (idx < 0 || idx >= g_alarmCount) return;
    if (g_alarmCount >= MAX_ALARMS) {
        MessageBox(g_hMain, "Jumlah alarm sudah maksimum.", "Copy Alarm", MB_OK | MB_ICONERROR);
        return;
    }
    g_alarms[g_alarmCount] = g_alarms[idx];
    g_alarms[g_alarmCount].used = 1;
    g_alarms[g_alarmCount].doneToday = 0;
    g_alarmCount++;
    RefreshList();
}

void PlayAlarmSound(void) {
    if (g_soundPlaying) return;
    g_soundPlaying = 1;

    char exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    char *slash = strrchr(exePath, '\\');
    if (slash) *slash = 0;

    char wavPath[MAX_PATH];
    snprintf(wavPath, sizeof(wavPath), "%s\\mixkit-sound-alert-in-hall-1006.wav", exePath);

    PlaySound(wavPath, NULL, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
}

void StopAlarmSound(void) {
    PlaySound(NULL, NULL, 0);
    g_soundPlaying = 0;
}

void GetLogPath(char *out, size_t outSize) {
    char exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    char *slash = strrchr(exePath, '\\');
    if (slash) *slash = 0;
    snprintf(out, outSize, "%s\\alarm_log.txt", exePath);
}

void LogDismissal(Alarm *a, const char *nama) {
    char path[MAX_PATH];
    GetLogPath(path, sizeof(path));
    FILE *f = fopen(path, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] Alarm '%s' (jadwal %s) dikonfirmasi oleh: %s\n",
        lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec,
        a->label[0] ? a->label : "(tanpa label)", a->time, nama);
    fclose(f);
}

/* ================= Popup window ================= */
void EvaluatePopupState(HWND hwnd) {
    (void)hwnd;
    Alarm *a = &g_alarms[g_popupAlarmIdx];
    int allChecked = 1;
    for (int i = 0; i < a->itemCount; i++)
        if (SendMessage(g_popupChecks[i], BM_GETCHECK, 0, 0) != BST_CHECKED) allChecked = 0;
    int hasName = GetWindowTextLength(g_popupName) > 0;
    int ready = allChecked && hasName;
    EnableWindow(g_popupDismiss, ready);
    if (!allChecked) SetWindowText(g_popupDismiss, "CENTANG SEMUA ITEM DULU");
    else if (!hasName) SetWindowText(g_popupDismiss, "ISI NAMA PERSONIL DULU");
    else SetWindowText(g_popupDismiss, "MATIKAN ALARM");
    InvalidateRect(g_popupDismiss, NULL, TRUE);
}

LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER: {
        if (wp == 2) {
            g_flashOn = !g_flashOn;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH b = CreateSolidBrush(g_flashOn ? RGB(80,0,0) : RGB(40,0,0));
        FillRect(hdc, &rc, b);
        DeleteObject(b);
        return 1;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        DrawFlatButton(dis, dis->hwndItem == g_popupDismiss);
        return TRUE;
    }
    case WM_COMMAND: {
        if (LOWORD(wp) == ID_NAME_EDIT && HIWORD(wp) == EN_CHANGE) {
            EvaluatePopupState(hwnd);
            return 0;
        }
        if (HIWORD(wp) == BN_CLICKED) {
            if ((HWND)lp == g_popupDismiss) {
                Alarm *a = &g_alarms[g_popupAlarmIdx];
                int allChecked = 1;
                for (int i = 0; i < a->itemCount; i++)
                    if (SendMessage(g_popupChecks[i], BM_GETCHECK, 0, 0) != BST_CHECKED) allChecked = 0;
                int hasName = GetWindowTextLength(g_popupName) > 0;
                if (!(allChecked && hasName)) return 0;
                char nameBuf[TXT];
                GetWindowText(g_popupName, nameBuf, sizeof(nameBuf));
                if (!g_popupIsTest) {
                    a->doneToday = 1;
                    LogDismissal(a, nameBuf);
                }
                KillTimer(hwnd, 2);
                StopAlarmSound();
                DestroyWindow(hwnd);
                g_hPopup = NULL;
                RefreshList();
                return 0;
            }
            EvaluatePopupState(hwnd);
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(255,255,255));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(HOLLOW_BRUSH);
    }
    case WM_CLOSE:
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void TriggerAlarm(int idx, int isTest) {
    if (g_hPopup) return;
    g_popupAlarmIdx = idx;
    g_popupIsTest = isTest;
    Alarm *a = &g_alarms[idx];

    WNDCLASS wc = {0};
    wc.lpfnWndProc = PopupProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "SocAlarmPopup";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    int h = 150 + a->itemCount * 32 + 130;
    g_hPopup = CreateWindowEx(WS_EX_TOPMOST, "SocAlarmPopup", "ALARM AKTIF",
        WS_POPUP | WS_BORDER, 480, 150, 460, h, NULL, NULL, wc.hInstance, NULL);

    char title[160];
    snprintf(title, sizeof(title), "ALARM AKTIF - %s", a->time);
    CreateWindow("STATIC", title, WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 15, 440, 20, g_hPopup, NULL, wc.hInstance, NULL);
    CreateWindow("STATIC", a->label[0] ? a->label : "Checklist shift",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 40, 440, 20, g_hPopup, NULL, wc.hInstance, NULL);

    int y = 75;
    for (int i = 0; i < a->itemCount; i++) {
        g_popupChecks[i] = CreateWindow("BUTTON", a->items[i].text,
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            25, y, 410, 26, g_hPopup, (HMENU)(INT_PTR)(300 + i), wc.hInstance, NULL);
        y += 32;
    }

    y += 6;
    CreateWindow("STATIC", "Nama personil yang konfirmasi:", WS_CHILD | WS_VISIBLE,
        25, y, 410, 18, g_hPopup, NULL, wc.hInstance, NULL);
    y += 20;
    g_popupName = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        25, y, 410, 26, g_hPopup, (HMENU)ID_NAME_EDIT, wc.hInstance, NULL);
    y += 40;

    g_popupDismiss = CreateWindow("BUTTON", "CENTANG SEMUA ITEM DULU",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
        25, y, 410, 40, g_hPopup, (HMENU)350, wc.hInstance, NULL);

    ApplyFontToChildren(g_hPopup);
    ShowWindow(g_hPopup, SW_SHOW);
    SetForegroundWindow(g_hPopup);
    SetFocus(g_popupName);
    EvaluatePopupState(g_hPopup);
    PlayAlarmSound();
    SetTimer(g_hPopup, 2, 600, NULL);
}

/* ================= Add/Edit dialog ================= */
void LayoutDialog(HWND hwnd) {
    int y = 150;
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (i < g_dItemVisible) {
            MoveWindow(g_dItems[i], 15, y, 315, 24, TRUE);
            MoveWindow(g_dItemDel[i], 335, y, 65, 24, TRUE);
            ShowWindow(g_dItems[i], SW_SHOW);
            ShowWindow(g_dItemDel[i], SW_SHOW);
            y += 30;
        } else {
            ShowWindow(g_dItems[i], SW_HIDE);
            ShowWindow(g_dItemDel[i], SW_HIDE);
        }
    }
    MoveWindow(g_dAddItem, 15, y, 150, 26, TRUE);
    y += 36;
    MoveWindow(g_dRepeat, 15, y, 220, 22, TRUE);
    y += 38;
    MoveWindow(g_dSave, 220, y, 80, 34, TRUE);
    MoveWindow(g_dCancel, 305, y, 95, 34, TRUE);
    y += 34 + 20;

    RECT rc = {0, 0, 420, y};
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
        SWP_NOMOVE | SWP_NOZORDER);
}

void ShowDialogItem(int i, const char *text) {
    SetWindowText(g_dItems[i], text);
}

LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_BG);
        return (LRESULT)g_bgBrush;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_LIST_BG);
        return (LRESULT)g_listBrush;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        DrawFlatButton(dis, dis->hwndItem == g_dSave);
        return TRUE;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) {
            for (int i = 0; i < g_dItemVisible; i++) {
                if ((HWND)lp == g_dItemDel[i]) {
                    for (int j = i; j < g_dItemVisible - 1; j++) {
                        char buf[TXT];
                        GetWindowText(g_dItems[j + 1], buf, sizeof(buf));
                        SetWindowText(g_dItems[j], buf);
                    }
                    SetWindowText(g_dItems[g_dItemVisible - 1], "");
                    if (g_dItemVisible > 0) g_dItemVisible--;
                    LayoutDialog(hwnd);
                    return 0;
                }
            }
            if ((HWND)lp == g_dAddItem) {
                if (g_dItemVisible < MAX_ITEMS) {
                    SetWindowText(g_dItems[g_dItemVisible], "");
                    g_dItemVisible++;
                    LayoutDialog(hwnd);
                }
            } else if ((HWND)lp == g_dSave) {
                int hh = SendMessage(g_dComboHour, CB_GETCURSEL, 0, 0);
                int mm = SendMessage(g_dComboMinute, CB_GETCURSEL, 0, 0);
                if (hh == CB_ERR || mm == CB_ERR) {
                    MessageBox(hwnd, "Pilih jam dan menit dulu.", "Jam belum dipilih", MB_OK | MB_ICONERROR);
                    return 0;
                }
                char labelBuf[TXT];
                GetWindowText(g_dLabel, labelBuf, sizeof(labelBuf));

                Item tmp[MAX_ITEMS];
                int cnt = 0;
                for (int i = 0; i < g_dItemVisible; i++) {
                    char buf[TXT];
                    GetWindowText(g_dItems[i], buf, sizeof(buf));
                    if (strlen(buf) > 0) { strncpy(tmp[cnt].text, buf, TXT - 1); tmp[cnt].text[TXT-1]=0; cnt++; }
                }
                if (cnt == 0) {
                    MessageBox(hwnd, "Checklist tidak boleh kosong.", "Checklist kosong", MB_OK | MB_ICONERROR);
                    return 0;
                }
                int idx = g_editIndex;
                if (idx < 0) {
                    if (g_alarmCount >= MAX_ALARMS) {
                        MessageBox(hwnd, "Jumlah alarm sudah maksimum.", "Penuh", MB_OK | MB_ICONERROR);
                        return 0;
                    }
                    idx = g_alarmCount++;
                }
                g_alarms[idx].used = 1;
                snprintf(g_alarms[idx].time, 6, "%02d:%02d", hh, mm);
                strncpy(g_alarms[idx].label, labelBuf, TXT - 1);
                g_alarms[idx].label[TXT-1] = 0;
                g_alarms[idx].repeat = (SendMessage(g_dRepeat, BM_GETCHECK, 0, 0) == BST_CHECKED);
                g_alarms[idx].itemCount = cnt;
                for (int i = 0; i < cnt; i++) g_alarms[idx].items[i] = tmp[i];
                g_alarms[idx].doneToday = 0;
                RefreshList();
                DestroyWindow(hwnd);
            } else if ((HWND)lp == g_dCancel) {
                DestroyWindow(hwnd);
            }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        EnableWindow(g_hMain, TRUE);
        SetForegroundWindow(g_hMain);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void OpenAlarmDialog(int editIndex) {
    g_editIndex = editIndex;
    Alarm *src = (editIndex >= 0) ? &g_alarms[editIndex] : NULL;

    WNDCLASS wc = {0};
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "SocAlarmDialog";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_bgBrush;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(WS_EX_DLGMODALFRAME, "SocAlarmDialog",
        editIndex >= 0 ? "Edit alarm" : "Alarm baru",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        450, 120, 420, 300, g_hMain, NULL, wc.hInstance, NULL);

    CreateWindow("STATIC", "Jam:", WS_CHILD | WS_VISIBLE, 15, 15, 100, 18, hwnd, NULL, wc.hInstance, NULL);

    int hh = 0, mm = 0;
    if (src) sscanf(src->time, "%d:%d", &hh, &mm);
    else {
        SYSTEMTIME st; GetLocalTime(&st);
        hh = st.wHour; mm = st.wMinute;
    }

    g_dComboHour = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        15, 35, 75, 200, hwnd, NULL, wc.hInstance, NULL);
    CreateWindow("STATIC", ":", WS_CHILD | WS_VISIBLE | SS_CENTER, 95, 38, 15, 18, hwnd, NULL, wc.hInstance, NULL);
    g_dComboMinute = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        115, 35, 75, 200, hwnd, NULL, wc.hInstance, NULL);

    char buf[4];
    for (int i = 0; i < 24; i++) { snprintf(buf, sizeof(buf), "%02d", i); SendMessage(g_dComboHour, CB_ADDSTRING, 0, (LPARAM)buf); }
    for (int i = 0; i < 60; i++) { snprintf(buf, sizeof(buf), "%02d", i); SendMessage(g_dComboMinute, CB_ADDSTRING, 0, (LPARAM)buf); }
    SendMessage(g_dComboHour, CB_SETCURSEL, hh, 0);
    SendMessage(g_dComboMinute, CB_SETCURSEL, mm, 0);

    CreateWindow("STATIC", "Label:", WS_CHILD | WS_VISIBLE, 15, 72, 150, 18, hwnd, NULL, wc.hInstance, NULL);
    g_dLabel = CreateWindow("EDIT", src ? src->label : "", WS_CHILD | WS_VISIBLE | WS_BORDER,
        15, 92, 385, 24, hwnd, NULL, wc.hInstance, NULL);

    CreateWindow("STATIC", "Checklist:", WS_CHILD | WS_VISIBLE, 15, 126, 150, 18, hwnd, NULL, wc.hInstance, NULL);

    for (int i = 0; i < MAX_ITEMS; i++) {
        g_dItems[i] = CreateWindow("EDIT", "", WS_CHILD | WS_BORDER,
            15, 150, 315, 24, hwnd, NULL, wc.hInstance, NULL);
        g_dItemDel[i] = CreateWindow("BUTTON", "Hapus", WS_CHILD | BS_OWNERDRAW,
            335, 150, 65, 24, hwnd, NULL, wc.hInstance, NULL);
    }

    const char *defaults[4] = {"Cek Wazuh LinkAja", "Cek QRadar PEPC", "Cek MSA", "Cek Whapi"};
    g_dItemVisible = 0;
    if (src) {
        for (int i = 0; i < src->itemCount; i++) { ShowDialogItem(i, src->items[i].text); g_dItemVisible++; }
    } else {
        for (int i = 0; i < 4; i++) { ShowDialogItem(i, defaults[i]); g_dItemVisible++; }
    }

    g_dAddItem = CreateWindow("BUTTON", "+ tambah item", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        15, 0, 150, 26, hwnd, NULL, wc.hInstance, NULL);

    g_dRepeat = CreateWindow("BUTTON", "Ulangi setiap hari", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        15, 0, 220, 22, hwnd, NULL, wc.hInstance, NULL);
    if (!src || src->repeat) SendMessage(g_dRepeat, BM_SETCHECK, BST_CHECKED, 0);

    g_dSave = CreateWindow("BUTTON", "Simpan", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 80, 34, hwnd, NULL, wc.hInstance, NULL);
    g_dCancel = CreateWindow("BUTTON", "Batal", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 95, 34, hwnd, NULL, wc.hInstance, NULL);

    LayoutDialog(hwnd);
    ApplyFontToChildren(hwnd);
    EnableWindow(g_hMain, FALSE);
    ShowWindow(hwnd, SW_SHOW);
}

/* ================= Main window ================= */
void AddTrayIcon(HWND hwnd) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hwnd;
    g_nid.uID = ID_TRAY;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(g_nid.szTip, "SOC Checklist Alarm");
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        InitTheme();

        g_hClock = CreateWindow("STATIC", "--:--:--", WS_CHILD | WS_VISIBLE,
            20, 18, 180, 34, hwnd, NULL, NULL, NULL);
        SendMessage(g_hClock, WM_SETFONT, (WPARAM)g_hFontBig, TRUE);

        g_hTopmostChk = CreateWindow("BUTTON", "Selalu di atas", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            430, 26, 160, 22, hwnd, (HMENU)500, NULL, NULL);
        SendMessage(g_hTopmostChk, BM_SETCHECK, BST_CHECKED, 0);

        g_hList = CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
            20, 65, 570, 220, hwnd, NULL, NULL, NULL);

        int bx = 20, bw = 108, gap = 8;
        CreateWindow("BUTTON", "Tambah alarm", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, bx, 297, bw, 32, hwnd, (HMENU)501, NULL, NULL);
        bx += bw + gap;
        CreateWindow("BUTTON", "Edit alarm", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, bx, 297, bw, 32, hwnd, (HMENU)502, NULL, NULL);
        bx += bw + gap;
        CreateWindow("BUTTON", "Copy alarm", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, bx, 297, bw, 32, hwnd, (HMENU)ID_COPY_ALARM, NULL, NULL);
        bx += bw + gap;
        CreateWindow("BUTTON", "Hapus alarm", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, bx, 297, bw, 32, hwnd, (HMENU)503, NULL, NULL);
        bx += bw + gap;
        CreateWindow("BUTTON", "Test alarm", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, bx, 297, bw, 32, hwnd, (HMENU)504, NULL, NULL);

        CreateWindow("STATIC", "Tutup jendela (X) untuk minimize ke tray. Klik kanan icon tray untuk keluar.",
            WS_CHILD | WS_VISIBLE, 20, 340, 560, 18, hwnd, NULL, NULL, NULL);

        AddTrayIcon(hwnd);
        ApplyFontToChildren(hwnd);
        g_hAddBtnMain = GetDlgItem(hwnd, 501);
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_BG);
        return (LRESULT)g_bgBrush;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_LIST_BG);
        return (LRESULT)g_listBrush;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        DrawFlatButton(dis, dis->hwndItem == g_hAddBtnMain);
        return TRUE;
    }
    case WM_TIMER: {
        if (wp == 1) {
            time_t t = time(NULL);
            struct tm *lt = localtime(&t);
            char buf[16];
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
            SetWindowText(g_hClock, buf);

            char hhmm[6];
            snprintf(hhmm, sizeof(hhmm), "%02d:%02d", lt->tm_hour, lt->tm_min);
            if (strcmp(hhmm, g_lastMinute) != 0) {
                strcpy(g_lastMinute, hhmm);
                if (lt->tm_hour == 0 && lt->tm_min == 0) {
                    for (int i = 0; i < g_alarmCount; i++)
                        if (g_alarms[i].used && g_alarms[i].repeat) g_alarms[i].doneToday = 0;
                    RefreshList();
                }
                if (!g_hPopup) {
                    for (int i = 0; i < g_alarmCount; i++) {
                        if (g_alarms[i].used && !g_alarms[i].doneToday && strcmp(g_alarms[i].time, hhmm) == 0) {
                            TriggerAlarm(i, 0);
                            break;
                        }
                    }
                }
            }
        }
        return 0;
    }
    case WM_TRAYICON:
        if (lp == WM_LBUTTONDBLCLK || lp == WM_LBUTTONUP) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        } else if (lp == WM_RBUTTONUP) {
            HMENU m = CreatePopupMenu();
            AppendMenu(m, MF_STRING, 999, "Keluar");
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(m);
        }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 999) { DestroyWindow(hwnd); return 0; }
        if (HIWORD(wp) == BN_CLICKED) {
            if (id == 500) {
                int on = (SendMessage(g_hTopmostChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            } else if (id == 501) {
                OpenAlarmDialog(-1);
            } else if (id == 502) {
                int idx = GetSelectedAlarmIndex();
                if (idx < 0) MessageBox(hwnd, "Pilih dulu alarm yang mau diedit.", "Pilih alarm", MB_OK);
                else OpenAlarmDialog(idx);
            } else if (id == ID_COPY_ALARM) {
                int idx = GetSelectedAlarmIndex();
                if (idx < 0) MessageBox(hwnd, "Pilih alarm yang akan disalin.", "Copy Alarm", MB_OK);
                else CopyAlarm(idx);
            } else if (id == 503) {
                int idx = GetSelectedAlarmIndex();
                if (idx < 0) MessageBox(hwnd, "Pilih dulu alarm yang mau dihapus.", "Pilih alarm", MB_OK);
                else { g_alarms[idx].used = 0; RefreshList(); }
            } else if (id == 504) {
                int idx = GetSelectedAlarmIndex();
                if (idx < 0) {
                    if (g_alarmCount < MAX_ALARMS) {
                        Alarm *a = &g_alarms[g_alarmCount];
                        a->used = 1; strcpy(a->time, "00:00"); strcpy(a->label, "Test alarm");
                        a->itemCount = 4;
                        strcpy(a->items[0].text, "Cek Wazuh LinkAja");
                        strcpy(a->items[1].text, "Cek QRadar PEPC");
                        strcpy(a->items[2].text, "Cek MSA");
                        strcpy(a->items[3].text, "Cek Whapi");
                        idx = g_alarmCount++;
                        RefreshList();
                    }
                }
                TriggerAlarm(idx, 1);
            }
        }
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hPrev; (void)cmd;
    InitTheme();

    WNDCLASS wc = {0};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "SocAlarmMain";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_bgBrush;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClass(&wc);

    g_hMain = CreateWindowEx(WS_EX_TOPMOST, "SocAlarmMain", "SOC Checklist Alarm",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 630, 430, NULL, NULL, hInst, NULL);
    ShowWindow(g_hMain, show);
    UpdateWindow(g_hMain);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
