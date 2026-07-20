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
static HWND g_hMain, g_hList, g_hClock, g_hTopmostChk;
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

static HWND g_dTime, g_dLabel, g_dRepeat, g_dItems[MAX_ITEMS], g_dItemDel[MAX_ITEMS];
static HWND g_dSave, g_dCancel, g_dAddItem;
static int g_dItemVisible = 0;

void RefreshList(void) {
    SendMessage(g_hList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_alarmCount; i++) {
        if (!g_alarms[i].used) continue;
        char line[256];
        snprintf(line, sizeof(line), "%s  |  %s  |  %s%s",
                 g_alarms[i].time,
                 g_alarms[i].label[0] ? g_alarms[i].label : "(tanpa label)",
                 g_alarms[i].repeat ? "harian" : "sekali",
                 g_alarms[i].doneToday ? "  [selesai]" : "");
        int idx = SendMessage(g_hList, LB_ADDSTRING, 0, (LPARAM)line);
        SendMessage(g_hList, LB_SETITEMDATA, idx, (LPARAM)i);
    }
}

int GetSelectedAlarmIndex(void) {
    int sel = SendMessage(g_hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return -1;
    return (int)SendMessage(g_hList, LB_GETITEMDATA, sel, 0);
}

void Beep880(void)
{
    if (g_soundPlaying)
        return;

    g_soundPlaying = 1;

    char exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);

    char *slash = strrchr(exePath, '\');
    if (slash) *slash = 0;

    char wavPath[MAX_PATH];
    snprintf(wavPath, sizeof(wavPath),
             "%s\mixkit-sound-alert-in-hall-1006.wav",
             exePath);

    PlaySound(wavPath, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
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
}

LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER: {
        if (wp == 1) Beep880();
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
                KillTimer(hwnd, 1);
                KillTimer(hwnd, 2);
                PlaySound(NULL, NULL, 0);
                g_soundPlaying = 0;
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
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
        25, y, 410, 40, g_hPopup, (HMENU)350, wc.hInstance, NULL);

    ShowWindow(g_hPopup, SW_SHOW);
    SetForegroundWindow(g_hPopup);
    SetFocus(g_popupName);
    EvaluatePopupState(g_hPopup);
    Beep880();
    SetTimer(g_hPopup, 2, 600, NULL);
}

void LayoutDialog(HWND hwnd) {
    int y = 140;
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (i < g_dItemVisible) {
            MoveWindow(g_dItems[i], 15, y, 315, 24, TRUE);
            MoveWindow(g_dItemDel[i], 335, y, 65, 24, TRUE);
            ShowWindow(g_dItems[i], SW_SHOW);
            ShowWindow(g_dItemDel[i], SW_SHOW);
            y += 28;
        } else {
            ShowWindow(g_dItems[i], SW_HIDE);
            ShowWindow(g_dItemDel[i], SW_HIDE);
        }
    }
    MoveWindow(g_dAddItem, 15, y, 150, 24, TRUE);
    y += 34;
    MoveWindow(g_dRepeat, 15, y, 200, 22, TRUE);
    y += 36;
    MoveWindow(g_dSave, 220, y, 80, 32, TRUE);
    MoveWindow(g_dCancel, 305, y, 95, 32, TRUE);
    y += 32 + 20;

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
                char timeBuf[8], labelBuf[TXT];
                GetWindowText(g_dTime, timeBuf, sizeof(timeBuf));
                GetWindowText(g_dLabel, labelBuf, sizeof(labelBuf));
                int hh, mm;
                if (sscanf(timeBuf, "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
                    MessageBox(hwnd, "Format jam harus HH:MM, contoh 07:30", "Format salah", MB_OK | MB_ICONERROR);
                    return 0;
                }
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
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(WS_EX_DLGMODALFRAME, "SocAlarmDialog",
        editIndex >= 0 ? "Edit alarm" : "Alarm baru",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        450, 120, 420, 300, g_hMain, NULL, wc.hInstance, NULL);

    CreateWindow("STATIC", "Jam (HH:MM):", WS_CHILD | WS_VISIBLE, 15, 15, 150, 18, hwnd, NULL, wc.hInstance, NULL);
    g_dTime = CreateWindow("EDIT", src ? src->time : "", WS_CHILD | WS_VISIBLE | WS_BORDER,
        15, 35, 150, 24, hwnd, NULL, wc.hInstance, NULL);

    CreateWindow("STATIC", "Label:", WS_CHILD | WS_VISIBLE, 15, 68, 150, 18, hwnd, NULL, wc.hInstance, NULL);
    g_dLabel = CreateWindow("EDIT", src ? src->label : "", WS_CHILD | WS_VISIBLE | WS_BORDER,
        15, 88, 370, 24, hwnd, NULL, wc.hInstance, NULL);

    CreateWindow("STATIC", "Checklist:", WS_CHILD | WS_VISIBLE, 15, 120, 150, 18, hwnd, NULL, wc.hInstance, NULL);

    for (int i = 0; i < MAX_ITEMS; i++) {
        g_dItems[i] = CreateWindow("EDIT", "", WS_CHILD | WS_BORDER,
            15, 140, 315, 24, hwnd, NULL, wc.hInstance, NULL);
        g_dItemDel[i] = CreateWindow("BUTTON", "Hapus", WS_CHILD | WS_BORDER,
            335, 140, 65, 24, hwnd, NULL, wc.hInstance, NULL);
    }

    const char *defaults[4] = {"Cek Wazuh LinkAja", "Cek QRadar PEPC", "Cek MSA", "Cek Whapi"};
    g_dItemVisible = 0;
    if (src) {
        for (int i = 0; i < src->itemCount; i++) { ShowDialogItem(i, src->items[i].text); g_dItemVisible++; }
    } else {
        for (int i = 0; i < 4; i++) { ShowDialogItem(i, defaults[i]); g_dItemVisible++; }
    }

    g_dAddItem = CreateWindow("BUTTON", "+ tambah item", WS_CHILD | WS_VISIBLE,
        15, 0, 150, 24, hwnd, NULL, wc.hInstance, NULL);

    g_dRepeat = CreateWindow("BUTTON", "Ulangi setiap hari", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        15, 0, 200, 22, hwnd, NULL, wc.hInstance, NULL);
    if (!src || src->repeat) SendMessage(g_dRepeat, BM_SETCHECK, BST_CHECKED, 0);

    g_dSave = CreateWindow("BUTTON", "Simpan", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        0, 0, 80, 32, hwnd, NULL, wc.hInstance, NULL);
    g_dCancel = CreateWindow("BUTTON", "Batal", WS_CHILD | WS_VISIBLE,
        0, 0, 95, 32, hwnd, NULL, wc.hInstance, NULL);

    LayoutDialog(hwnd);
    EnableWindow(g_hMain, FALSE);
    ShowWindow(hwnd, SW_SHOW);
}

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
        g_hClock = CreateWindow("STATIC", "--:--:--", WS_CHILD | WS_VISIBLE,
            15, 15, 150, 28, hwnd, NULL, NULL, NULL);
        HFONT f = CreateFont(24, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Consolas");
        SendMessage(g_hClock, WM_SETFONT, (WPARAM)f, TRUE);

        g_hTopmostChk = CreateWindow("BUTTON", "Selalu di atas", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            290, 20, 150, 22, hwnd, (HMENU)500, NULL, NULL);
        SendMessage(g_hTopmostChk, BM_SETCHECK, BST_CHECKED, 0);

        g_hList = CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
            15, 55, 430, 220, hwnd, NULL, NULL, NULL);

        CreateWindow("BUTTON", "Tambah alarm", WS_CHILD | WS_VISIBLE, 15, 285, 105, 30, hwnd, (HMENU)501, NULL, NULL);
        CreateWindow("BUTTON", "Edit alarm", WS_CHILD | WS_VISIBLE, 125, 285, 105, 30, hwnd, (HMENU)502, NULL, NULL);
        CreateWindow("BUTTON", "Hapus alarm", WS_CHILD | WS_VISIBLE, 235, 285, 105, 30, hwnd, (HMENU)503, NULL, NULL);
        CreateWindow("BUTTON", "Test alarm", WS_CHILD | WS_VISIBLE, 345, 285, 100, 30, hwnd, (HMENU)504, NULL, NULL);

        CreateWindow("STATIC", "Tutup jendela (X) = minimize ke tray. Klik kanan icon tray untuk keluar.",
            WS_CHILD | WS_VISIBLE, 15, 325, 430, 18, hwnd, NULL, NULL, NULL);

        AddTrayIcon(hwnd);
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
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
    WNDCLASS wc = {0};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "SocAlarmMain";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClass(&wc);

    g_hMain = CreateWindowEx(WS_EX_TOPMOST, "SocAlarmMain", "SOC Checklist Alarm",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 400, NULL, NULL, hInst, NULL);
    ShowWindow(g_hMain, show);
    UpdateWindow(g_hMain);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
