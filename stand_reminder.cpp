// Напоминалка «Встань на 5 минут» — Win32 + GDI+, без зависимостей.
#define _WIN32_WINNT 0x0A00
#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <oleauto.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <ctime>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <set>
#include <tlhelp32.h>
#include <commctrl.h>
#include <propsys.h>
#include <powrprof.h>
#include <urlmon.h>
#include <wininet.h>
#include <dwmapi.h>
#include <cstring>
using namespace Gdiplus;
using std::wstring;

static int WORK_MIN = 60, STAND_MIN = 5, SNOOZE_MIN = 5;
static int IGNORE_MIN = 15;             // проигнорировал (был в игре) — повторить через 15 минут, и так до «Да, встал»
static time_t sittingSince = 0;         // когда последний раз вставал (или запуск)
static DWORD IGNORE_MS = 60000;         // через сколько исчезает неотвеченное напоминание

enum Kind { P_REMIND, P_DONE, P_QUIT, P_POSDEMO, P_STATS, P_TIMER, P_QUITS, P_GAMES, P_CUSTOM, P_UPDATE };
enum { ACT_NONE, ACT_REMIND, ACT_DONE };
enum { T_MAIN = 1, T_ANIM, T_TOP, T_TICK, T_GAMES = 10, T_NIGHT, T_NIGHT_PREVIEW, T_UPDCHECK, T_DELOLD };
enum { HIT_NONE = -1, HIT_B0 = 0, HIT_B1 = 1, HIT_B2 = 2, HIT_CLOSE = 9 };
#define WM_TRAY (WM_APP + 1)
#define WM_GSI (WM_APP + 2)
#define WM_SHOWBLACK (WM_APP + 3)
#define WM_UPDCHECKED (WM_APP + 4)
#define WM_UPDDOWNLOADED (WM_APP + 5)
#define WM_ALREADY (WM_APP + 6)
enum { M_NOW = 200, M_AUTO, M_VOICE, M_EXIT, M_TEST, M_STATS, M_ST_MINUS, M_ST_PLUS, M_ST_ZERO, M_ST_RESET, M_ALREADY, M_DOTA_ON, M_DOTA_OFF, M_AWAY, M_EXPORT, M_IMPORT, M_TIMER_LAST, M_TIMER_STOP, M_POS0 = 300, M_VOICE0 = 400, M_VOL0 = 500, M_STEPS0 = 600, M_TIMER0 = 700,
       M_Q_SHOW = 800, M_Q_CUSTOM, M_Q_PRESET0 = 810, M_Q_RELAPSE0 = 900, M_Q_PLUS0 = 930, M_Q_MINUS0 = 960, M_Q_DEL0 = 990, M_GAMES = 1100, M_GAMES_RESET, M_G_ADD, M_G_REN0 = 1200, M_G_DEL0 = 1250,
       M_N_SCHED = 1300, M_N_ON, M_N_OFF, M_N_AUTO, M_N_PREVIEW, M_N_UNLOCK, M_N_START0 = 1320, M_N_END0 = 1340, M_N_K0 = 1360,
       M_R_1H = 1400, M_R_1D, M_R_1M, M_R_PICK, M_R_DEL0 = 1420,
       M_B_SHOW = 1500, M_B_START, M_B_IDLE0 = 1510, M_LOG = 1600, M_UPD_CHECK, M_UPD_NOW, M_HUB,
       M_REP_TOGGLE = 1700, M_R_REP1, M_R_REP2, M_R_REP7, M_R_SHIFT0 = 1720 };
static const int B_IDLE[] = { 0, 1, 2, 3, 5, 10, 15 };
static const int N_STARTS[] = { 20 * 60, 21 * 60, 21 * 60 + 30, 22 * 60, 22 * 60 + 30, 23 * 60, 23 * 60 + 30, 0 };
static const int N_ENDS[] = { 5 * 60, 6 * 60, 6 * 60 + 30, 7 * 60, 7 * 60 + 30, 8 * 60, 9 * 60 };
static const int N_KS[] = { 4500, 3400, 2700, 1900 };
static const wchar_t *N_KNAMES[] = { L"Слабо", L"Средне", L"Сильно", L"Очень сильно" };
static const wchar_t *Q_PRESETS[] = { L"Сладкое", L"Фастфуд", L"Газировка", L"Энергетики", L"Чипсы и снеки", L"Алкоголь", L"Курение", L"Еда на ночь" };
static const int Q_PRESET_N = 8, Q_MAX = 30;
static const int TIMER_MINS[] = { 1, 2, 3, 4, 5, 10, 15, 20, 30, 45, 60 };
static const int TIMER_N = 11;
static const int STEPS[] = { 3000, 5000, 6500, 8000, 10000, 12000 };
enum { POS_BR, POS_TR, POS_TC, POS_RM, POS_TL, POS_CUSTOM };
static const wchar_t *POS_NAMES[] = { L"Снизу справа", L"Сверху справа", L"Сверху по центру", L"Справа посередине", L"Сверху слева" };
static const wchar_t *RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t *RUN_NAME = L"StandReminder";
static const DWORD DONE_CLOSE_MS = 30000;

struct QuitItem { wstring name; time_t start; time_t best; };
struct Row { wstring name, right, line2; };
struct Btn { RectF r; wstring text; bool primary; };
struct Popup {
    HWND hwnd = NULL; Kind kind = P_REMIND;
    wstring label, title, text, text2; int glyph = 0; RectF text2R;
    wstring customId;
    std::vector<Row> rows; RectF listR; int chartV[7] = {}; bool chartMinutes = false;
    Color c1, c2;
    Btn btn[3]; int nb = 0;
    RectF card, close, iconR, labelR, titleR, textR, chartR; bool hasChart = false;
    int W = 0, H = 0, x = 0, y = 0;
    int hover = HIT_NONE, press = HIT_NONE;
    bool dragging = false, dragMoved = false; POINT dragStart{}; int dragX = 0, dragY = 0; int slideDir = 1;
    DWORD animStart = 0; bool closing = false; float t = 0;
    DWORD shownAt = 0; DWORD autoMs = 0;
};

static HINSTANCE hInst;
static HWND hBlack = NULL;              // окно «Чёрный экран» для OLED
static HWND hMain;
static Popup *cur = nullptr;
static time_t dueAt; static int dueAction = ACT_NONE;
static time_t timerEnd = 0; static int timerMin = 4, timerFiredMin = 4; static bool timerPending = false;   // таймер
static int pendingKind = -1;            // напоминание ждёт, пока закроют полноэкранное приложение
#define APP_VERSION L"3.3"
#define UPDATE_REPO L"yusupedilov95-cyber/napominalka"
#define APP_CREATED L"13 ноября 2026 г."
static float sc = 1.0f;
static NOTIFYICONDATAW nid;
static UINT msgTaskbarCreated;
static wstring semiFont;

static float S(float v) { return v * sc; }

static wstring Min(int n) {
    int a = n % 100, b = n % 10;
    const wchar_t *w = (a >= 11 && a <= 14) ? L"минут" : b == 1 ? L"минуту" : (b >= 2 && b <= 4) ? L"минуты" : L"минут";
    return std::to_wstring(n) + L" " + w;
}

static wstring Clock(time_t t) {
    struct tm *m = localtime(&t); wchar_t b[16];
    swprintf(b, 16, L"%02d:%02d", m->tm_hour, m->tm_min); return b;
}

// ---------- tray ----------
static wstring TimerLeft() {
    long left = (long)(timerEnd - time(NULL)); if (left < 0) left = 0;
    wchar_t b[24]; swprintf(b, 24, L"%ld:%02ld", left / 60, left % 60); return b;
}
static void UpdateTip() {
    extern bool AwayNow();
    wstring t = L"Напоминалка";
    if (AwayNow()) t += L" — пауза, вас нет за компьютером";
    else if (pendingKind >= 0) t += L" — напомню после матча / игры";
    else if (dueAction != ACT_NONE) t += L" — следующее в " + Clock(dueAt);
    if (timerEnd) t = L"Таймер: " + TimerLeft() + L"\n" + t;
    wcsncpy(nid.szTip, t.c_str(), 127); nid.szTip[127] = 0;
    nid.uFlags = NIF_TIP; Shell_NotifyIconW(NIM_MODIFY, &nid);
}
static void StartTimer(int minutes) {
    timerMin = minutes; timerEnd = time(NULL) + (time_t)minutes * 60; timerPending = false; UpdateTip();
}
static void Schedule(int minutes, int action) {
    dueAt = time(NULL) + (time_t)minutes * 60; dueAction = action; UpdateTip();
}

// ---------- drawing helpers ----------
static void RR(GraphicsPath &gp, RectF r, float rad) {
    float d = rad * 2;
    gp.AddArc(r.X, r.Y, d, d, 180, 90);
    gp.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    gp.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    gp.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    gp.CloseFigure();
}
static Color A(const Color &c, BYTE a) { return Color(a, c.GetR(), c.GetG(), c.GetB()); }
static Color Mix(const Color &c, const Color &d, float k) {
    return Color(c.GetA(), (BYTE)(c.GetR() + (d.GetR() - c.GetR()) * k),
                 (BYTE)(c.GetG() + (d.GetG() - c.GetG()) * k), (BYTE)(c.GetB() + (d.GetB() - c.GetB()) * k));
}
static void Text(Graphics &g, const wstring &s, const wchar_t *fam, float px, int style, Color col,
                 RectF r, StringAlignment ha = StringAlignmentNear, StringAlignment va = StringAlignmentNear,
                 bool noWrap = false) {
    FontFamily ff0(fam); const FontFamily *ff = ff0.IsAvailable() ? &ff0 : FontFamily::GenericSansSerif();
    Font f(ff, px, style, UnitPixel); SolidBrush b(col);
    StringFormat sf; sf.SetAlignment(ha); sf.SetLineAlignment(va);
    if (noWrap) { sf.SetFormatFlags(StringFormatFlagsNoWrap); sf.SetTrimming(StringTrimmingEllipsisCharacter); }
    g.DrawString(s.c_str(), -1, &f, r, &sf, &b);
}
static float MeasureH(const wstring &s, const wchar_t *fam, float px, int style, float w) {
    Bitmap bm(1, 1); Graphics g(&bm); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    FontFamily ff0(fam); const FontFamily *ff = ff0.IsAvailable() ? &ff0 : FontFamily::GenericSansSerif();
    Font f(ff, px, style, UnitPixel); RectF out;
    g.MeasureString(s.c_str(), -1, &f, RectF(0, 0, w, 1000), StringFormat::GenericDefault(), &out);
    return out.Height;
}
enum Glyph { G_PERSON, G_CHECK, G_POWER, G_CLOCK };
static void Icon(Graphics &g, RectF r, Color c1, Color c2, int glyph) {
    LinearGradientBrush gb(PointF(r.X, r.Y), PointF(r.X + r.Width, r.Y + r.Height), c1, c2);
    g.FillEllipse(&gb, r);
    float k = r.Width / 44, ox = r.X, oy = r.Y;
    auto P = [&](float x, float y) { return PointF(ox + x * k, oy + y * k); };
    Pen p(Color(255, 255, 255, 255), 3.2f * k);
    p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound); p.SetLineJoin(LineJoinRound);
    SolidBrush wb(Color(255, 255, 255, 255));
    if (glyph == G_PERSON) {
        g.FillEllipse(&wb, ox + (22 - 3.4f) * k, oy + (10.5f - 3.4f) * k, 6.8f * k, 6.8f * k);
        g.DrawLine(&p, P(22, 17), P(22, 26.5f));
        g.DrawLine(&p, P(22, 19), P(15.5f, 12.5f)); g.DrawLine(&p, P(22, 19), P(28.5f, 12.5f));
        g.DrawLine(&p, P(22, 26.5f), P(17.5f, 34)); g.DrawLine(&p, P(22, 26.5f), P(26.5f, 34));
    } else if (glyph == G_CHECK) {
        p.SetWidth(3.6f * k);
        PointF pts[3] = { P(14, 22.5f), P(19.5f, 28), P(30, 16.5f) };
        g.DrawLines(&p, pts, 3);
    } else if (glyph == G_CLOCK) {
        p.SetWidth(3.0f * k);
        g.DrawEllipse(&p, ox + 11.5f * k, oy + 11.5f * k, 21 * k, 21 * k);
        PointF pts[3] = { P(22, 16), P(22, 22.5f), P(26.5f, 25) };
        g.DrawLines(&p, pts, 3);
    } else {
        g.DrawArc(&p, ox + 13 * k, oy + 14 * k, 18 * k, 18 * k, 300, 300);
        g.DrawLine(&p, P(22, 11.5f), P(22, 21));
    }
}

// ---------- полноэкранные игры и голос ----------
// ---------- журнал событий (чтобы можно было проверить, приходило ли напоминание) ----------
static wstring LogPath() {
    wchar_t b[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", b, MAX_PATH);
    wstring dir = (n && n < MAX_PATH) ? wstring(b) + L"\\Napominalka" : L".";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir + L"\\журнал.txt";
}
static void Log(const wstring &msg) {
    static wstring path = LogPath();
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 400000) {   // не разрастаться
        wstring old = path.substr(0, path.size() - 4) + L"_старый.txt";
        DeleteFileW(old.c_str()); MoveFileW(path.c_str(), old.c_str());
    }
    time_t t = time(NULL); struct tm *m = localtime(&t); wchar_t ts[32];
    swprintf(ts, 32, L"%02d.%02d.%04d %02d:%02d:%02d  ", m->tm_mday, m->tm_mon + 1, m->tm_year + 1900, m->tm_hour, m->tm_min, m->tm_sec);
    wstring line = ts + msg + L"\r\n";
    int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), NULL, 0, NULL, NULL);
    std::string u(n, 0); WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), &u[0], n, NULL, NULL);
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz; if (GetFileSizeEx(f, &sz) && sz.QuadPart == 0) { DWORD w; WriteFile(f, "\xEF\xBB\xBF", 3, &w, NULL); }   // чтобы Блокнот понял UTF-8
    DWORD w; WriteFile(f, u.data(), (DWORD)u.size(), &w, NULL); CloseHandle(f);
}
static wstring ForegroundExe() {
    HWND fg = GetForegroundWindow(); DWORD pid = 0; if (fg) GetWindowThreadProcessId(fg, &pid);
    HANDLE pr = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : NULL; if (!pr) return L"?";
    wchar_t p[1024]; DWORD n = 1024; wstring r = L"?";
    if (QueryFullProcessImageNameW(pr, 0, p, &n)) { r = p; size_t k = r.find_last_of(L'\\'); if (k != wstring::npos) r = r.substr(k + 1); }
    CloseHandle(pr); return r;
}
static bool DisplayRequired() {          // видео в браузере/плеере просит Windows не гасить экран
    ULONG st = 0;
    if (CallNtPowerInformation(SystemExecutionState, NULL, 0, &st, sizeof st) != 0) return false;
    return (st & ES_DISPLAY_REQUIRED) != 0;
}
static bool IsFullscreenApp() {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == GetShellWindow()) return false;
    if (hBlack && fg == hBlack) return false;                     // наш чёрный экран — не игра
    wchar_t cls[64]; GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Progman") || !wcscmp(cls, L"Shell_TrayWnd")) return false;
    if (IsIconic(fg) || !IsWindowVisible(fg)) return false;
    // Окно на переднем плане должно закрывать весь свой монитор. Раньше хватало одного признака Windows
    // «занят полноэкранным приложением» — он иногда «залипает» после игры, и напоминания откладывались часами.
    RECT wr; GetWindowRect(fg, &wr);
    MONITORINFO mi = { sizeof mi }; GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi);
    bool covers = wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
                  wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
    if (!covers) return false;
    QUERY_USER_NOTIFICATION_STATE st;
    if (SUCCEEDED(SHQueryUserNotificationState(&st)) &&
        (st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_BUSY || st == QUNS_PRESENTATION_MODE)) return true;
    LONG st2 = GetWindowLongW(fg, GWL_STYLE);
    if ((st2 & WS_CAPTION) == WS_CAPTION) return false;           // обычное окно с заголовком — не полноэкранное
    if (IsZoomed(fg)) return false;                               // просто развёрнутое окно (Chrome, Discord…)
    return true;
}

// ---------- Dota 2: игра сама сообщает, идёт ли матч (Game State Integration) ----------
static const int GSI_PORT = 53921;
static SOCKET gsiSock = INVALID_SOCKET;
static DWORD gsiLast = 0;            // когда пришло последнее сообщение от Доты
static bool gsiInMatch = false;
static bool GsiActive() { return gsiLast && GetTickCount() - gsiLast < 90000; }
static bool ForegroundIsDota() {
    HWND fg = GetForegroundWindow(); if (!fg) return false;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    HANDLE pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!pr) return false;
    wchar_t path[MAX_PATH]; DWORD n = MAX_PATH; bool is = false;
    if (QueryFullProcessImageNameW(pr, 0, path, &n)) {
        _wcslwr(path); is = wcsstr(path, L"dota2.exe") != NULL;
    }
    CloseHandle(pr); return is;
}
// Надо ли пока придержать напоминание
static bool ShouldDefer() {
    if (GsiActive()) {
        if (gsiInMatch) return true;              // идёт матч — молчим
        if (ForegroundIsDota()) return false;     // Дота в меню — можно напоминать
    }
    return IsFullscreenApp();
}
static void GsiStart(HWND h) {
    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) return;
    gsiSock = socket(AF_INET, SOCK_STREAM, 0); if (gsiSock == INVALID_SOCKET) return;
    sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(GSI_PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(gsiSock, (sockaddr *)&a, sizeof a) || listen(gsiSock, 8)) { closesocket(gsiSock); gsiSock = INVALID_SOCKET; return; }
    WSAAsyncSelect(gsiSock, h, WM_GSI, FD_ACCEPT);
}
static void GsiAccept(HWND h) {
    SOCKET c = accept(gsiSock, NULL, NULL); if (c == INVALID_SOCKET) return;
    WSAAsyncSelect(c, h, 0, 0); u_long nb = 0; ioctlsocket(c, FIONBIO, &nb);
    DWORD to = 300; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
    std::string buf; char tmp[4096]; size_t need = (size_t)-1;
    for (;;) {
        int n = recv(c, tmp, sizeof tmp, 0); if (n <= 0) break;
        buf.append(tmp, n); if (buf.size() > 262144) break;
        size_t he = buf.find("\r\n\r\n");
        if (he != std::string::npos) {
            if (need == (size_t)-1) {
                std::string hd = buf.substr(0, he); std::transform(hd.begin(), hd.end(), hd.begin(), ::tolower);
                size_t cl = hd.find("content-length:");
                need = he + 4 + (cl == std::string::npos ? 0 : (size_t)atoi(hd.c_str() + cl + 15));
            }
            if (buf.size() >= need) break;
        }
    }
    const char *resp = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send(c, resp, (int)strlen(resp), 0); closesocket(c);
    size_t he = buf.find("\r\n\r\n"); if (he == std::string::npos || buf.compare(0, 4, "POST")) return;
    std::string body = buf.substr(he + 4), state;
    size_t gs = body.find("\"game_state\"");
    if (gs != std::string::npos) {
        size_t q1 = body.find('"', body.find(':', gs)), q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
        if (q2 != std::string::npos) state = body.substr(q1 + 1, q2 - q1 - 1);
    }
    gsiLast = GetTickCount(); if (!gsiLast) gsiLast = 1;
    gsiInMatch = !state.empty() && state.find("POST_GAME") == std::string::npos &&
                 state.find("DISCONNECT") == std::string::npos && state.find("STATE_INIT") == std::string::npos;
}

static const wchar_t *CFG_KEY = L"Software\\StandReminder";
static bool VoiceOn() {
    DWORD v = 1, sz = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, CFG_KEY, L"VoiceInGames", RRF_RT_REG_DWORD, NULL, &v, &sz);
    return v != 0;
}
static void SetVoice(bool on) {
    DWORD v = on; HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CFG_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"VoiceInGames", 0, REG_DWORD, (BYTE *)&v, sizeof v); RegCloseKey(k);
    }
}

static DWORD GetCfg(const wchar_t *name, DWORD def) {
    DWORD v = def, sz = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, CFG_KEY, name, RRF_RT_REG_DWORD, NULL, &v, &sz) != ERROR_SUCCESS) return def;
    return v;
}
static void SetCfg(const wchar_t *name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CFG_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (BYTE *)&v, sizeof v); RegCloseKey(k);
    }
}

// ---------- статистика (хранится в реестре, не зависит от места exe) ----------
static const wchar_t *STATS_KEY = L"Software\\StandReminder\\Stats";
static wstring DayKey(time_t t) {
    struct tm *m = localtime(&t); wchar_t b[16];
    swprintf(b, 16, L"%04d-%02d-%02d", m->tm_year + 1900, m->tm_mon + 1, m->tm_mday); return b;
}
static DWORD StatGet(const wstring &name) {
    DWORD v = 0, sz = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, STATS_KEY, name.c_str(), RRF_RT_REG_DWORD, NULL, &v, &sz) != ERROR_SUCCESS) return 0;
    return v;
}
static void StatSet(const wstring &name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, STATS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name.c_str(), 0, REG_DWORD, (BYTE *)&v, sizeof v); RegCloseKey(k);
    }
}
static time_t lastCountAt = 0;        // когда последний раз засчитали подход
static bool testMode = false;        // тестовая версия статистику не трогает
static void StatAdjustToday(int delta) {
    wstring d = DayKey(time(NULL));
    int today = (int)StatGet(d), nt = std::max(0, today + delta), total = (int)StatGet(L"Total");
    StatSet(d, (DWORD)nt); StatSet(L"Total", (DWORD)std::max(0, total + (nt - today)));
}
static void StatResetAll() { RegDeleteKeyW(HKEY_CURRENT_USER, STATS_KEY); }
static void StatAddStandUp() {
    lastCountAt = time(NULL);
    if (testMode) return;
    wstring d = DayKey(time(NULL));
    StatSet(d, StatGet(d) + 1);
    StatSet(L"Total", StatGet(L"Total") + 1);
}
static int StatDays(int days) {      // сумма за последние N дней, включая сегодня
    int sum = 0; time_t now = time(NULL);
    for (int i = 0; i < days; i++) sum += StatGet(DayKey(now - (time_t)i * 86400));
    return sum;
}
static int StatBestDay() {
    HKEY k; int best = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STATS_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    for (DWORD i = 0;; i++) {
        wchar_t name[64]; DWORD nl = 64, type, v, vs = sizeof v;
        if (RegEnumValueW(k, i, name, &nl, NULL, &type, (BYTE *)&v, &vs) != ERROR_SUCCESS) break;
        if (type == REG_DWORD && wcscmp(name, L"Total") && (int)v > best) best = (int)v;
    }
    RegCloseKey(k); return best;
}
static int StatStreak() {           // сколько дней подряд вставал хотя бы раз
    int n = 0; time_t now = time(NULL);
    int start = StatGet(DayKey(now)) ? 0 : 1;   // сегодня ещё может быть впереди
    for (int i = start; i < 3650; i++) { if (StatGet(DayKey(now - (time_t)i * 86400))) n++; else break; }
    return n;
}
static wstring Times(int n) {
    int a = n % 100, b = n % 10;
    return std::to_wstring(n) + ((a >= 12 && a <= 14) ? L" раз" : (b >= 2 && b <= 4) ? L" раза" : L" раз");
}
static wstring Dur(int minutes) {      // «2 ч 15 мин»
    if (minutes < 60) return std::to_wstring(minutes) + L" мин";
    return std::to_wstring(minutes / 60) + L" ч " + std::to_wstring(minutes % 60) + L" мин";
}
static wstring Dur0(int minutes) { return minutes < 1 ? L"меньше минуты" : Dur(minutes); }
static wstring Num(int n) {            // 6500 -> «6 500»
    wstring t = std::to_wstring(n); for (int i = (int)t.size() - 3; i > 0; i -= 3) t.insert(i, L" "); return t;
}
static wstring Km(int steps) {          // средний шаг ~0,75 м
    int d = (int)(steps * 0.75 / 100 + 0.5); return std::to_wstring(d / 10) + L"," + std::to_wstring(d % 10);
}
static wstring Days(int n) {
    int a = n % 100, b = n % 10;
    return std::to_wstring(n) + ((a >= 11 && a <= 14) ? L" дней" : b == 1 ? L" день" : (b >= 2 && b <= 4) ? L" дня" : L" дней");
}

// ---------- отказы от вредного ----------
static const wchar_t *QUIT_KEY = L"Software\\StandReminder\\Quits";
static const wchar_t *QBEST_KEY = L"Software\\StandReminder\\QuitsBest";
static ULONGLONG RegQ(const wchar_t *key, const wstring &name) {
    ULONGLONG v = 0; DWORD sz = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, key, name.c_str(), RRF_RT_REG_QWORD, NULL, &v, &sz) != ERROR_SUCCESS) return 0;
    return v;
}
static void SetRegQ(const wchar_t *key, const wstring &name, ULONGLONG v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name.c_str(), 0, REG_QWORD, (BYTE *)&v, sizeof v); RegCloseKey(k);
    }
}
static void DelRegV(const wchar_t *key, const wstring &name) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) { RegDeleteValueW(k, name.c_str()); RegCloseKey(k); }
}
static std::vector<QuitItem> QuitList() {
    std::vector<QuitItem> v; HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, QUIT_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return v;
    for (DWORD i = 0; (int)v.size() < Q_MAX; i++) {
        wchar_t name[128]; DWORD nl = 128, type; ULONGLONG st; DWORD vs = sizeof st;
        if (RegEnumValueW(k, i, name, &nl, NULL, &type, (BYTE *)&st, &vs) != ERROR_SUCCESS) break;
        if (type == REG_QWORD) v.push_back({ name, (time_t)st, (time_t)RegQ(QBEST_KEY, name) });
    }
    RegCloseKey(k);
    std::sort(v.begin(), v.end(), [](const QuitItem &a, const QuitItem &b) { return a.start < b.start; });
    return v;
}
static void QuitAdd(const wstring &name) { if (!RegQ(QUIT_KEY, name)) SetRegQ(QUIT_KEY, name, (ULONGLONG)time(NULL)); }
static void QuitRelapse(const QuitItem &q) {      // сорвался: запомнить рекорд и начать заново
    time_t el = time(NULL) - q.start;
    if (el > q.best) SetRegQ(QBEST_KEY, q.name, (ULONGLONG)el);
    SetRegQ(QUIT_KEY, q.name, (ULONGLONG)time(NULL));
}
static void QuitDelete(const QuitItem &q) { DelRegV(QUIT_KEY, q.name); DelRegV(QBEST_KEY, q.name); }
static void QuitShift(const QuitItem &q, int days) {
    time_t st = q.start - (time_t)days * 86400, now = time(NULL);
    SetRegQ(QUIT_KEY, q.name, (ULONGLONG)(st > now ? now : st));
}
static wstring Years(int n) {
    int a = n % 100, b = n % 10;
    return std::to_wstring(n) + ((a >= 11 && a <= 14) ? L" лет" : b == 1 ? L" год" : (b >= 2 && b <= 4) ? L" года" : L" лет");
}
static wstring Span(time_t secs) {                 // длительность без привязки к календарю (для рекорда)
    if (secs < 0) secs = 0;
    long d = (long)(secs / 86400);
    if (d >= 1) return Days((int)d);
    long hh = (long)(secs / 3600), mm = (long)(secs % 3600 / 60);
    if (secs < 60) return L"только что";
    return hh ? std::to_wstring(hh) + L" ч " + std::to_wstring(mm) + L" мин" : std::to_wstring(mm) + L" мин";
}
static wstring Elapsed(time_t start) {             // «45 дней (1 мес 14 дн)»
    time_t now = time(NULL); if (start > now) start = now;
    long d = (long)((now - start) / 86400);
    wstring out = Span(now - start);
    if (d >= 31) {
        struct tm a = *localtime(&start), b = *localtime(&now);
        int y = b.tm_year - a.tm_year, mo = b.tm_mon - a.tm_mon, dd = b.tm_mday - a.tm_mday;
        if (b.tm_hour * 3600 + b.tm_min * 60 + b.tm_sec < a.tm_hour * 3600 + a.tm_min * 60 + a.tm_sec) dd--;
        if (dd < 0) {
            mo--;
            static const int ML[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
            int pm = (b.tm_mon + 11) % 12, py = b.tm_year + 1900 - (b.tm_mon == 0 ? 1 : 0);
            dd += ML[pm] + (pm == 1 && ((py % 4 == 0 && py % 100 != 0) || py % 400 == 0) ? 1 : 0);
        }
        if (mo < 0) { y--; mo += 12; }
        wstring p;
        if (y > 0) p += Years(y);
        if (mo > 0) p += (p.empty() ? L"" : L" ") + std::to_wstring(mo) + L" мес";
        if (dd > 0) p += (p.empty() ? L"" : L" ") + std::to_wstring(dd) + L" дн";
        if (!p.empty()) out += L"  (" + p + L")";
    }
    return out;
}
static wstring DateStr(time_t t) {
    struct tm *m = localtime(&t); wchar_t b[24];
    swprintf(b, 24, L"%02d.%02d.%04d", m->tm_mday, m->tm_mon + 1, m->tm_year + 1900); return b;
}

// ---------- время в играх ----------
// Игры Steam не считаем (у Steam свой счётчик). Остальные (в т.ч. без магазина) находим так:
//  • программа, открытая на весь экран минуту подряд (кроме браузеров, плееров и т.п.), запоминается как игра;
//  • или игру добавляют вручную, выбрав её .exe.
// Раз в 30 секунд смотрим только список процессов — в игры не лезем, FPS не трогаем.
static const wchar_t *GT_KEY = L"Software\\StandReminder\\GameTotal";    // имя игры -> секунд всего
static const wchar_t *GD_KEY = L"Software\\StandReminder\\GameDays";     // «дата|игра» -> секунд за день
static const wchar_t *GK_KEY = L"Software\\StandReminder\\GameExe";      // путь к exe (строчными) -> имя игры
static const wchar_t *GI_KEY = L"Software\\StandReminder\\GameIgnore";   // путь к exe, который не считать
struct PidInfo { wstring exe, path; };
static std::map<DWORD, PidInfo> pidCache;
static std::map<wstring, wstring> knownGames; static std::set<wstring> ignoredExe; static bool gamesLoaded = false;
static time_t gameLastPoll = 0;
static std::vector<wstring> gamesNow;
static wstring fsCandidate;                 // кто был на весь экран в прошлую проверку
static wstring Lower(wstring s) { if (!s.empty()) _wcslwr(&s[0]); return s; }
static void LoadGameLists() {
    knownGames.clear(); ignoredExe.clear(); HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, GK_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        for (DWORD i = 0;; i++) {
            wchar_t name[1024], data[256]; DWORD nl = 1024, dl = sizeof data, type;
            if (RegEnumValueW(k, i, name, &nl, NULL, &type, (BYTE *)data, &dl) != ERROR_SUCCESS) break;
            if (type == REG_SZ) { data[255] = 0; knownGames[name] = data; }
        }
        RegCloseKey(k);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, GI_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        for (DWORD i = 0;; i++) {
            wchar_t name[1024]; DWORD nl = 1024;
            if (RegEnumValueW(k, i, name, &nl, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            ignoredExe.insert(name);
        }
        RegCloseKey(k);
    }
    gamesLoaded = true;
}
static void SetRegSz(const wchar_t *key, const wstring &name, const wstring &v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name.c_str(), 0, REG_SZ, (const BYTE *)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))); RegCloseKey(k);
    }
}
static bool IsGenericName(const wstring &n) {
    wstring l = Lower(n);
    static const wchar_t *G[] = { L"", L"unreal engine", L"unrealgame", L"ue4game", L"unity", L"game", L"launcher", L"bootstrappackagedgame",
                                  L"shipping", L"application", L"setup", L"win64", L"x64", L"client", L"godot engine" };
    for (auto g : G) if (l == g) return true;
    return l.size() < 2;
}
static wstring VerString(const wstring &path, const wchar_t *field) {
    DWORD h = 0, sz = GetFileVersionInfoSizeW(path.c_str(), &h); if (!sz) return L"";
    std::vector<BYTE> buf(sz); if (!GetFileVersionInfoW(path.c_str(), 0, sz, buf.data())) return L"";
    struct LC { WORD lang, cp; } *tr; UINT tl = 0; wchar_t sub[96];
    if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (void **)&tr, &tl) && tl >= sizeof(LC))
        swprintf(sub, 96, L"\\StringFileInfo\\%04x%04x\\%ls", tr[0].lang, tr[0].cp, field);
    else swprintf(sub, 96, L"\\StringFileInfo\\040904b0\\%ls", field);
    wchar_t *v = nullptr; UINT vl = 0;
    if (!VerQueryValueW(buf.data(), sub, (void **)&v, &vl) || !v || !vl) return L"";
    wstring r(v); while (!r.empty() && (r.back() == L' ' || r.back() == 0)) r.pop_back();
    for (auto &ch : r) if (ch == L'|' || ch == L'=' || ch == L';' || ch == L'\\') ch = L' ';
    return r.size() > 60 ? r.substr(0, 60) : r;
}
static wstring NameForExe(const wstring &path) {
    wstring n = VerString(path, L"ProductName");
    if (IsGenericName(n)) n = VerString(path, L"FileDescription");
    if (!IsGenericName(n)) return n;
    // имя папки игры: поднимаемся мимо bin, x64, Binaries и т.п.
    static const wchar_t *SKIP[] = { L"bin", L"bin64", L"bin32", L"x64", L"x86", L"win64", L"win32", L"binaries", L"game", L"release",
                                     L"retail", L"shipping", L"exe", L"app", L"client" };
    wstring p = path; size_t e = p.find_last_of(L'\\');
    while (e != wstring::npos && e > 2) {
        size_t b2 = p.find_last_of(L'\\', e - 1); if (b2 == wstring::npos) break;
        wstring dir = p.substr(b2 + 1, e - b2 - 1), dl = Lower(dir); bool skip = false;
        for (auto sk : SKIP) if (dl == sk) skip = true;
        if (!skip && dir.size() > 1 && dir.find(L':') == wstring::npos) return dir;
        e = b2;
    }
    wstring f = p.substr(p.find_last_of(L'\\') + 1); size_t dot = f.find_last_of(L'.');
    return dot == wstring::npos ? f : f.substr(0, dot);
}
static bool NeverAGame(const wstring &lpath) {     // браузеры, плееры, системное, Steam
    if (lpath.find(L"\\steamapps\\") != wstring::npos) return true;
    if (lpath.find(L"\\windows\\") != wstring::npos && lpath.find(L":\\windows\\") != wstring::npos) return true;
    size_t sl = lpath.find_last_of(L'\\'); wstring exe = sl == wstring::npos ? lpath : lpath.substr(sl + 1);
    static const wchar_t *NO[] = { L"chrome.exe", L"msedge.exe", L"firefox.exe", L"opera.exe", L"opera_gx.exe", L"browser.exe", L"brave.exe",
        L"vivaldi.exe", L"iexplore.exe", L"yandex.exe", L"vlc.exe", L"mpc-hc.exe", L"mpc-hc64.exe", L"mpc-be.exe", L"mpc-be64.exe",
        L"potplayer.exe", L"potplayer64.exe", L"potplayermini.exe", L"potplayermini64.exe", L"kmplayer.exe", L"kmplayer64x.exe",
        L"wmplayer.exe", L"video.ui.exe", L"microsoft.media.player.exe", L"discord.exe", L"obs64.exe", L"obs32.exe", L"steam.exe",
        L"steamwebhelper.exe", L"telegram.exe", L"zoom.exe", L"teams.exe", L"ms-teams.exe", L"powerpnt.exe", L"winword.exe",
        L"excel.exe", L"code.exe", L"epicgameslauncher.exe", L"battle.net.exe", L"riotclientux.exe", L"riotclientservices.exe",
        L"explorer.exe", L"applicationframehost.exe", L"spotify.exe", L"yandexmusic.exe", L"photos.exe", L"mspaint.exe" };
    for (auto n : NO) if (exe == n) return true;
    wchar_t self[1024]; GetModuleFileNameW(NULL, self, 1024);
    return Lower(self) == lpath;
}
static void RemoveGameTime(const wstring &name) {      // удалить всё время игры
    DelRegV(GT_KEY, name); HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, GD_KEY, 0, KEY_READ | KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    std::vector<wstring> del;
    for (DWORD i = 0;; i++) {
        wchar_t n[300]; DWORD nl = 300;
        if (RegEnumValueW(k, i, n, &nl, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        if (nl > 11 && n[10] == L'|' && name == n + 11) del.push_back(n);
    }
    for (auto &d : del) RegDeleteValueW(k, d.c_str());
    RegCloseKey(k);
}
static void RenameGame(const wstring &from, const wstring &to) {
    if (from == to) return;
    for (auto &kv : knownGames) if (kv.second == from) SetRegSz(GK_KEY, kv.first, to);
    SetRegQ(GT_KEY, to, RegQ(GT_KEY, to) + RegQ(GT_KEY, from)); DelRegV(GT_KEY, from);
    HKEY k; std::vector<std::pair<wstring, ULONGLONG>> mv;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, GD_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        for (DWORD i = 0;; i++) {
            wchar_t n[300]; DWORD nl = 300, type; ULONGLONG v; DWORD vs = sizeof v;
            if (RegEnumValueW(k, i, n, &nl, NULL, &type, (BYTE *)&v, &vs) != ERROR_SUCCESS) break;
            if (type == REG_QWORD && nl > 11 && n[10] == L'|' && from == n + 11) mv.push_back({ wstring(n, 10), v });
        }
        RegCloseKey(k);
    }
    for (auto &m : mv) { DelRegV(GD_KEY, m.first + L"|" + from); wstring t = m.first + L"|" + to; SetRegQ(GD_KEY, t, RegQ(GD_KEY, t) + m.second); }
    LoadGameLists();
}
static void AddGameExe(const wstring &path) {
    wstring lp = Lower(path);
    DelRegV(GI_KEY, lp);
    if (!knownGames.count(lp)) SetRegSz(GK_KEY, lp, NameForExe(path));
    LoadGameLists();
}
static void IgnoreGame(const wstring &name) {           // «это не игра»: не считать и удалить время
    for (auto &kv : knownGames) if (kv.second == name) { SetRegSz(GI_KEY, kv.first, L"1"); DelRegV(GK_KEY, kv.first); }
    RemoveGameTime(name); LoadGameLists();
}
static void GamePoll() {
    time_t now = time(NULL);
    if (!gamesLoaded) LoadGameLists();
    // кто сейчас на весь экран
    wstring fsPath;
    if (IsFullscreenApp()) {
        HWND fg = GetForegroundWindow(); DWORD fpid = 0;
        if (fg) GetWindowThreadProcessId(fg, &fpid);
        HANDLE pr = fpid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, fpid) : NULL;
        if (pr) { wchar_t p[1024]; DWORD n = 1024; if (QueryFullProcessImageNameW(pr, 0, p, &n)) fsPath = p; CloseHandle(pr); }
    }
    if (!fsPath.empty()) {
        wstring lp = Lower(fsPath);
        if (!knownGames.count(lp) && !ignoredExe.count(lp) && !NeverAGame(lp)) {
            if (fsCandidate == lp) { SetRegSz(GK_KEY, lp, NameForExe(fsPath)); LoadGameLists(); fsCandidate.clear(); }
            else fsCandidate = lp;            // на весь экран 2 проверки подряд (~минута) — тогда это игра
        } else fsCandidate.clear();
    } else fsCandidate.clear();

    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (sn == INVALID_HANDLE_VALUE) return;
    std::map<DWORD, PidInfo> next; std::set<wstring> running;
    PROCESSENTRY32W pe = { sizeof pe };
    if (Process32FirstW(sn, &pe)) do {
        DWORD pid = pe.th32ProcessID; if (pid <= 4) continue;
        PidInfo inf; auto it = pidCache.find(pid);
        if (it != pidCache.end() && it->second.exe == pe.szExeFile) inf = it->second;
        else {
            inf.exe = pe.szExeFile;
            HANDLE pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (pr) { wchar_t p[1024]; DWORD n = 1024; if (QueryFullProcessImageNameW(pr, 0, p, &n)) inf.path = Lower(p); CloseHandle(pr); }
        }
        auto kg = inf.path.empty() ? knownGames.end() : knownGames.find(inf.path);
        if (kg != knownGames.end()) running.insert(kg->second);
        next[pid] = inf;
    } while (Process32NextW(sn, &pe));
    CloseHandle(sn);
    pidCache.swap(next);
    long dt = gameLastPoll ? (long)(now - gameLastPoll) : 0; gameLastPoll = now;
    if (dt < 0 || dt > 90) dt = 0;              // компьютер спал — эти минуты не считаем
    gamesNow.assign(running.begin(), running.end());
    if (dt) for (auto &g : running) {
        SetRegQ(GT_KEY, g, RegQ(GT_KEY, g) + dt);
        wstring dk = DayKey(now) + L"|" + g; SetRegQ(GD_KEY, dk, RegQ(GD_KEY, dk) + dt);
    }
}
struct GameAgg { std::map<wstring, long long> total, today, week, month; long long day[30] = {}; long long all = 0; };
static GameAgg GameStats() {
    GameAgg a; time_t now = time(NULL); std::map<wstring, int> idx;
    for (int i = 0; i < 30; i++) idx[DayKey(now - (time_t)i * 86400)] = i;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, GT_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        for (DWORD i = 0;; i++) {
            wchar_t name[256]; DWORD nl = 256, type; ULONGLONG v; DWORD vs = sizeof v;
            if (RegEnumValueW(k, i, name, &nl, NULL, &type, (BYTE *)&v, &vs) != ERROR_SUCCESS) break;
            if (type == REG_QWORD) { a.total[name] = (long long)v; a.all += (long long)v; }
        }
        RegCloseKey(k);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, GD_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        for (DWORD i = 0;; i++) {
            wchar_t name[300]; DWORD nl = 300, type; ULONGLONG v; DWORD vs = sizeof v;
            if (RegEnumValueW(k, i, name, &nl, NULL, &type, (BYTE *)&v, &vs) != ERROR_SUCCESS) break;
            if (type != REG_QWORD || nl < 12 || name[10] != L'|') continue;
            auto it = idx.find(wstring(name, 10)); if (it == idx.end()) continue;
            wstring g = name + 11; int d = it->second;
            a.day[d] += (long long)v; a.month[g] += (long long)v;
            if (d < 7) a.week[g] += (long long)v;
            if (d == 0) a.today[g] += (long long)v;
        }
        RegCloseKey(k);
    }
    return a;
}
static wstring HM(long long secs) {
    long long h = secs / 3600, m = secs % 3600 / 60;
    if (h >= 100) return std::to_wstring(h) + L" ч";
    if (h) return std::to_wstring(h) + L" ч " + std::to_wstring(m) + L" мин";
    return std::to_wstring(m) + L" мин";
}

// ---------- голос (записан заранее и встроен в программу) ----------
static const wchar_t *VOICE_NAMES[] = { L"Арина", L"Виктория", L"Татьяна", L"Анна", L"Елена", L"Александр", L"Михаил" };
static const int VOICE_COUNT = 7;
static const int VOLUMES[] = { 25, 50, 75, 100 };
static std::vector<BYTE> playBuf;
static void PlayClip(int voiceIdx, int phrase) {
    if (voiceIdx < 0 || voiceIdx >= VOICE_COUNT) voiceIdx = 0;
    HRSRC r = FindResourceW(hInst, MAKEINTRESOURCEW(phrase < 2 ? 100 + voiceIdx * 2 + phrase : 200 + voiceIdx), (LPCWSTR)RT_RCDATA);
    if (!r) return;
    HGLOBAL g = LoadResource(hInst, r); DWORD n = SizeofResource(hInst, r);
    const BYTE *src = (const BYTE *)LockResource(g); if (!src || n < 44) return;
    PlaySoundW(NULL, NULL, 0);                     // остановить предыдущее
    playBuf.assign(src, src + n);
    float vol = std::max(0, std::min(100, (int)GetCfg(L"Volume", 50))) / 100.0f;
    // найти блок data и уменьшить громкость
    size_t pos = 12;
    while (pos + 8 <= playBuf.size()) {
        DWORD sz = *(DWORD *)&playBuf[pos + 4];
        if (!memcmp(&playBuf[pos], "data", 4)) {
            short *smp = (short *)&playBuf[pos + 8];
            size_t cnt = std::min((size_t)sz, playBuf.size() - pos - 8) / 2;
            for (size_t i = 0; i < cnt; i++) smp[i] = (short)(smp[i] * vol);
            break;
        }
        pos += 8 + sz + (sz & 1);
    }
    PlaySoundW((LPCWSTR)playBuf.data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
static void Speak(int phrase) { PlayClip((int)GetCfg(L"VoiceIdx", 0), phrase); }
static void Chime() {
    if (!PlaySoundW(L"Notification.Reminder", NULL, SND_ALIAS | SND_ASYNC | SND_NODEFAULT))
        MessageBeep(MB_ICONASTERISK);
}

// ---------- popup ----------
static void Layout(Popup *p) {
    float M = S(26), pad = S(18), ic = S(44), gap = S(14);
    float cw = p->kind == P_GAMES ? S(470) : (p->kind == P_STATS || p->kind == P_QUITS) ? S(430) : S(380);
    float colX = M + pad + ic + gap, colW = cw - pad - ic - gap - pad;
    float y = M + pad - S(2);
    p->iconR = RectF(M + pad, M + pad, ic, ic);
    p->labelR = RectF(colX, y, colW - S(28), S(16)); y += S(17);
    float th = MeasureH(p->title, semiFont.c_str(), S(16), FontStyleRegular, colW - S(24));
    p->titleR = RectF(colX, y, colW - S(24), th); y += th + S(2);
    float xh = MeasureH(p->text, L"Segoe UI", S(13), FontStyleRegular, colW);
    p->textR = RectF(colX, y, colW, xh + S(2)); y += xh;
    p->hasChart = p->kind == P_STATS || p->kind == P_GAMES;
    if (p->hasChart) { p->chartR = RectF(colX, y + S(10), colW, S(92)); y = p->chartR.Y + p->chartR.Height; }
    if (!p->text2.empty()) {
        float h2 = MeasureH(p->text2, L"Segoe UI", S(13), FontStyleRegular, colW);
        p->text2R = RectF(colX, y + S(10), colW, h2 + S(2)); y = p->text2R.Y + h2;
    }
    if (!p->rows.empty()) { p->listR = RectF(colX, y + S(10), colW, p->rows.size() * S(46)); y = p->listR.Y + p->listR.Height - S(6); }
    float by = std::max(y + S(14), M + pad + ic + S(14)), bh = S(36);
    if (p->nb >= 2) {
        float bw = (colW - S(8)) / 2;
        p->btn[0].r = RectF(colX, by, bw, bh);
        p->btn[1].r = RectF(colX + bw + S(8), by, bw, bh);
        if (p->nb == 3) {            // третья кнопка — во всю ширину, вторым рядом
            float bh3 = S(30); by += bh + S(8);
            p->btn[2].r = RectF(colX, by, colW, bh3); bh = bh3;
        }
    } else {
        float bw = (colW - S(8)) / 2;
        p->btn[0].r = RectF(colX + colW - bw, by, bw, bh);
    }
    float ch = by + bh + pad - M;
    p->card = RectF(M, M, cw, ch);
    p->close = RectF(M + cw - S(12) - S(26), M + S(10), S(26), S(26));
    p->W = (int)(cw + 2 * M + 0.5f); p->H = (int)(ch + 2 * M + 0.5f);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int mode = (int)GetCfg(L"Position", POS_BR), eg = (int)S(14);
    int left = wa.left - (int)M + eg, right = wa.right - (int)(cw + M) - eg;
    int top = wa.top - (int)M + eg, bottom = wa.bottom - (int)(ch + M) - eg;
    p->slideDir = 1;
    switch (mode) {
    case POS_TR: p->x = right; p->y = top; p->slideDir = -1; break;
    case POS_TC: p->x = (wa.left + wa.right) / 2 - p->W / 2; p->y = top; p->slideDir = -1; break;
    case POS_RM: p->x = right; p->y = (wa.top + wa.bottom) / 2 - p->H / 2; break;
    case POS_TL: p->x = left; p->y = top; p->slideDir = -1; break;
    case POS_CUSTOM: {
        int cx = (int)(LONG)GetCfg(L"PosX", 0), cy = (int)(LONG)GetCfg(L"PosY", 0);
        // держим окно в пределах монитора, на котором оно было
        POINT c = { cx + p->W / 2, cy + p->H / 2 };
        MONITORINFO mi = { sizeof mi }; GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST), &mi);
        RECT w = mi.rcWork;
        p->x = std::max((int)(w.left - M), std::min(cx, (int)(w.right - cw - M)));
        p->y = std::max((int)(w.top - M), std::min(cy, (int)(w.bottom - ch - M)));
        break; }
    default: p->x = right; p->y = bottom; break;
    }
}

static void Draw(Graphics &g, Popup *p) {
    RectF c = p->card;
    // мягкая тень
    float M = S(26);
    for (int i = (int)M; i >= 1; i -= 1) {
        float k = 1.0f - (float)i / M;
        GraphicsPath sp; RR(sp, RectF(c.X - i * 0.6f, c.Y - i * 0.6f + S(5), c.Width + i * 1.2f, c.Height + i * 1.2f), S(16) + i * 0.6f);
        SolidBrush sb(Color((BYTE)(7 * k * k), 0, 0, 0)); g.FillPath(&sb, &sp);
    }
    // карточка
    GraphicsPath cp; RR(cp, c, S(16));
    LinearGradientBrush bg(PointF(0, c.Y), PointF(0, c.Y + c.Height), Color(250, 0x26, 0x2B, 0x3B), Color(250, 0x1A, 0x1D, 0x28));
    g.FillPath(&bg, &cp);
    // лёгкое цветное свечение слева сверху
    {
        GraphicsPath glow; glow.AddEllipse(c.X - S(60), c.Y - S(70), S(220), S(180));
        PathGradientBrush pg(&glow); pg.SetCenterColor(A(p->c1, 38));
        Color edge(0, 0, 0, 0); int n = 1; pg.SetSurroundColors(&edge, &n);
        Region old; g.GetClip(&old); g.SetClip(&cp); g.FillPath(&pg, &glow); g.SetClip(&old);
    }
    Pen border(Color(28, 255, 255, 255), 1.0f); g.DrawPath(&border, &cp);

    // иконка
    Icon(g, p->iconR, p->c1, p->c2, p->glyph);

    // тексты
    Text(g, p->label, semiFont.c_str(), S(11), FontStyleRegular, Mix(p->c1, Color(255, 255, 255, 255), 0.25f), p->labelR,
         StringAlignmentNear, StringAlignmentNear, true);
    Text(g, p->title, semiFont.c_str(), S(16), FontStyleRegular, Color(255, 0xF5, 0xF7, 0xFB), p->titleR);
    Text(g, p->text, L"Segoe UI", S(13), FontStyleRegular, Color(255, 0x9C, 0xA5, 0xB9), p->textR);

    // кнопка закрытия
    bool ch = p->hover == HIT_CLOSE;
    if (ch) { SolidBrush hb(Color(26, 255, 255, 255)); g.FillEllipse(&hb, p->close); }
    {
        Pen xp(ch ? Color(255, 255, 255, 255) : Color(255, 0x7E, 0x87, 0x9B), S(1.6f));
        xp.SetStartCap(LineCapRound); xp.SetEndCap(LineCapRound);
        float cx = p->close.X + p->close.Width / 2, cy = p->close.Y + p->close.Height / 2, d = S(4.5f);
        g.DrawLine(&xp, cx - d, cy - d, cx + d, cy + d); g.DrawLine(&xp, cx - d, cy + d, cx + d, cy - d);
    }

    // кнопки
    for (int i = 0; i < p->nb; i++) {
        Btn &b = p->btn[i]; bool hv = p->hover == i, pr = p->press == i && hv;
        GraphicsPath bp; RR(bp, b.r, S(10));
        if (b.primary) {
            Color a = p->c1, z = p->c2;
            if (hv) { a = Mix(a, Color(255, 255, 255, 255), 0.12f); z = Mix(z, Color(255, 255, 255, 255), 0.12f); }
            if (pr) { a = Mix(a, Color(255, 0, 0, 0), 0.15f); z = Mix(z, Color(255, 0, 0, 0), 0.15f); }
            LinearGradientBrush gb(PointF(b.r.X, b.r.Y), PointF(b.r.X + b.r.Width, b.r.Y + b.r.Height), a, z);
            g.FillPath(&gb, &bp);
            Pen hl(Color(40, 255, 255, 255), 1.0f); g.DrawPath(&hl, &bp);
        } else {
            SolidBrush fb(Color(pr ? 14 : hv ? 34 : 20, 255, 255, 255)); g.FillPath(&fb, &bp);
            Pen bd(Color(hv ? 40 : 26, 255, 255, 255), 1.0f); g.DrawPath(&bd, &bp);
        }
        RectF tr = b.r; if (pr) tr.Y += 1;
        Text(g, b.text, semiFont.c_str(), S(13), FontStyleRegular, Color(255, 255, 255, 255), tr,
             StringAlignmentCenter, StringAlignmentCenter, true);
    }

    if (!p->text2.empty()) Text(g, p->text2, L"Segoe UI", S(13), FontStyleRegular, Color(255, 0x9C, 0xA5, 0xB9), p->text2R);

    // список отказов
    for (size_t i = 0; i < p->rows.size(); i++) {
        const Row &q = p->rows[i]; float ry = p->listR.Y + i * S(46), rw = p->listR.Width;
        if (i) { SolidBrush ln(Color(18, 255, 255, 255)); g.FillRectangle(&ln, RectF(p->listR.X, ry - S(4), rw, 1)); }
        Text(g, q.name, semiFont.c_str(), S(14), FontStyleRegular, Color(255, 0xF5, 0xF7, 0xFB),
             RectF(p->listR.X, ry, rw - S(110), S(20)), StringAlignmentNear, StringAlignmentNear, true);
        Text(g, q.right, L"Segoe UI", S(11), FontStyleRegular, Color(255, 0x7E, 0x87, 0x9B),
             RectF(p->listR.X + rw - S(110), ry + S(2), S(110), S(18)), StringAlignmentFar, StringAlignmentNear, true);
        Text(g, q.line2, semiFont.c_str(), S(13), FontStyleRegular, Mix(p->c1, Color(255, 255, 255, 255), 0.2f),
             RectF(p->listR.X, ry + S(19), rw, S(20)), StringAlignmentNear, StringAlignmentNear, true);
    }

    // график за 7 дней
    if (p->hasChart) {
        static const wchar_t *WD[] = { L"Вс", L"Пн", L"Вт", L"Ср", L"Чт", L"Пт", L"Сб" };
        RectF r = p->chartR; time_t now = time(NULL); int v[7], mx = 1;
        for (int i = 0; i < 7; i++) { v[i] = p->chartV[i]; mx = std::max(mx, v[i]); }
        float slot = r.Width / 7, top = S(16), bot = S(18), maxH = r.Height - top - bot, bw = slot * 0.56f;
        SolidBrush line(Color(22, 255, 255, 255));
        g.FillRectangle(&line, RectF(r.X, r.Y + top + maxH, r.Width, 1));
        for (int i = 0; i < 7; i++) {
            time_t d = now - (time_t)(6 - i) * 86400; struct tm *tmv = localtime(&d); int wd = tmv->tm_wday;
            bool today = i == 6;
            float bh = v[i] ? std::max(S(5), maxH * v[i] / mx) : S(3);
            RectF br(r.X + slot * i + (slot - bw) / 2, r.Y + top + maxH - bh, bw, bh);
            GraphicsPath bp; RR(bp, br, std::min(S(4), std::min(bh, bw) / 2 - 0.1f));
            if (!v[i]) { SolidBrush zb(Color(34, 255, 255, 255)); g.FillPath(&zb, &bp); }
            else if (today) { LinearGradientBrush tb(PointF(0, br.Y - 1), PointF(0, br.Y + br.Height + 1), p->c1, p->c2); g.FillPath(&tb, &bp); }
            else { SolidBrush ob(A(p->c1, 105)); g.FillPath(&ob, &bp); }
            if (v[i]) Text(g, p->chartMinutes ? (v[i] >= 60 ? std::to_wstring(v[i] / 60) + (v[i] % 60 >= 30 ? L",5" : L"") + L" ч" : std::to_wstring(v[i]) + L" м") : std::to_wstring(v[i]), semiFont.c_str(), S(11), FontStyleRegular,
                           today ? Color(255, 255, 255, 255) : Color(255, 0xC5, 0xCC, 0xDA),
                           RectF(r.X + slot * i, br.Y - S(16), slot, S(15)), StringAlignmentCenter, StringAlignmentFar, true);
            Text(g, WD[wd], today ? semiFont.c_str() : L"Segoe UI", S(11), FontStyleRegular,
                 today ? Color(255, 255, 255, 255) : Color(255, 0x7E, 0x87, 0x9B),
                 RectF(r.X + slot * i, r.Y + top + maxH + S(3), slot, S(15)), StringAlignmentCenter, StringAlignmentNear, true);
        }
    }

    // полоска автозакрытия
    if (p->autoMs && p->shownAt) {
        float left = 1.0f - std::min(1.0f, (GetTickCount() - p->shownAt) / (float)p->autoMs);
        Region old; g.GetClip(&old); g.SetClip(&cp);
        SolidBrush tb(Color(18, 255, 255, 255));
        g.FillRectangle(&tb, RectF(c.X, c.Y + c.Height - S(3), c.Width, S(3)));
        LinearGradientBrush pb(PointF(c.X, 0), PointF(c.X + c.Width, 0), p->c1, p->c2);
        g.FillRectangle(&pb, RectF(c.X, c.Y + c.Height - S(3), c.Width * left, S(3)));
        g.SetClip(&old);
    }
}

static float Ease(float t) { t = std::max(0.0f, std::min(1.0f, t)); return 1 - (1 - t) * (1 - t) * (1 - t); }

static void Render(Popup *p) {
    int W = p->W, H = p->H;
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr; HDC sdc = GetDC(NULL); HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP dib = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mdc, dib);
    {
        Bitmap bmp(W, H, W * 4, PixelFormat32bppPARGB, (BYTE *)bits);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(0, 0, 0, 0));
        Draw(g, p);
    }
    float e = Ease(p->t);
    POINT dst = { p->x, p->y + (int)((1 - e) * S(16)) * p->slideDir }; SIZE sz = { W, H }; POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(255 * e), AC_SRC_ALPHA };
    UpdateLayeredWindow(p->hwnd, sdc, &dst, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mdc, old); DeleteObject(dib); DeleteDC(mdc); ReleaseDC(NULL, sdc);
}

static void DismissAnimated() {
    if (!cur) return;
    Popup *p = cur; cur = nullptr;
    p->closing = true; p->animStart = GetTickCount(); p->hover = HIT_NONE;
    SetTimer(p->hwnd, T_ANIM, 15, NULL);
}

static wstring HHMM(int m) { wchar_t b[8]; swprintf(b, 8, L"%02d:%02d", m / 60 % 24, m % 60); return b; }
// ---------- свои напоминания на дату и время ----------
static const wchar_t *REM_KEY = L"Software\\StandReminder\\Reminders";   // id -> «время<TAB>текст» или «время<TAB>R<TAB>дни<TAB>база<TAB>текст»
struct Reminder { wstring id; time_t at; wstring text; int every = 0; time_t base = 0; };   // every — повтор раз в N дней (0 — разовое)
static bool RepeatOn() { return GetCfg(L"RepeatOn", 0) != 0; }      // повторяющиеся напоминания: по умолчанию выключены
static wstring EveryStr(int d) {
    return d == 1 ? wstring(L"каждый день") : d == 2 ? wstring(L"через день") : d == 7 ? wstring(L"раз в неделю") : L"каждые " + Days(d);
}
static std::vector<wstring> remQueue;          // сработали, ждут показа
static std::set<wstring> remFired;             // уже сработали в этот запуск (в реестре до «Готово»)
static std::vector<Reminder> menuRems;
static std::vector<Reminder> RemList() {
    std::vector<Reminder> v; HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REM_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return v;
    for (DWORD i = 0;; i++) {
        wchar_t name[64], data[600]; DWORD nl = 64, dl = sizeof data - 2, type;
        if (RegEnumValueW(k, i, name, &nl, NULL, &type, (BYTE *)data, &dl) != ERROR_SUCCESS) break;
        if (type != REG_SZ) continue;
        data[dl / 2] = 0; wstring d = data; size_t tab = d.find(L'\t');
        if (tab == wstring::npos) continue;
        Reminder r; r.id = name; r.at = (time_t)_wtoi64(d.substr(0, tab).c_str()); r.text = d.substr(tab + 1);
        if (r.text.compare(0, 2, L"R\t") == 0) {          // повторяющееся
            wstring rest = r.text.substr(2); size_t t1 = rest.find(L'\t'), t2 = t1 == wstring::npos ? t1 : rest.find(L'\t', t1 + 1);
            if (t2 == wstring::npos) continue;
            r.every = std::max(1, _wtoi(rest.substr(0, t1).c_str())); r.base = (time_t)_wtoi64(rest.substr(t1 + 1, t2 - t1 - 1).c_str());
            r.text = rest.substr(t2 + 1); if (!r.base) r.base = r.at;
        }
        v.push_back(r);
    }
    RegCloseKey(k);
    std::sort(v.begin(), v.end(), [](const Reminder &a, const Reminder &b) { return a.at < b.at; });
    return v;
}
static bool RemGet(const wstring &id, Reminder &r) { for (auto &x : RemList()) if (x.id == id) { r = x; return true; } return false; }
static void RemSave(const wstring &id, time_t at, const wstring &text, int every = 0, time_t base = 0) {
    wstring v = std::to_wstring((long long)at) + L"\t";
    if (every > 0) v += L"R\t" + std::to_wstring(every) + L"\t" + std::to_wstring((long long)(base ? base : at)) + L"\t";
    SetRegSz(REM_KEY, id, v + text);
}
static std::vector<Reminder> RemVisible() {    // что показывать в списках: повторяющиеся — только если функция включена
    std::vector<Reminder> v; for (auto &r : RemList()) if (!r.every || RepeatOn()) v.push_back(r);
    if (v.size() > 40) v.resize(40);
    return v;
}
static time_t NextOccur(time_t base, int every) {   // следующий раз после «сейчас», в то же время суток
    time_t now = time(NULL), t = base; struct tm m = *localtime(&base);
    for (int guard = 0; guard < 5000 && t <= now; guard++) { m.tm_mday += every; m.tm_isdst = -1; t = mktime(&m); }
    return t;
}
static void RemAdd(time_t at, const wstring &text, int every = 0) {
    static unsigned n = 0;
    wstring id = L"r" + std::to_wstring((long long)time(NULL)) + L"_" + std::to_wstring(GetTickCount() % 100000) + L"_" + std::to_wstring(n++);
    RemSave(id, at, text.empty() ? (every ? L"Тренировка" : L"Напоминание") : text, every, at);
}
static void RemDelete(const wstring &id) { DelRegV(REM_KEY, id); remFired.erase(id); }
static time_t AddMonths(time_t t, int months) {
    struct tm m = *localtime(&t); int day = m.tm_mday;
    m.tm_mon += months; m.tm_mday = 1; mktime(&m);
    static const int ML[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int y = m.tm_year + 1900, ml = ML[m.tm_mon] + (m.tm_mon == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0);
    m.tm_mday = std::min(day, ml); m.tm_isdst = -1;
    return mktime(&m);
}
static wstring WhenStr(time_t t) {             // «сегодня в 21:30», «завтра в 09:00», «17.12.2026 в 10:00»
    struct tm a = *localtime(&t); time_t now = time(NULL); struct tm b = *localtime(&now);
    time_t tm1 = now + 86400; struct tm c = *localtime(&tm1);
    wchar_t hm[8]; swprintf(hm, 8, L"%02d:%02d", a.tm_hour, a.tm_min);
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) return wstring(L"сегодня в ") + hm;
    if (a.tm_year == c.tm_year && a.tm_yday == c.tm_yday) return wstring(L"завтра в ") + hm;
    return DateStr(t) + L" в " + hm;
}
static void RemCheck() {                       // раз в секунду: что сработало
    time_t now = time(NULL);
    for (auto &r : RemList()) {
        if (r.at > now || remFired.count(r.id)) continue;
        if (r.every && !RepeatOn()) continue;          // повторяющиеся выключены — молчим, но не удаляем
        remFired.insert(r.id); remQueue.push_back(r.id);
        Log(L"Сработало своё напоминание: " + r.text);
        Chime();
    }
}
static time_t RoundUp5(time_t t) { struct tm m = *localtime(&t); m.tm_sec = 0; m.tm_min = (m.tm_min + 4) / 5 * 5; m.tm_isdst = -1; return mktime(&m); }
// ---------- обновление одной кнопкой (новые версии лежат в GitHub Releases) ----------
static HANDLE mtx = NULL;
static wstring updVersion, updUrl;             // найденная новая версия
static bool updBusy = false, updManual = false, updPopupPending = false;
static int VerNum(const wstring &v) {          // «2.10» -> 2010
    int a = 0, b = 0; swscanf(v.c_str() + (v.size() && (v[0] == L'v' || v[0] == L'V') ? 1 : 0), L"%d.%d", &a, &b); return a * 1000 + b;
}
static wstring TempFile(const wchar_t *name) { wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); return wstring(t) + name; }
// latest.txt в репозитории: 1-я строка — версия, 2-я — ссылка на exe (если нет — стандартная)
#define UPDATE_RAW L"https://raw.githubusercontent.com/" UPDATE_REPO L"/main/"
static DWORD WINAPI UpdCheckThread(LPVOID) {    // в фоне: какая версия последняя
    wstring url = UPDATE_RAW L"latest.txt?nocache=" + std::to_wstring((long long)time(NULL));
    wstring tmp = TempFile(L"napominalka_latest.txt"); DeleteFileW(tmp.c_str());
    DeleteUrlCacheEntryW(url.c_str());
    wstring *res = nullptr;
    if (URLDownloadToFileW(NULL, url.c_str(), tmp.c_str(), BINDF_GETNEWESTVERSION, NULL) == S_OK) {
        HANDLE f = CreateFileW(tmp.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            std::string d(4096, 0); DWORD n = 0; ReadFile(f, &d[0], (DWORD)d.size(), &n, NULL); CloseHandle(f); d.resize(n);
            std::vector<std::string> lines; size_t p = 0;
            while (p < d.size()) { size_t e = d.find('\n', p); if (e == std::string::npos) e = d.size();
                std::string l = d.substr(p, e - p); while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back(); lines.push_back(l); p = e + 1; }
            if (!lines.empty() && !lines[0].empty() && isdigit((unsigned char)lines[0][0])) {
                wstring ver(lines[0].begin(), lines[0].end());
                wstring dl = lines.size() > 1 && !lines[1].compare(0, 8, "https://") ? wstring(lines[1].begin(), lines[1].end())
                                                                                    : wstring(UPDATE_RAW L"release/Napominalka.exe");
                res = new wstring(ver + L"\n" + dl);
            }
        }
        DeleteFileW(tmp.c_str());
    }
    PostMessageW(hMain, WM_UPDCHECKED, 0, (LPARAM)res);
    return 0;
}
static void UpdCheck(bool manual) {
    if (updBusy) return;
    updBusy = true; updManual = manual;
    HANDLE t = CreateThread(NULL, 0, UpdCheckThread, NULL, 0, NULL); if (t) CloseHandle(t); else updBusy = false;
}
static wstring ExePath() { wchar_t p[1024]; GetModuleFileNameW(NULL, p, 1024); return p; }
static DWORD WINAPI UpdDownloadThread(LPVOID) {
    wstring dst = ExePath() + L".new"; DeleteFileW(dst.c_str());
    DeleteUrlCacheEntryW(updUrl.c_str());
    bool ok = URLDownloadToFileW(NULL, updUrl.c_str(), dst.c_str(), BINDF_GETNEWESTVERSION, NULL) == S_OK;
    if (ok) {                                       // проверка: это exe и он не обрезан
        HANDLE f = CreateFileW(dst.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        char mz[2] = {}; DWORD n = 0; LARGE_INTEGER sz = {};
        if (f != INVALID_HANDLE_VALUE) { ReadFile(f, mz, 2, &n, NULL); GetFileSizeEx(f, &sz); CloseHandle(f); }
        ok = n == 2 && mz[0] == 'M' && mz[1] == 'Z' && sz.QuadPart > 500000;
        if (!ok) DeleteFileW(dst.c_str());
    }
    PostMessageW(hMain, WM_UPDDOWNLOADED, ok, 0);
    return 0;
}
static void UpdStartDownload() {
    if (updBusy || updUrl.empty()) return;
    updBusy = true;
    Log(L"Скачиваю обновление " + updVersion);
    HANDLE t = CreateThread(NULL, 0, UpdDownloadThread, NULL, 0, NULL); if (t) CloseHandle(t); else updBusy = false;
}
static void UpdInstall() {                      // заменить себя новым файлом и перезапуститься
    wstring exe = ExePath(), neu = exe + L".new", old = exe + L".old";
    DeleteFileW(old.c_str());
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MessageBoxW(hMain, L"Не получилось заменить файл программы (нет прав на папку?). Обновите вручную.", L"Напоминалка", MB_ICONWARNING | MB_TOPMOST);
        DeleteFileW(neu.c_str()); return;
    }
    if (!MoveFileExW(neu.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);   // вернуть как было
        MessageBoxW(hMain, L"Не получилось установить новую версию. Старая версия осталась на месте.", L"Напоминалка", MB_ICONWARNING | MB_TOPMOST);
        return;
    }
    Log(L"Обновление установлено: " APP_VERSION L" → " + updVersion + L", перезапуск");
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); mtx = NULL; }   // чтобы новая копия смогла запуститься
    wstring cmd = L"\"" + exe + L"\" --updated";
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    DestroyWindow(hMain);
}

static wstring showCustomId;
static void ShowPopup(Kind kind) {
    if (cur && cur->kind == P_CUSTOM && !cur->closing) remQueue.insert(remQueue.begin(), cur->customId);   // вернётся позже
    if (cur) { DestroyWindow(cur->hwnd); cur = nullptr; }
    Popup *p = new Popup(); p->kind = kind;
    p->label = L"НАПОМИНАЛКА  ·  " + Clock(time(NULL));
    switch (kind) {
    case P_REMIND:
        p->title = L"Пора встать!";
        p->text = L"Ты сидишь уже " + Min(std::max(1, (int)((time(NULL) - sittingSince + 30) / 60))) + L". Встань на " + Min(STAND_MIN) + L" — пройдись, потянись, попей воды.";
        p->glyph = G_PERSON; p->c1 = Color(255, 0x3D, 0xDC, 0x97); p->c2 = Color(255, 0x14, 0x9E, 0xA8);
        p->nb = 3; p->btn[0] = { {}, L"Через " + Min(SNOOZE_MIN), false }; p->btn[1] = { {}, L"Да, встал", true };
        p->btn[2] = { {}, L"Я уже постоял " + Min(STAND_MIN), false };
        break;
    case P_DONE:
        p->title = L"Готово! Можно садиться";
        p->text = L"Ты постоял " + Min(STAND_MIN) + L". Следующее напоминание через " + Min(WORK_MIN) + L".";
        p->glyph = G_CHECK; p->c1 = Color(255, 0x60, 0xA5, 0xFA); p->c2 = Color(255, 0x63, 0x66, 0xF1);
        p->nb = 1; p->btn[0] = { {}, L"Отлично", true };
        break;
    case P_STATS: {
        for (int i = 0; i < 7; i++) p->chartV[i] = (int)StatGet(DayKey(time(NULL) - (time_t)(6 - i) * 86400));
        int today = StatGet(DayKey(time(NULL))), total = (int)StatGet(L"Total"), steps = (int)GetCfg(L"StepsPerDay", 6500);
        p->title = L"Сегодня ты вставал " + Times(today);
        p->text = Min(today * STAND_MIN) + L" на ногах   ·   серия " + Days(StatStreak()) + L" подряд";
        p->text2 = L"7 дней:  " + Times(StatDays(7)) + L"   ·   30 дней:  " + Times(StatDays(30)) + L"   ·   всего:  " + std::to_wstring(total) + L"\n"
                   L"Лучший день:  " + Times(StatBestDay()) + L"\n\n"
                   L"Польза за всё время\n"
                   L"На ногах:  " + Dur(total * STAND_MIN) + L"   ·   ≈ " + Num((int)(total * STAND_MIN * 1.5 + 0.5)) + L" ккал сверх сидения\n"
                   L"Глаза отдыхали:  " + Times(total) + L"  (≈ " + Dur0(total * 20 / 60) + L" взгляда вдаль)\n"
                   L"Шаги:  " + Num(steps) + L" в день  ≈  " + Km(steps) + L" км,  ≈ " + std::to_wstring((int)(steps * 0.04 + 0.5)) + L" ккал\n\n"
                   L"Программа создана " APP_CREATED;
        p->glyph = G_PERSON; p->c1 = Color(255, 0x3D, 0xDC, 0x97); p->c2 = Color(255, 0x14, 0x9E, 0xA8);
        p->nb = 2; p->btn[0] = { {}, L"Убрать 1 за сегодня", false }; p->btn[1] = { {}, L"Закрыть", true };
        break; }
    case P_QUITS: {
        std::vector<QuitItem> q = QuitList();
        p->title = q.empty() ? L"Отказы" : L"Мои отказы";
        if (q.empty()) p->text = L"Пока ничего нет. Добавьте отказ в меню значка: «Отказы → Добавить».";
        else {
            p->text = L"Сколько уже держишься:";
            for (auto &x : q) {
                wstring l2 = Elapsed(x.start);
                if (x.best > time(NULL) - x.start) l2 += L"   ·   рекорд: " + Span(x.best);
                p->rows.push_back({ x.name, L"с " + DateStr(x.start), l2 });
            }
        }
        p->glyph = G_CHECK; p->c1 = Color(255, 0xA7, 0x8B, 0xFA); p->c2 = Color(255, 0x7C, 0x3A, 0xED);
        p->nb = 1; p->btn[0] = { {}, L"Закрыть", true };
        break; }
    case P_GAMES: {
        GameAgg a = GameStats();
        long long w = 0, mo = 0; for (int i = 0; i < 30; i++) { mo += a.day[i]; if (i < 7) w += a.day[i]; }
        for (int i = 0; i < 7; i++) p->chartV[i] = (int)(a.day[6 - i] / 60);
        p->chartMinutes = true;
        p->title = L"В играх сегодня: " + HM(a.day[0]);
        p->text = L"7 дней:  " + HM(w) + L"   ·   30 дней:  " + HM(mo) + L"   ·   всего:  " + HM(a.all);
        if (!gamesNow.empty()) {
            wstring now = L"\nСейчас запущено: ";
            for (size_t i = 0; i < gamesNow.size(); i++) now += (i ? L", " : L"") + gamesNow[i];
            p->text += now;
        }
        std::vector<std::pair<long long, wstring>> order;
        for (auto &t : a.total) order.push_back({ t.second, t.first });
        std::sort(order.rbegin(), order.rend());
        for (size_t i = 0; i < order.size() && i < 8; i++) {
            const wstring &g = order[i].second;
            wstring l2 = L"сегодня " + HM(a.today[g]) + L"   ·   7 дней " + HM(a.week[g]) + L"   ·   30 дней " + HM(a.month[g]);
            p->rows.push_back({ g, L"всего " + HM(order[i].first), l2 });
        }
        if (order.empty()) p->text += L"\n\nПока игр нет. Игра добавится сама, если открыть её на весь экран на минуту, "
                                      L"или добавьте её в меню: «Время в играх → Добавить игру вручную».";
        p->glyph = G_CLOCK; p->c1 = Color(255, 0x38, 0xBD, 0xF8); p->c2 = Color(255, 0x25, 0x63, 0xEB);
        p->nb = 1; p->btn[0] = { {}, L"Закрыть", true };
        break; }
    case P_CUSTOM: {
        p->customId = showCustomId;
        Reminder r; if (!RemGet(p->customId, r)) r = { p->customId, time(NULL), L"Напоминание" };
        p->title = r.text;
        long late = (long)(time(NULL) - r.at);
        p->text = late > 120 ? L"Было назначено на " + DateStr(r.at) + L" " + HHMM((int)(localtime(&r.at)->tm_hour * 60 + localtime(&r.at)->tm_min))
                             : wstring(L"Ваше напоминание");
        if (r.every) p->text += L"\nПовторяется " + EveryStr(r.every) + L". «Готово» — следующий раз " + WhenStr(NextOccur(r.base, r.every)) + L".";
        p->glyph = G_CLOCK; p->c1 = Color(255, 0xF4, 0x72, 0xB6); p->c2 = Color(255, 0xDB, 0x27, 0x77);
        p->nb = 3; p->btn[0] = { {}, L"Через 10 минут", false }; p->btn[1] = { {}, L"Готово", true }; p->btn[2] = { {}, L"Через 1 час", false };
        break; }
    case P_UPDATE:
        p->title = L"Доступна новая версия " + updVersion;
        p->text = L"Сейчас у вас " APP_VERSION L". Обновление займёт несколько секунд, статистика и настройки сохранятся.";
        p->glyph = G_CHECK; p->c1 = Color(255, 0x34, 0xD3, 0x99); p->c2 = Color(255, 0x05, 0x96, 0x69);
        p->nb = 2; p->btn[0] = { {}, L"Позже", false }; p->btn[1] = { {}, L"Обновить", true };
        break;
    case P_TIMER:
        p->title = L"Таймер сработал";
        p->text = L"Прошло " + (timerFiredMin == 1 ? wstring(L"1 минута") : Min(timerFiredMin)) + L".";
        p->glyph = G_CLOCK; p->c1 = Color(255, 0xFB, 0xBF, 0x24); p->c2 = Color(255, 0xF9, 0x73, 0x16);
        p->nb = 2; p->btn[0] = { {}, L"Ещё " + Min(timerFiredMin), false }; p->btn[1] = { {}, L"Готово", true };
        break;
    case P_POSDEMO:
        p->title = L"Окно будет появляться здесь";
        p->text = L"Можно перетащить его мышкой в любое удобное место — программа запомнит.";
        p->glyph = G_CHECK; p->c1 = Color(255, 0x60, 0xA5, 0xFA); p->c2 = Color(255, 0x63, 0x66, 0xF1);
        p->nb = 1; p->btn[0] = { {}, L"Понятно", true };
        break;
    case P_QUIT:
        p->title = L"Выключить напоминалку?";
        p->text = L"Напоминания перестанут приходить до следующего запуска.";
        p->glyph = G_POWER; p->c1 = Color(255, 0xFB, 0x71, 0x85); p->c2 = Color(255, 0xE1, 0x1D, 0x48);
        p->nb = 2; p->btn[0] = { {}, L"Отмена", false }; p->btn[1] = { {}, L"Выключить", true };
        break;
    }
    Layout(p);
    p->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"StandPopup", p->title.c_str(), WS_POPUP, p->x, p->y + (int)S(16) * p->slideDir, p->W, p->H, NULL, NULL, hInst, NULL);
    SetWindowLongPtrW(p->hwnd, GWLP_USERDATA, (LONG_PTR)p);
    cur = p;
    p->animStart = GetTickCount(); p->shownAt = GetTickCount(); p->t = 0;
    Render(p);
    ShowWindow(p->hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(p->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetTimer(p->hwnd, T_ANIM, 15, NULL);
    SetTimer(p->hwnd, T_TOP, 1000, NULL);
    p->autoMs = kind == P_DONE ? DONE_CLOSE_MS : kind == P_REMIND ? IGNORE_MS : kind == P_POSDEMO ? 8000 : kind == P_TIMER ? 120000 : kind == P_QUIT ? 30000 : 0;
    if (p->autoMs) SetTimer(p->hwnd, T_TICK, 100, NULL);
    if (kind == P_REMIND || kind == P_DONE) {
        if (VoiceOn()) Speak(kind == P_REMIND ? 0 : 1);
        else Chime();
    }
}

static void OnAction(Popup *p, int id) {
    Kind kind = p->kind;
    if (id == HIT_CLOSE) {
        if (kind == P_REMIND) { ShowPopup(P_QUIT); return; }
        if (kind == P_QUIT) id = HIT_B0;
    }
    if (kind == P_QUIT && id == HIT_B1) { Log(L"Программа выключена кнопкой «Выключить»"); DestroyWindow(hMain); return; }
    if (kind == P_REMIND) Log(id == HIT_B1 ? L"Нажато «Да, встал»" : id == HIT_B2 ? L"Нажато «Я уже постоял»" : id == HIT_CLOSE ? L"Нажат крестик" : L"Нажато «Позже»");
    if (kind == P_REMIND && id == HIT_B2) {      // уже постоял: засчитать и сразу отсчитывать час
        StatAddStandUp(); sittingSince = time(NULL);
        DismissAnimated(); Schedule(WORK_MIN, ACT_REMIND); return;
    }
    if (kind == P_UPDATE) { DismissAnimated(); if (id == HIT_B1) UpdStartDownload(); return; }
    if (kind == P_TIMER && id == HIT_B0) { DismissAnimated(); StartTimer(timerFiredMin); return; }
    if (kind == P_CUSTOM) {
        Reminder r; bool have = RemGet(p->customId, r);
        if (id == HIT_B0 || id == HIT_B2) {        // отложить (у повторяющихся расписание не сдвигается)
            if (have) { RemSave(r.id, time(NULL) + (id == HIT_B0 ? 600 : 3600), r.text, r.every, r.base); remFired.erase(r.id); }
        } else if (have && r.every) {              // «Готово» или крестик у повторяющегося — ждём следующий раз
            time_t nx = NextOccur(r.base, r.every); RemSave(r.id, nx, r.text, r.every, nx); remFired.erase(r.id);
            Log(L"Повторяющееся «" + r.text + L"» — следующий раз " + WhenStr(nx));
        } else RemDelete(p->customId);            // разовое: «Готово» и крестик удаляют
        DismissAnimated(); return;
    }
    if (kind == P_STATS && id == HIT_B0) { StatAdjustToday(-1); ShowPopup(P_STATS); return; }
    DismissAnimated();
    if (kind == P_REMIND) {
        if (id == HIT_B1) { StatAddStandUp(); Schedule(STAND_MIN, ACT_DONE); }
        else Schedule(SNOOZE_MIN, ACT_REMIND);
    } else if (kind == P_QUIT) {
        if (dueAction == ACT_NONE && pendingKind < 0) Schedule(SNOOZE_MIN, ACT_REMIND);
    }
}

static int HitTest(Popup *p, int x, int y) {
    PointF pt((REAL)x, (REAL)y);
    if (p->close.Contains(pt)) return HIT_CLOSE;
    for (int i = 0; i < p->nb; i++) if (p->btn[i].r.Contains(pt)) return i;
    return HIT_NONE;
}

static LRESULT CALLBACK PopupProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Popup *p = (Popup *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_MOUSEMOVE: if (p && !p->closing) {
        if (p->dragging) {
            POINT c; GetCursorPos(&c);
            int dx = c.x - p->dragStart.x, dy = c.y - p->dragStart.y;
            if (abs(dx) > 3 || abs(dy) > 3) p->dragMoved = true;
            if (p->dragMoved) { p->x = p->dragX + dx; p->y = p->dragY + dy; p->t = 1; Render(p); }
            return 0;
        }
        int hit = HitTest(p, (short)LOWORD(lp), (short)HIWORD(lp));
        if (hit != p->hover) { p->hover = hit; Render(p); }
        TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, h, 0 }; TrackMouseEvent(&te);
    } return 0;
    case WM_MOUSELEAVE: if (p && p->hover != HIT_NONE) { p->hover = HIT_NONE; Render(p); } return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, p && p->hover != HIT_NONE ? IDC_HAND : IDC_SIZEALL)); return TRUE;
    case WM_LBUTTONDOWN: if (p && !p->closing) {
        p->press = HitTest(p, (short)LOWORD(lp), (short)HIWORD(lp)); SetCapture(h);
        if (p->press == HIT_NONE) {   // тянем окно за любое место, кроме кнопок
            p->dragging = true; p->dragMoved = false; GetCursorPos(&p->dragStart); p->dragX = p->x; p->dragY = p->y;
        }
        Render(p);
    } return 0;
    case WM_LBUTTONUP: if (p) {
        ReleaseCapture();
        if (p->dragging) {
            p->dragging = false;
            if (p->dragMoved) { SetCfg(L"Position", POS_CUSTOM); SetCfg(L"PosX", (DWORD)p->x); SetCfg(L"PosY", (DWORD)p->y); }
            p->press = HIT_NONE; return 0;
        }
        int hit = HitTest(p, (short)LOWORD(lp), (short)HIWORD(lp)), pr = p->press;
        p->press = HIT_NONE;
        if (!p->closing && hit != HIT_NONE && hit == pr) OnAction(p, hit);
        else if (!p->closing) Render(p);
    } return 0;
    case WM_TIMER: if (!p) return 0;
        if (wp == T_ANIM) {
            float el = (GetTickCount() - p->animStart) / (p->closing ? 170.0f : 260.0f);
            p->t = p->closing ? 1 - std::min(1.0f, el) : std::min(1.0f, el);
            Render(p);
            if (el >= 1) { KillTimer(h, T_ANIM); if (p->closing) DestroyWindow(h); }
        } else if (wp == T_TOP) {
            // запустили игру, пока окно висит: убираем его с экрана (не мешаем игре и не трогаем FPS)
            // и покажем снова, когда игру свернут
            if (!p->closing && !p->dragging && cur == p && (p->kind == P_REMIND || p->kind == P_DONE) && ShouldDefer()) {
                pendingKind = p->kind; cur = nullptr; DestroyWindow(h); UpdateTip(); return 0;
            }
            SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else if (wp == T_TICK && !p->closing) {
            bool idleNow = false;
            if (p->kind == P_REMIND) { LASTINPUTINFO li = { sizeof li }; idleNow = GetLastInputInfo(&li) && GetTickCount() - li.dwTime > 15000; }
            // пока мышь над окном или за компьютером никого (не трогали мышь/клавиатуру) — «игнор» не считаем
            bool hiddenByFs = (p->kind == P_REMIND) && IsFullscreenApp();   // поверх игры окно может быть не видно
            if (p->hover != HIT_NONE || p->dragging || idleNow || hiddenByFs) p->shownAt += 100;
            if (GetTickCount() - p->shownAt >= p->autoMs) {
                KillTimer(h, T_TICK);
                if (cur == p) {
                    DismissAnimated();
                    if (p->kind == P_REMIND) { Schedule(IGNORE_MIN, ACT_REMIND); Log(L"Напоминание проигнорировано — повтор через " + std::to_wstring(IGNORE_MIN) + L" мин"); }  // проигнорировали
                    if (p->kind == P_QUIT && dueAction == ACT_NONE && pendingKind < 0) Schedule(SNOOZE_MIN, ACT_REMIND);   // окно «Выключить?» без ответа
                }
            }
            else Render(p);
        }
        return 0;
    case WM_NCDESTROY:
        if (cur == p) cur = nullptr;
        delete p; SetWindowLongPtrW(h, GWLP_USERDATA, 0); break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ---------- автозапуск ----------
static bool AutoStartOn() {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    bool on = RegQueryValueExW(k, RUN_NAME, NULL, NULL, NULL, NULL) == ERROR_SUCCESS; RegCloseKey(k); return on;
}
static void SetAutoStart(bool on) {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t path[MAX_PATH]; GetModuleFileNameW(NULL, path, MAX_PATH);
        wstring v = L"\"" + wstring(path) + L"\"";
        RegSetValueExW(k, RUN_NAME, 0, REG_SZ, (const BYTE *)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else RegDeleteValueW(k, RUN_NAME);
    RegCloseKey(k);
}

// ---------- main window / tray ----------
// ---------- чёрный экран для OLED ----------
// Окно на весь монитор, закрашенное чистым чёрным (пиксели OLED выключены). Живёт свёрнутым на панели задач:
// нажали иконку — экран чёрный; клик или любая клавиша — снова свернулось.
static HICON blackIconBig = NULL, blackIconSmall = NULL;
static HICON MakeBlackIcon(int sz) {
    Bitmap bm(sz, sz, PixelFormat32bppARGB); Graphics g(&bm);
    g.SetSmoothingMode(SmoothingModeAntiAlias); g.Clear(Color(0, 0, 0, 0));
    float r = sz * 0.2f, pad = sz * 0.06f; RectF rc(pad, pad, sz - 2 * pad - 1, sz - 2 * pad - 1);
    GraphicsPath p; RR(p, rc, r);
    SolidBrush b(Color(255, 0, 0, 0)); g.FillPath(&b, &p);
    // маленький месяц
    float cx = sz * 0.5f, cy = sz * 0.5f, mr = sz * 0.2f;
    SolidBrush mb(Color(255, 0x9C, 0xA3, 0xAF)); g.FillEllipse(&mb, cx - mr, cy - mr, mr * 2, mr * 2);
    g.FillEllipse(&b, cx - mr + mr * 0.55f, cy - mr - mr * 0.25f, mr * 2, mr * 2);
    HICON hi = NULL; bm.GetHICON(&hi); return hi;
}
static bool blackAuto = false; static POINT blackAutoPt;     // включён сам по бездействию
static void BlackCover(HWND h) {                // растянуть на монитор, где сейчас мышь
    POINT pt; GetCursorPos(&pt);
    MONITORINFO mi = { sizeof mi }; GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT r = mi.rcMonitor, cur; GetWindowRect(h, &cur);
    if (!EqualRect(&r, &cur)) SetWindowPos(h, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE);
}
static LRESULT CALLBACK BlackProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_ERASEBKGND: { RECT r; GetClientRect(h, &r); FillRect((HDC)wp, &r, (HBRUSH)GetStockObject(BLACK_BRUSH)); return 1; }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); FillRect(dc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH)); EndPaint(h, &ps); return 0; }
    case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; } break;   // курсор не светится
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) BlackCover(h);
        else if (blackAuto) { blackAuto = false; SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
        return 0;
    case WM_MOUSEMOVE:
        if (blackAuto) {                         // включился сам — уходит, как только тронули мышь
            POINT pt; GetCursorPos(&pt);
            if (abs(pt.x - blackAutoPt.x) > 8 || abs(pt.y - blackAutoPt.y) > 8) ShowWindow(h, SW_MINIMIZE);
        }
        return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_KEYDOWN:
        ShowWindow(h, SW_MINIMIZE); return 0;
    case WM_SYSKEYDOWN:
        if (wp == VK_F4) break;                  // Alt+F4 — закрыть как обычно
        ShowWindow(h, SW_MINIMIZE); return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: if (hBlack == h) hBlack = NULL; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
// Своё «приложение» для панели задач: иначе Windows показывает общую иконку программы
static void SetPropStr(IPropertyStore *ps, const PROPERTYKEY &k, const wstring &v) {
    PROPVARIANT pv; PropVariantInit(&pv); pv.vt = VT_LPWSTR;
    pv.pwszVal = (LPWSTR)CoTaskMemAlloc((v.size() + 1) * sizeof(wchar_t)); if (!pv.pwszVal) return;
    memcpy(pv.pwszVal, v.c_str(), (v.size() + 1) * sizeof(wchar_t));
    ps->SetValue(k, pv); PropVariantClear(&pv);
}
static void BlackSetAppId(HWND h) {
    static const GUID AUM = { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } };
    PROPERTYKEY kId = { AUM, 5 }, kCmd = { AUM, 2 }, kIcon = { AUM, 3 }, kName = { AUM, 4 };
    IPropertyStore *ps = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(h, IID_IPropertyStore, (void **)&ps)) || !ps) return;
    wchar_t exe[1024]; GetModuleFileNameW(NULL, exe, 1024);
    SetPropStr(ps, kCmd, L"\"" + wstring(exe) + L"\" --black");      // для закрепления на панели задач
    SetPropStr(ps, kIcon, wstring(exe) + L",-3");
    SetPropStr(ps, kName, L"Чёрный экран");
    SetPropStr(ps, kId, L"StandReminder.BlackScreen.v2");
    ps->Commit(); ps->Release();
}
static void BlackCreate(bool showNow) {
    if (!hBlack) {
        static bool reg = false;
        if (!reg) {
            WNDCLASSW wc = {}; wc.hInstance = hInst; wc.lpfnWndProc = BlackProc; wc.lpszClassName = L"StandBlackScreen";
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(3)); RegisterClassW(&wc); reg = true;
            blackIconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(3), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
            blackIconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(3), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
            if (!blackIconBig) blackIconBig = MakeBlackIcon(GetSystemMetrics(SM_CXICON));
            if (!blackIconSmall) blackIconSmall = MakeBlackIcon(GetSystemMetrics(SM_CXSMICON));
        }
        MONITORINFO mi = { sizeof mi }; POINT pt; GetCursorPos(&pt);
        GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi); RECT r = mi.rcMonitor;
        hBlack = CreateWindowExW(WS_EX_APPWINDOW, L"StandBlackScreen", L"Чёрный экран", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                                 r.left, r.top, r.right - r.left, r.bottom - r.top, NULL, NULL, hInst, NULL);
        if (!hBlack) return;
        BlackSetAppId(hBlack);
        SendMessageW(hBlack, WM_SETICON, ICON_BIG, (LPARAM)blackIconBig);
        SendMessageW(hBlack, WM_SETICON, ICON_SMALL, (LPARAM)blackIconSmall);
        if (!showNow) { ShowWindow(hBlack, SW_SHOWMINNOACTIVE); return; }
    }
    ShowWindow(hBlack, SW_RESTORE); BlackCover(hBlack); SetForegroundWindow(hBlack);
}
static void BlackAutoCheck() {
    int mins = (int)GetCfg(L"AutoBlackMin", 1); if (mins <= 0) return;
    if (hBlack && !IsIconic(hBlack) && IsWindowVisible(hBlack)) return;   // уже чёрный
    LASTINPUTINFO li = { sizeof li }; if (!GetLastInputInfo(&li)) return;
    DWORD idle = (GetTickCount() - li.dwTime) / 1000;
    if (idle < (DWORD)mins * 60) return;
    static DWORD lastTry = 0;                    // проверки тяжелее — не чаще раза в 5 секунд
    if (lastTry && GetTickCount() - lastTry < 5000) return;
    lastTry = GetTickCount();
    if (IsFullscreenApp() || DisplayRequired()) return;     // фильм, игра, видео — не мешаем
    if (!gamesNow.empty()) return;                           // запущена игра (даже в окне, с геймпадом) — не трогаем
    if (GsiActive()) return;                                 // открыта Dota 2
    BlackCreate(false);
    if (!hBlack) return;
    GetCursorPos(&blackAutoPt); blackAuto = true;
    ShowWindow(hBlack, SW_SHOWNOACTIVATE); ShowWindow(hBlack, SW_RESTORE);
    POINT pt = blackAutoPt; MONITORINFO mi = { sizeof mi }; GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT r = mi.rcMonitor;
    SetWindowPos(hBlack, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_SHOWWINDOW);   // поверх всего, и панели задач
    SetForegroundWindow(hBlack);
}

static HICON MakeTrayIcon() {
    return (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                             GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
}
static void AddTray() {
    nid.cbSize = sizeof nid; nid.hWnd = hMain; nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; nid.uCallbackMessage = WM_TRAY;
    if (!nid.hIcon) nid.hIcon = MakeTrayIcon();
    if (!nid.hIcon) nid.hIcon = LoadIcon(NULL, IDI_INFORMATION);
    wcscpy(nid.szTip, L"Напоминалка");
    Shell_NotifyIconW(NIM_ADD, &nid); UpdateTip();
}

// ---------- ночной режим: тёплый экран по расписанию ----------
// Меняем «гамму» видеокарты (как f.lux): на игры и FPS не влияет.
static int nightOverride = 0;           // 0 — по расписанию, 1 — включён вручную, 2 — выключен вручную
static bool overrideSchedState = false;
static int nightAppliedK = 0, nightMaxOkK = 0;   // nightMaxOkK — самый тёплый цвет, который разрешила Windows
static bool nightLimited = false;
static DWORD previewUntil = 0;
static void KelvinMul(int k, double m[3]) {
    auto rgb = [](double K, double out[3]) {
        double t = K / 100.0, r, g, b;
        r = t <= 66 ? 255 : 329.698727446 * pow(t - 60, -0.1332047592);
        g = t <= 66 ? 99.4708025861 * log(t) - 161.1195681661 : 288.1221695283 * pow(t - 60, -0.0755148492);
        b = t >= 66 ? 255 : t <= 19 ? 0 : 138.5177312231 * log(t - 10) - 305.0447927307;
        out[0] = std::max(0.0, std::min(255.0, r)); out[1] = std::max(0.0, std::min(255.0, g)); out[2] = std::max(0.0, std::min(255.0, b));
    };
    double a[3], w[3]; rgb(k, a); rgb(6500, w);
    for (int i = 0; i < 3; i++) m[i] = std::min(1.0, a[i] / w[i]);
}
static bool ApplyKelvin(int k) {        // true, если Windows приняла цвет на всех мониторах
    double m[3]; KelvinMul(k >= 6500 ? 6500 : k, m);
    static WORD ramp[3][256];
    for (int c = 0; c < 3; c++) for (int i = 0; i < 256; i++) ramp[c][i] = (WORD)std::min(65535.0, i * 257.0 * (k >= 6500 ? 1.0 : m[c]));
    struct Ctx { bool ok; } ctx = { true };
    EnumDisplayMonitors(NULL, NULL, [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
        MONITORINFOEXW mi; mi.cbSize = sizeof mi;
        if (GetMonitorInfoW(hm, &mi)) {
            HDC dc = CreateDCW(L"DISPLAY", mi.szDevice, NULL, NULL);
            if (dc) { if (!SetDeviceGammaRamp(dc, ramp)) ((Ctx *)lp)->ok = false; DeleteDC(dc); }
        }
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.ok;
}
static int MinsNow() { time_t t = time(NULL); struct tm *m = localtime(&t); return m->tm_hour * 60 + m->tm_min; }
static double NightScheduleLevel() {    // 0 — день, 1 — ночь, между — плавный переход по 40 минут
    int st = (int)GetCfg(L"NightStart", 0), en = (int)GetCfg(L"NightEnd", 7 * 60), now = MinsNow();
    auto since = [](int from, int to) { return ((to - from) % 1440 + 1440) % 1440; };   // минут от from до to по кругу
    int len = since(st, en), pos = since(st, now);
    if (pos >= len) return 0;                     // день
    const double F = 40;
    double inL = pos / F, outL = (len - pos) / F;
    return std::max(0.0, std::min(1.0, std::min(inL, outL)));
}
static bool NightScheduled() { return GetCfg(L"NightOn", 1) && NightScheduleLevel() > 0; }
static void NightUpdate(bool force = false) {
    int target = (int)GetCfg(L"NightK", 3400);
    double lvl = GetCfg(L"NightOn", 1) ? NightScheduleLevel() : 0;
    bool sched = lvl > 0;
    if (nightOverride && sched != overrideSchedState) nightOverride = 0;   // наступило следующее «событие» расписания
    if (nightOverride == 1) lvl = 1; else if (nightOverride == 2) lvl = 0;
    if (previewUntil) { if (GetTickCount() < previewUntil) lvl = 1; else previewUntil = 0; }
    int k = (int)(6500 - (6500 - target) * lvl + 0.5);
    if (nightMaxOkK && k < nightMaxOkK) k = nightMaxOkK;
    if (!force && abs(k - nightAppliedK) < 20 && k >= 6500) return;   // днём ничего не трогаем
    if (ApplyKelvin(k)) { nightAppliedK = k; return; }
    // Windows не даёт такой тёплый цвет без особой настройки — ищем самый тёплый разрешённый
    nightLimited = true;
    for (int kk = k + 200; kk < 6500; kk += 200) if (ApplyKelvin(kk)) { nightMaxOkK = kk; nightAppliedK = kk; return; }
    ApplyKelvin(6500); nightAppliedK = 6500;
}

// ---------- отошёл от компьютера ----------
static bool away = false; bool AwayNow() { return away; }
static time_t awayLastInput = 0; static int awaySecOverride = 0;
static bool CountAway() { return GetCfg(L"CountAway", 1) != 0; }
static void CheckAway() {
    LASTINPUTINFO li = { sizeof li }; if (!GetLastInputInfo(&li)) return;
    DWORD idle = (GetTickCount() - li.dwTime) / 1000;
    DWORD limit = awaySecOverride ? awaySecOverride : (DWORD)STAND_MIN * 60;
    if (!away) {
        // мышь и клавиатуру не трогали всё время «перерыва», и это не кино/игра на весь экран
        if (idle >= limit && !IsFullscreenApp() && !(GsiActive() && gsiInMatch) && !DisplayRequired()) {   // смотрит видео — не «отошёл»
            away = true; awayLastInput = time(NULL) - idle;
            Log(L"Отошёл от компьютера (мышь и клавиатура не трогались " + std::to_wstring(idle / 60) + L" мин) — напоминания на паузе");
            if (cur && cur->kind == P_REMIND) { HWND w = cur->hwnd; cur = nullptr; DestroyWindow(w); }
            if (pendingKind == P_REMIND) pendingKind = -1;
            if (dueAction == ACT_REMIND) dueAction = ACT_NONE;      // пауза, пока не вернётся
            UpdateTip();
        }
    } else if (idle < 3) {                                           // вернулся
        away = false;
        Log(L"Вернулся к компьютеру — новый отсчёт " + std::to_wstring(WORK_MIN) + L" мин");
        if (CountAway() && lastCountAt < awayLastInput - 90) StatAddStandUp();   // не считать дважды после «Да, встал»
        if (dueAction != ACT_DONE) { sittingSince = time(NULL); Schedule(WORK_MIN, ACT_REMIND); }
        UpdateTip();
    }
}

// ---------- окно ввода названия ----------
static bool inputDone, inputOk; static HWND inputEdit; static wstring inputResult;
static LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { wchar_t b[64]; GetWindowTextW(inputEdit, b, 64); inputResult = b; inputOk = true; inputDone = true; }
        else if (LOWORD(wp) == IDCANCEL) inputDone = true;
        return 0;
    case WM_CLOSE: inputDone = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
static HWND inputDate, inputTime;
static bool AskText(HWND owner, const wchar_t *title, const wchar_t *prompt, wstring &out, SYSTEMTIME *dt = nullptr, bool allowEmpty = false, int maxLen = 40) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {}; wc.hInstance = hInst; wc.lpfnWndProc = InputProc; wc.lpszClassName = L"StandInput";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1)); RegisterClassW(&wc); reg = true;
    }
    int w = (int)S(380), hh = (int)S(dt ? 240 : 170);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    HWND d = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, L"StandInput", title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
        (wa.left + wa.right - w) / 2, (wa.top + wa.bottom - hh) / 2, w, hh, owner, NULL, hInst, NULL);
    HFONT f = CreateFontW(-(int)S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    RECT cr; GetClientRect(d, &cr); int cw = cr.right, pad = (int)S(16);
    HWND st = CreateWindowW(L"STATIC", prompt, WS_CHILD | WS_VISIBLE, pad, pad, cw - 2 * pad, (int)S(20), d, NULL, hInst, NULL);
    inputEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        pad, pad + (int)S(26), cw - 2 * pad, (int)S(26), d, NULL, hInst, NULL);
    SendMessageW(inputEdit, EM_SETLIMITTEXT, maxLen, 0);
    HWND st2 = NULL; inputDate = inputTime = NULL;
    if (dt) {
        INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_DATE_CLASSES }; InitCommonControlsEx(&ic);
        int y2 = pad + (int)S(62);
        st2 = CreateWindowW(L"STATIC", L"Когда напомнить:", WS_CHILD | WS_VISIBLE, pad, y2, cw - 2 * pad, (int)S(20), d, NULL, hInst, NULL);
        inputDate = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT,
            pad, y2 + (int)S(24), (int)((cw - 2 * pad) * 0.64f), (int)S(26), d, NULL, hInst, NULL);
        inputTime = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT | DTS_UPDOWN,
            pad + (int)((cw - 2 * pad) * 0.66f), y2 + (int)S(24), (int)((cw - 2 * pad) * 0.34f), (int)S(26), d, NULL, hInst, NULL);
        SendMessageW(inputTime, DTM_SETFORMATW, 0, (LPARAM)L"HH':'mm");
        SendMessageW(inputDate, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)dt);
        SendMessageW(inputTime, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)dt);
    }
    int bw = (int)S(100), bh = (int)S(30), by = cr.bottom - pad - bh;
    HWND ok = CreateWindowW(L"BUTTON", L"Готово", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, cw - pad - 2 * bw - (int)S(8), by, bw, bh, d, (HMENU)IDOK, hInst, NULL);
    HWND ca = CreateWindowW(L"BUTTON", L"Отмена", WS_CHILD | WS_VISIBLE | WS_TABSTOP, cw - pad - bw, by, bw, bh, d, (HMENU)IDCANCEL, hInst, NULL);
    for (HWND c : { st, inputEdit, ok, ca, st2, inputDate, inputTime }) if (c) SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
    inputDone = inputOk = false; inputResult.clear();
    ShowWindow(d, SW_SHOW); SetForegroundWindow(d); SetFocus(inputEdit);
    MSG msg;
    while (!inputDone && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(d, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (dt && inputOk) {
        SYSTEMTIME a = {}, b = {};
        SendMessageW(inputDate, DTM_GETSYSTEMTIME, 0, (LPARAM)&a); SendMessageW(inputTime, DTM_GETSYSTEMTIME, 0, (LPARAM)&b);
        a.wHour = b.wHour; a.wMinute = b.wMinute; a.wSecond = 0; a.wMilliseconds = 0; *dt = a;
    }
    DestroyWindow(d); DeleteObject(f);
    wstring r = inputResult;
    while (!r.empty() && r.back() == L' ') r.pop_back();
    while (!r.empty() && r.front() == L' ') r.erase(0, 1);
    for (auto &ch : r) if (ch == L'=' || ch == L';' || ch == L'\\' || ch == L'\r' || ch == L'\n') ch = L' ';
    out = r; return inputOk && (allowEmpty || !r.empty());
}
static std::string W2U(const wstring &w) {
    char b[1024]; int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, b, sizeof b, NULL, NULL); return n > 0 ? std::string(b) : "";
}
static wstring U2W(const std::string &u) {
    wchar_t b[512]; int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, b, 512); return n > 0 ? wstring(b) : L"";
}
static std::vector<QuitItem> menuQuits;
static std::vector<wstring> menuGames;     // список, по которому построено меню

static bool AskRepeat(HWND h, int every) {       // повторяющееся: текст + когда первый раз
    wstring text; SYSTEMTIME st = {};
    time_t now = time(NULL); struct tm m = *localtime(&now); m.tm_min = 0; m.tm_sec = 0; m.tm_hour = 18; m.tm_isdst = -1;
    time_t def = mktime(&m); if (def <= now) { m.tm_mday += 1; m.tm_isdst = -1; def = mktime(&m); }   // по умолчанию 18:00
    struct tm d = *localtime(&def);
    st.wYear = (WORD)(d.tm_year + 1900); st.wMonth = (WORD)(d.tm_mon + 1); st.wDay = (WORD)d.tm_mday; st.wHour = (WORD)d.tm_hour; st.wMinute = (WORD)d.tm_min;
    wstring prompt = L"О чём напоминать " + EveryStr(every) + L"? (например, «Тренировка»)";
    if (!AskText(h, L"Повторяющееся напоминание", prompt.c_str(), text, &st, true, 120)) return false;
    struct tm t = {}; t.tm_year = st.wYear - 1900; t.tm_mon = st.wMonth - 1; t.tm_mday = st.wDay; t.tm_hour = st.wHour; t.tm_min = st.wMinute; t.tm_isdst = -1;
    time_t at = mktime(&t);
    if (at <= now) at = NextOccur(at, every);          // первый раз в прошлом — берём ближайший будущий
    RemAdd(at, text, every);
    Log(L"Добавлено повторяющееся (" + EveryStr(every) + L"), первый раз " + WhenStr(at));
    return true;
}
static void RemShift(const Reminder &r, int days) {     // сдвинуть расписание на N дней
    struct tm m = *localtime(&r.base); m.tm_mday += days; m.tm_isdst = -1; time_t nb = mktime(&m);
    time_t at = nb; if (at <= time(NULL)) at = NextOccur(nb, r.every);
    RemSave(r.id, at, r.text, r.every, at); remFired.erase(r.id);
}
static bool AskReminder(HWND h, time_t at, bool pickTime) {
    wstring text; SYSTEMTIME st = {};
    if (pickTime) {
        time_t def = RoundUp5(time(NULL) + 3600); struct tm m = *localtime(&def);
        st.wYear = (WORD)(m.tm_year + 1900); st.wMonth = (WORD)(m.tm_mon + 1); st.wDay = (WORD)m.tm_mday;
        st.wHour = (WORD)m.tm_hour; st.wMinute = (WORD)m.tm_min;
        if (!AskText(h, L"Новое напоминание", L"О чём напомнить?", text, &st, true, 120)) return false;
        struct tm t = {}; t.tm_year = st.wYear - 1900; t.tm_mon = st.wMonth - 1; t.tm_mday = st.wDay;
        t.tm_hour = st.wHour; t.tm_min = st.wMinute; t.tm_isdst = -1; at = mktime(&t);
        if (at <= time(NULL)) { MessageBoxW(h, L"Это время уже прошло. Выберите время в будущем.", L"Напоминалка", MB_ICONWARNING | MB_TOPMOST); return AskReminder(h, 0, true); }
    } else {
        wstring prompt = L"О чём напомнить " + WhenStr(at) + L"?";
        if (!AskText(h, L"Новое напоминание", prompt.c_str(), text, nullptr, true, 120)) return false;
    }
    RemAdd(at, text);
    return true;
}

// ---------- перенос статистики на другой Windows ----------
static bool PickFile(HWND h, bool save, wchar_t *path) {
    OPENFILENAMEW o = {}; o.lStructSize = sizeof o; o.hwndOwner = h;
    o.lpstrFilter = L"Статистика Напоминалки (*.txt)\0*.txt\0Все файлы\0*.*\0";
    o.lpstrFile = path; o.nMaxFile = MAX_PATH; o.lpstrDefExt = L"txt";
    o.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o);
}
static void ExportStats(HWND h) {
    wchar_t path[MAX_PATH] = L"Напоминалка_статистика.txt";
    if (!PickFile(h, true, path)) return;
    std::string out = "Napominalka stats v1\r\n";
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STATS_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        for (DWORD i = 0;; i++) {
            wchar_t name[64]; DWORD nl = 64, type, v, vs = sizeof v;
            if (RegEnumValueW(k, i, name, &nl, NULL, &type, (BYTE *)&v, &vs) != ERROR_SUCCESS) break;
            if (type != REG_DWORD) continue;
            char line[96]; snprintf(line, sizeof line, "%ls=%lu\r\n", name, (unsigned long)v); out += line;
        }
        RegCloseKey(k);
    }
    out += "StepsPerDay=" + std::to_string(GetCfg(L"StepsPerDay", 6500)) + "\r\n";
    for (auto &q : QuitList())
        out += "Quit:" + W2U(q.name) + "=" + std::to_string((long long)q.start) + ";" + std::to_string((long long)q.best) + "\r\n";
    for (auto &r : RemList()) {
        if (r.every) out += "RemR:" + W2U(r.id) + "=" + std::to_string((long long)r.at) + ";" + std::to_string(r.every) + ";" + std::to_string((long long)r.base) + ";" + W2U(r.text) + "\r\n";
        else out += "Rem:" + W2U(r.id) + "=" + std::to_string((long long)r.at) + ";" + W2U(r.text) + "\r\n";
    }
    if (!gamesLoaded) LoadGameLists();
    for (auto &kv : knownGames) out += "GameExe:" + W2U(kv.first) + "=" + W2U(kv.second) + "\r\n";
    for (auto &ig : ignoredExe) out += "GameIgnore:" + W2U(ig) + "=1\r\n";
    for (const wchar_t *key : { GT_KEY, GD_KEY }) {
        HKEY gk;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &gk) != ERROR_SUCCESS) continue;
        for (DWORD i = 0;; i++) {
            wchar_t name[300]; DWORD nl = 300, type; ULONGLONG v; DWORD vs = sizeof v;
            if (RegEnumValueW(gk, i, name, &nl, NULL, &type, (BYTE *)&v, &vs) != ERROR_SUCCESS) break;
            if (type == REG_QWORD) out += std::string(key == GT_KEY ? "GameTotal:" : "GameDay:") + W2U(name) + "=" + std::to_string((unsigned long long)v) + "\r\n";
        }
        RegCloseKey(gk);
    }
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wr = 0; bool ok = f != INVALID_HANDLE_VALUE && WriteFile(f, out.data(), (DWORD)out.size(), &wr, NULL) && wr == out.size();
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    MessageBoxW(h, ok ? L"Статистика сохранена в файл.\n\nСкопируйте его на флешку или в облако. После переустановки Windows "
                        L"запустите Напоминалку и выберите «Изменить статистику → Загрузить из файла…»."
                      : L"Не получилось сохранить файл.", L"Напоминалка", (ok ? MB_ICONINFORMATION : MB_ICONWARNING) | MB_TOPMOST);
}
static bool ImportStats(HWND h) {
    wchar_t path[MAX_PATH] = L"";
    if (!PickFile(h, false, path)) return false;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { MessageBoxW(h, L"Не получилось открыть файл.", L"Напоминалка", MB_ICONWARNING | MB_TOPMOST); return false; }
    std::string d(1 << 20, 0); DWORD n = 0; ReadFile(f, &d[0], (DWORD)d.size(), &n, NULL); CloseHandle(f); d.resize(n);
    if (d.compare(0, 17, "Napominalka stats")) { MessageBoxW(h, L"Это не файл статистики Напоминалки.", L"Напоминалка", MB_ICONWARNING | MB_TOPMOST); return false; }
    std::vector<std::pair<wstring, DWORD>> rows; long sum = 0, total = -1, steps = -1; size_t pos = 0;
    std::vector<QuitItem> quits; std::vector<std::pair<wstring, ULONGLONG>> gTot, gDay; std::vector<std::pair<wstring, wstring>> gExe; std::vector<wstring> gIgn; std::vector<Reminder> rems;
    while (pos < d.size()) {
        size_t e = d.find('\n', pos); if (e == std::string::npos) e = d.size();
        std::string line = d.substr(pos, e - pos); pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t eq = line.find('='); if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        if (!key.compare(0, 5, "RemR:")) {            // повторяющееся: время;дни;база;текст
            std::string v = line.substr(eq + 1); size_t a1 = v.find(';'), a2 = a1 == std::string::npos ? a1 : v.find(';', a1 + 1), a3 = a2 == std::string::npos ? a2 : v.find(';', a2 + 1);
            if (a3 != std::string::npos) {
                Reminder r; r.id = U2W(key.substr(5)); r.at = (time_t)atoll(v.c_str()); r.every = std::max(1, atoi(v.c_str() + a1 + 1));
                r.base = (time_t)atoll(v.c_str() + a2 + 1); r.text = U2W(v.substr(a3 + 1));
                if (r.at > 0 && !r.id.empty()) rems.push_back(r);
            }
            continue;
        }
        if (!key.compare(0, 4, "Rem:")) {
            size_t sc2 = line.find(';', eq); long long at = atoll(line.c_str() + eq + 1);
            if (sc2 != std::string::npos && at > 0) rems.push_back({ U2W(key.substr(4)), (time_t)at, U2W(line.substr(sc2 + 1)) });
            continue;
        }
        if (!key.compare(0, 8, "GameExe:")) { gExe.push_back({ U2W(key.substr(8)), U2W(line.substr(eq + 1)) }); continue; }
        if (!key.compare(0, 11, "GameIgnore:")) { gIgn.push_back(U2W(key.substr(11))); continue; }
        if (!key.compare(0, 10, "GameTotal:") || !key.compare(0, 8, "GameDay:")) {
            bool tot = key[4] == 'T'; wstring nm = U2W(key.substr(tot ? 10 : 8)); long long v = atoll(line.c_str() + eq + 1);
            if (!nm.empty() && v > 0) (tot ? gTot : gDay).push_back({ nm, (ULONGLONG)v });
            continue;
        }
        if (!key.compare(0, 5, "Quit:")) {          // отказ: начало;рекорд
            long long st = atoll(line.c_str() + eq + 1); size_t sc2 = line.find(';', eq);
            long long bs = sc2 == std::string::npos ? 0 : atoll(line.c_str() + sc2 + 1);
            wstring nm = U2W(key.substr(5)); if (!nm.empty() && st > 0) quits.push_back({ nm, (time_t)st, (time_t)(bs > 0 ? bs : 0) });
            continue;
        }
        long v = atol(line.c_str() + eq + 1); if (v < 0 || v > 100000) continue;
        if (key == "Total") total = v;
        else if (key == "StepsPerDay") steps = v;
        else if (key.size() == 10 && key[4] == '-' && key[7] == '-') { rows.push_back({ wstring(key.begin(), key.end()), (DWORD)v }); sum += v; }
    }
    wstring q = L"В файле: " + Days((int)rows.size()) + L" с записями, всего " + Times((int)(total >= 0 ? total : sum)) +
                (quits.empty() ? L"" : L", отказов: " + std::to_wstring(quits.size())) +
                (gTot.empty() ? L"" : L", игр: " + std::to_wstring(gTot.size())) +
                L".\n\nЗаменить текущую статистику данными из файла?";
    if (MessageBoxW(h, q.c_str(), L"Напоминалка", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) != IDYES) return false;
    StatResetAll();
    for (auto &r : rows) StatSet(r.first, r.second);
    StatSet(L"Total", (DWORD)(total >= 0 ? total : sum));
    if (steps > 0) SetCfg(L"StepsPerDay", (DWORD)steps);
    if (!gTot.empty() || !gDay.empty()) {           // время в играх из файла заменяет текущее
        RegDeleteKeyW(HKEY_CURRENT_USER, GT_KEY); RegDeleteKeyW(HKEY_CURRENT_USER, GD_KEY);
        for (auto &g : gTot) SetRegQ(GT_KEY, g.first, g.second);
        for (auto &g : gDay) SetRegQ(GD_KEY, g.first, g.second);
    }
    if (!rems.empty()) {                             // напоминания из файла заменяют текущие
        RegDeleteKeyW(HKEY_CURRENT_USER, REM_KEY); remFired.clear(); remQueue.clear();
        for (auto &r : rems) if (!r.id.empty()) RemSave(r.id, r.at, r.text, r.every, r.base);
    }
    if (!gExe.empty() || !gIgn.empty()) {
        RegDeleteKeyW(HKEY_CURRENT_USER, GK_KEY); RegDeleteKeyW(HKEY_CURRENT_USER, GI_KEY);
        for (auto &g : gExe) if (!g.first.empty() && !g.second.empty()) SetRegSz(GK_KEY, g.first, g.second);
        for (auto &g : gIgn) if (!g.empty()) SetRegSz(GI_KEY, g, L"1");
        LoadGameLists();
    }
    if (!quits.empty()) {                            // отказы из файла заменяют текущие
        RegDeleteKeyW(HKEY_CURRENT_USER, QUIT_KEY); RegDeleteKeyW(HKEY_CURRENT_USER, QBEST_KEY);
        for (auto &q : quits) { SetRegQ(QUIT_KEY, q.name, (ULONGLONG)q.start); if (q.best > 0) SetRegQ(QBEST_KEY, q.name, (ULONGLONG)q.best); }
    }
    return true;
}

// ---------- подключение Dota 2 ----------
static bool DirExists(const wstring &p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static wstring RegStr(HKEY root, const wchar_t *key, const wchar_t *name) {
    wchar_t b[1024]; DWORD sz = sizeof b;
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, NULL, b, &sz) != ERROR_SUCCESS) return L"";
    return b;
}
static void SetRegStr(const wchar_t *name, const wstring &v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CFG_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))); RegCloseKey(k);
    }
}
static const wchar_t *DOTA_SUB = L"\\steamapps\\common\\dota 2 beta\\game\\dota\\cfg";
static wstring FindDotaCfg() {
    wstring steam = RegStr(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    std::vector<wstring> libs; if (!steam.empty()) libs.push_back(steam);
    // остальные библиотеки Steam перечислены в libraryfolders.vdf
    HANDLE f = CreateFileW((steam + L"\\steamapps\\libraryfolders.vdf").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        std::string d(65536, 0); DWORD n = 0; ReadFile(f, &d[0], (DWORD)d.size(), &n, NULL); CloseHandle(f); d.resize(n);
        size_t pos = 0;
        while ((pos = d.find("\"path\"", pos)) != std::string::npos) {
            size_t q1 = d.find('"', pos + 6), q2 = q1 == std::string::npos ? q1 : d.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string p8 = d.substr(q1 + 1, q2 - q1 - 1), u; pos = q2;
            for (size_t i = 0; i < p8.size(); i++) { if (p8[i] == '\\' && i + 1 < p8.size() && p8[i + 1] == '\\') i++; u += p8[i]; }
            wchar_t w[1024]; int wn = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w, 1024);
            if (wn > 0) libs.push_back(w);
        }
    }
    for (auto &l : libs) { wstring p = l + DOTA_SUB; std::replace(p.begin(), p.end(), L'/', L'\\'); if (DirExists(p)) return p; }
    return L"";
}
static void ConnectDota(HWND h) {
    wstring cfg = FindDotaCfg();
    if (cfg.empty()) {
        MessageBoxW(h, L"Не нашёл папку Dota 2 автоматически.\n\nСейчас откроется окно — выберите папку «dota 2 beta» "
                       L"(обычно она лежит в Steam\\steamapps\\common).", L"Напоминалка", MB_ICONINFORMATION | MB_TOPMOST);
        BROWSEINFOW bi = {}; bi.hwndOwner = h; bi.lpszTitle = L"Выберите папку «dota 2 beta»"; bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST id = SHBrowseForFolderW(&bi); if (!id) return;
        wchar_t p[MAX_PATH]; SHGetPathFromIDListW(id, p); CoTaskMemFree(id);
        cfg = wstring(p) + L"\\game\\dota\\cfg";
        if (!DirExists(cfg)) { MessageBoxW(h, L"В этой папке нет Dota 2 (не найдена подпапка game\\dota\\cfg).", L"Напоминалка", MB_ICONWARNING | MB_TOPMOST); return; }
    }
    wstring dir = cfg + L"\\gamestate_integration"; CreateDirectoryW(dir.c_str(), NULL);
    wstring file = dir + L"\\gamestate_integration_napominalka.cfg";
    char text[512];
    int n = snprintf(text, sizeof text,
        "\"Napominalka\"\r\n{\r\n    \"uri\"       \"http://127.0.0.1:%d/\"\r\n    \"timeout\"   \"5.0\"\r\n    \"buffer\"    \"0.5\"\r\n"
        "    \"throttle\"  \"1.0\"\r\n    \"heartbeat\" \"30.0\"\r\n    \"data\"\r\n    {\r\n        \"map\" \"1\"\r\n    }\r\n}\r\n", GSI_PORT);
    HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { MessageBoxW(h, (L"Не получилось записать файл:\n" + file).c_str(), L"Напоминалка", MB_ICONWARNING | MB_TOPMOST); return; }
    DWORD wr; WriteFile(f, text, n, &wr, NULL); CloseHandle(f);
    SetRegStr(L"DotaCfgFile", file);
    MessageBoxW(h, L"Готово, файл настройки для Dota 2 создан.\n\n"
                   L"Остался один шаг (делается один раз):\n"
                   L"1. Откройте Steam → Библиотека.\n"
                   L"2. Правой кнопкой по Dota 2 → «Свойства».\n"
                   L"3. В поле «Параметры запуска» впишите:   -gamestateintegration\n"
                   L"4. Перезапустите Доту.\n\n"
                   L"После этого во время матча напоминалка будет молчать, а когда матч закончится — напомнит.",
                L"Напоминалка", MB_ICONINFORMATION | MB_TOPMOST);
}
static void DisconnectDota(HWND h) {
    wstring file = RegStr(HKEY_CURRENT_USER, CFG_KEY, L"DotaCfgFile");
    if (!file.empty()) DeleteFileW(file.c_str());
    SetRegStr(L"DotaCfgFile", L""); gsiLast = 0; gsiInMatch = false;
    MessageBoxW(h, L"Dota 2 отключена: файл настройки удалён.\nПараметр запуска -gamestateintegration можно убрать в Steam вручную.", L"Напоминалка", MB_ICONINFORMATION | MB_TOPMOST);
}

static void HubShow(); static void HubRefresh();
// ---------- полное меню (оно же — источник подменю для главного окна) ----------
static HMENU BuildFullMenu() {
    HMENU mn = CreatePopupMenu();
            wstring next = pendingKind >= 0 ? L"Напомню после матча / игры" : dueAction == ACT_NONE ? L"Напоминалка" : L"Следующее в " + Clock(dueAt);
            AppendMenuW(mn, MF_STRING | MF_GRAYED, 0, next.c_str());
            wstring todayS = L"Сегодня вставал: " + Times(StatGet(DayKey(time(NULL))));
            AppendMenuW(mn, MF_STRING | MF_GRAYED, 0, todayS.c_str());
            AppendMenuW(mn, MF_STRING | (cur && cur->kind != P_STATS && cur->kind != P_POSDEMO ? MF_GRAYED : 0), M_STATS, L"Статистика…");
            AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
            AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
            int lastT = (int)GetCfg(L"TimerMin", 4);
            if (timerEnd) {
                wstring tl = L"Таймер: осталось " + TimerLeft();
                AppendMenuW(mn, MF_STRING | MF_GRAYED, 0, tl.c_str());
                AppendMenuW(mn, MF_STRING, M_TIMER_STOP, L"Остановить таймер");
            } else {
                wstring tq = L"Запустить таймер на " + Min(lastT);
                AppendMenuW(mn, MF_STRING, M_TIMER_LAST, tq.c_str());
            }
            HMENU tm = CreatePopupMenu();
            for (int i = 0; i < TIMER_N; i++) {
                wstring t = TIMER_MINS[i] == 60 ? L"1 час" : std::to_wstring(TIMER_MINS[i]) + L" мин";
                AppendMenuW(tm, MF_STRING | (lastT == TIMER_MINS[i] ? MF_CHECKED : 0), M_TIMER0 + i, t.c_str());
            }
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)tm, L"Таймер на другое время");
            AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
            AppendMenuW(mn, MF_STRING, M_NOW, L"Напомнить сейчас");
            wstring already = L"Я уже постоял " + Min(STAND_MIN);
            AppendMenuW(mn, MF_STRING, M_ALREADY, already.c_str());
            HMENU dm = CreatePopupMenu();
            bool dotaOn = !RegStr(HKEY_CURRENT_USER, CFG_KEY, L"DotaCfgFile").empty();
            AppendMenuW(dm, MF_STRING | MF_GRAYED, 0, GsiActive() ? (gsiInMatch ? L"Сейчас: идёт матч" : L"Сейчас: Дота в меню")
                                                   : dotaOn ? L"Сигнала от Доты нет (игра закрыта?)" : L"Не подключена");
            AppendMenuW(dm, MF_SEPARATOR, 0, NULL);
            AppendMenuW(dm, MF_STRING, M_DOTA_ON, dotaOn ? L"Подключить заново…" : L"Подключить Dota 2…");
            if (dotaOn) AppendMenuW(dm, MF_STRING, M_DOTA_OFF, L"Отключить");
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)dm, L"Dota 2: напоминать между матчами");
            AppendMenuW(mn, MF_STRING | (CountAway() ? MF_CHECKED : 0), M_AWAY, L"Засчитывать, когда отошёл от компьютера");
            HMENU stm = CreatePopupMenu(); int curSt = (int)GetCfg(L"StepsPerDay", 6500);
            for (int i = 0; i < 6; i++) AppendMenuW(stm, MF_STRING | (curSt == STEPS[i] ? MF_CHECKED : 0), M_STEPS0 + i, Num(STEPS[i]).c_str());
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)stm, L"Сколько шагов в день я хожу");
            HMENU qm = CreatePopupMenu(); menuQuits = QuitList();
            for (size_t i = 0; i < menuQuits.size(); i++) {
                HMENU im = CreatePopupMenu();
                AppendMenuW(im, MF_STRING, M_Q_RELAPSE0 + i, L"Сорвался — начать отсчёт заново");
                AppendMenuW(im, MF_SEPARATOR, 0, NULL);
                AppendMenuW(im, MF_STRING, M_Q_PLUS0 + i, L"Я начал на день раньше (+1 день)");
                AppendMenuW(im, MF_STRING, M_Q_MINUS0 + i, L"Я начал на день позже (−1 день)");
                AppendMenuW(im, MF_SEPARATOR, 0, NULL);
                AppendMenuW(im, MF_STRING, M_Q_DEL0 + i, L"Удалить этот отказ");
                wstring t = menuQuits[i].name + L"  —  " + Span(time(NULL) - menuQuits[i].start);
                AppendMenuW(qm, MF_POPUP, (UINT_PTR)im, t.c_str());
            }
            if (!menuQuits.empty()) AppendMenuW(qm, MF_SEPARATOR, 0, NULL);
            HMENU am = CreatePopupMenu();
            for (int i = 0; i < Q_PRESET_N; i++) AppendMenuW(am, MF_STRING | (RegQ(QUIT_KEY, Q_PRESETS[i]) ? MF_GRAYED : 0), M_Q_PRESET0 + i, Q_PRESETS[i]);
            AppendMenuW(am, MF_SEPARATOR, 0, NULL);
            AppendMenuW(am, MF_STRING, M_Q_CUSTOM, L"Своё название…");
            AppendMenuW(qm, MF_POPUP | ((int)menuQuits.size() >= Q_MAX ? MF_GRAYED : 0), (UINT_PTR)am, L"Добавить");
            AppendMenuW(qm, MF_STRING | (cur && cur->kind != P_STATS && cur->kind != P_POSDEMO && cur->kind != P_QUITS ? MF_GRAYED : 0), M_Q_SHOW, L"Показать все…");
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)qm, L"Отказы от вредного");
            HMENU gm = CreatePopupMenu(); if (!gamesLoaded) LoadGameLists();
            {
                std::set<wstring> names; for (auto &kv : knownGames) names.insert(kv.second);
                menuGames.assign(names.begin(), names.end()); if (menuGames.size() > 40) menuGames.resize(40);
            }
            AppendMenuW(gm, MF_STRING | (cur && cur->kind != P_STATS && cur->kind != P_POSDEMO && cur->kind != P_QUITS && cur->kind != P_GAMES ? MF_GRAYED : 0), M_GAMES, L"Показать статистику…");
            AppendMenuW(gm, MF_SEPARATOR, 0, NULL);
            for (size_t i = 0; i < menuGames.size(); i++) {
                HMENU im = CreatePopupMenu();
                AppendMenuW(im, MF_STRING, M_G_REN0 + i, L"Переименовать…");
                AppendMenuW(im, MF_STRING, M_G_DEL0 + i, L"Это не игра — не считать и удалить время");
                AppendMenuW(gm, MF_POPUP, (UINT_PTR)im, menuGames[i].c_str());
            }
            if (!menuGames.empty()) AppendMenuW(gm, MF_SEPARATOR, 0, NULL);
            AppendMenuW(gm, MF_STRING, M_G_ADD, L"Добавить игру вручную (выбрать .exe)…");
            AppendMenuW(gm, MF_STRING, M_GAMES_RESET, L"Сбросить всё время в играх…");
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)gm, L"Время в играх");
            HMENU nm = CreatePopupMenu();
            {
                bool on = nightAppliedK && nightAppliedK < 6500;
                wstring st = on ? (nightOverride == 1 ? L"Сейчас: включён вручную" : L"Сейчас: включён") :
                             nightOverride == 2 ? L"Сейчас: выключен вручную" : L"Сейчас: выключен";
                AppendMenuW(nm, MF_STRING | MF_GRAYED, 0, st.c_str());
                AppendMenuW(nm, MF_SEPARATOR, 0, NULL);
                int ns = (int)GetCfg(L"NightStart", 0), ne = (int)GetCfg(L"NightEnd", 7 * 60);
                wstring sc = L"По расписанию: " + HHMM(ns) + L" – " + HHMM(ne);
                AppendMenuW(nm, MF_STRING | (GetCfg(L"NightOn", 1) ? MF_CHECKED : 0), M_N_SCHED, sc.c_str());
                if (on) AppendMenuW(nm, MF_STRING, M_N_OFF, L"Выключить сейчас (до утра / до вечера)");
                else AppendMenuW(nm, MF_STRING, M_N_ON, L"Включить сейчас");
                if (nightOverride) AppendMenuW(nm, MF_STRING, M_N_AUTO, L"Вернуть как по расписанию");
                AppendMenuW(nm, MF_SEPARATOR, 0, NULL);
                HMENU sm1 = CreatePopupMenu(), sm2 = CreatePopupMenu(), sm3 = CreatePopupMenu();
                for (int i = 0; i < 8; i++) AppendMenuW(sm1, MF_STRING | (ns == N_STARTS[i] ? MF_CHECKED : 0), M_N_START0 + i, HHMM(N_STARTS[i]).c_str());
                for (int i = 0; i < 7; i++) AppendMenuW(sm2, MF_STRING | (ne == N_ENDS[i] ? MF_CHECKED : 0), M_N_END0 + i, HHMM(N_ENDS[i]).c_str());
                int nk = (int)GetCfg(L"NightK", 3400);
                for (int i = 0; i < 4; i++) {
                    wstring t = wstring(N_KNAMES[i]) + L"  (" + std::to_wstring(N_KS[i]) + L" K)";
                    AppendMenuW(sm3, MF_STRING | (nk == N_KS[i] ? MF_CHECKED : 0), M_N_K0 + i, t.c_str());
                }
                AppendMenuW(nm, MF_POPUP, (UINT_PTR)sm1, L"Начало вечером");
                AppendMenuW(nm, MF_POPUP, (UINT_PTR)sm2, L"Конец утром");
                AppendMenuW(nm, MF_POPUP, (UINT_PTR)sm3, L"Насколько тёплый");
                AppendMenuW(nm, MF_STRING, M_N_PREVIEW, L"Показать, как будет (10 секунд)");
                if (nightLimited) {
                    AppendMenuW(nm, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(nm, MF_STRING, M_N_UNLOCK, L"Windows ограничивает силу — разрешить…");
                }
            }
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)nm, L"Ночной режим (тёплый экран)");
            HMENU rm = CreatePopupMenu(); menuRems = RemVisible();
            AppendMenuW(rm, MF_STRING, M_R_1H, L"Через 1 час…");
            AppendMenuW(rm, MF_STRING, M_R_1D, L"Через 1 день…");
            AppendMenuW(rm, MF_STRING, M_R_1M, L"Через 1 месяц…");
            AppendMenuW(rm, MF_STRING, M_R_PICK, L"Выбрать дату и время…");
            AppendMenuW(rm, MF_SEPARATOR, 0, NULL);
            if (RepeatOn()) {
                AppendMenuW(rm, MF_STRING, M_R_REP2, L"Повторять через день…");
                AppendMenuW(rm, MF_STRING, M_R_REP1, L"Повторять каждый день…");
                AppendMenuW(rm, MF_STRING, M_R_REP7, L"Повторять раз в неделю…");
                AppendMenuW(rm, MF_STRING, M_REP_TOGGLE, L"Выключить повторяющиеся");
            } else AppendMenuW(rm, MF_STRING, M_REP_TOGGLE, L"Включить повторяющиеся напоминания");
            if (!menuRems.empty()) {
                AppendMenuW(rm, MF_SEPARATOR, 0, NULL);
                AppendMenuW(rm, MF_STRING | MF_GRAYED, 0, L"Запланировано (нажмите, чтобы удалить):");
                for (size_t i = 0; i < menuRems.size(); i++) {
                    wstring t = WhenStr(menuRems[i].at) + L"  —  " + (menuRems[i].text.size() > 40 ? menuRems[i].text.substr(0, 40) + L"…" : menuRems[i].text);
                    if (menuRems[i].every) t += L"  (" + EveryStr(menuRems[i].every) + L")";
                    AppendMenuW(rm, MF_STRING, M_R_DEL0 + i, t.c_str());
                }
            }
            wstring rl = menuRems.empty() ? wstring(L"Мои напоминания") : L"Мои напоминания (" + std::to_wstring(menuRems.size()) + L")";
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)rm, rl.c_str());
            HMENU bm = CreatePopupMenu();
            AppendMenuW(bm, MF_STRING, M_B_SHOW, L"Включить чёрный экран");
            AppendMenuW(bm, MF_STRING | (GetCfg(L"BlackOnStart", 1) ? MF_CHECKED : 0), M_B_START, L"Иконка на панели задач при запуске");
            HMENU im2 = CreatePopupMenu(); int curIdle = (int)GetCfg(L"AutoBlackMin", 1);
            for (int i = 0; i < 7; i++) {
                wstring t = B_IDLE[i] == 0 ? wstring(L"Не включать сам") : L"Через " + std::to_wstring(B_IDLE[i]) + L" мин без мыши и клавиатуры";
                AppendMenuW(im2, MF_STRING | (curIdle == B_IDLE[i] ? MF_CHECKED : 0), M_B_IDLE0 + i, t.c_str());
            }
            AppendMenuW(bm, MF_POPUP, (UINT_PTR)im2, L"Включать сам");
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)bm, L"Чёрный экран (OLED)");
            HMENU sm = CreatePopupMenu();
            AppendMenuW(sm, MF_STRING, M_ST_MINUS, L"Убрать 1 за сегодня");
            AppendMenuW(sm, MF_STRING, M_ST_PLUS, L"Добавить 1 за сегодня");
            AppendMenuW(sm, MF_STRING, M_ST_ZERO, L"Обнулить сегодняшний день");
            AppendMenuW(sm, MF_SEPARATOR, 0, NULL);
            AppendMenuW(sm, MF_STRING, M_EXPORT, L"Сохранить в файл (для переноса)…");
            AppendMenuW(sm, MF_STRING, M_IMPORT, L"Загрузить из файла…");
            AppendMenuW(sm, MF_SEPARATOR, 0, NULL);
            AppendMenuW(sm, MF_STRING, M_ST_RESET, L"Сбросить всю статистику…");
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)sm, L"Изменить статистику");
            AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
            AppendMenuW(mn, MF_STRING | (AutoStartOn() ? MF_CHECKED : 0), M_AUTO, L"Запускать вместе с Windows");
            AppendMenuW(mn, MF_STRING | (VoiceOn() ? MF_CHECKED : 0), M_VOICE, L"Озвучивать напоминание голосом");
            HMENU pm = CreatePopupMenu(); int curPos = (int)GetCfg(L"Position", POS_BR);
            for (int i = 0; i < 5; i++) AppendMenuW(pm, MF_STRING | (curPos == i ? MF_CHECKED : 0), M_POS0 + i, POS_NAMES[i]);
            AppendMenuW(pm, MF_STRING | MF_GRAYED | (curPos == POS_CUSTOM ? MF_CHECKED : 0), 0, L"Своё место (перетащите окно мышкой)");
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)pm, L"Где показывать окно");
            HMENU vm = CreatePopupMenu(); int curV = (int)GetCfg(L"VoiceIdx", 0);
            for (int i = 0; i < VOICE_COUNT; i++) AppendMenuW(vm, MF_STRING | (curV == i ? MF_CHECKED : 0), M_VOICE0 + i, VOICE_NAMES[i]);
            AppendMenuW(vm, MF_SEPARATOR, 0, NULL);
            AppendMenuW(vm, MF_STRING, M_TEST, L"Прослушать");
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)vm, L"Голос");
            HMENU lm = CreatePopupMenu(); int curVol = (int)GetCfg(L"Volume", 50);
            for (int i = 0; i < 4; i++) {
                wstring t = std::to_wstring(VOLUMES[i]) + L"%";
                AppendMenuW(lm, MF_STRING | (curVol == VOLUMES[i] ? MF_CHECKED : 0), M_VOL0 + i, t.c_str());
            }
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)lm, L"Громкость голоса");
            AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
            if (!updVersion.empty()) {
                wstring ut = L"Обновить до версии " + updVersion;
                AppendMenuW(mn, MF_STRING, M_UPD_NOW, ut.c_str());
            } else AppendMenuW(mn, MF_STRING | (updBusy ? MF_GRAYED : 0), M_UPD_CHECK, L"Проверить обновления");
            AppendMenuW(mn, MF_STRING, M_LOG, L"Журнал событий…");
            AppendMenuW(mn, MF_STRING | MF_GRAYED, 0, L"Версия " APP_VERSION L"  ·  создана " APP_CREATED);
            AppendMenuW(mn, MF_STRING, M_EXIT, L"Выход");
    return mn;
}
static void HandleCmd(HWND h, int c) {
            if (c == M_NOW) { dueAction = ACT_NONE; pendingKind = -1; ShowPopup(P_REMIND); UpdateTip(); }
            else if (c == M_AUTO) SetAutoStart(!AutoStartOn());
            else if (c == M_VOICE) SetVoice(!VoiceOn());
            else if (c >= M_VOICE0 && c < M_VOICE0 + VOICE_COUNT) { SetCfg(L"VoiceIdx", c - M_VOICE0); Speak(0); }
            else if (c >= M_VOL0 && c < M_VOL0 + 4) { SetCfg(L"Volume", VOLUMES[c - M_VOL0]); Speak(0); }
            else if (c == M_TEST) Speak(0);
            else if (c == M_STATS) ShowPopup(P_STATS);
            else if (c == M_TIMER_LAST) StartTimer((int)GetCfg(L"TimerMin", 4));
            else if (c == M_TIMER_STOP) { timerEnd = 0; timerPending = false; UpdateTip(); }
            else if (c >= M_TIMER0 && c < M_TIMER0 + TIMER_N) { SetCfg(L"TimerMin", TIMER_MINS[c - M_TIMER0]); StartTimer(TIMER_MINS[c - M_TIMER0]); }
            else if (c >= M_STEPS0 && c < M_STEPS0 + 6) SetCfg(L"StepsPerDay", STEPS[c - M_STEPS0]);
            else if (c >= M_Q_SHOW && c < M_Q_DEL0 + Q_MAX) {
                bool canShow = !cur || cur->kind == P_STATS || cur->kind == P_POSDEMO || cur->kind == P_QUITS, changed = true;
                if (c == M_Q_SHOW) {}
                else if (c == M_Q_CUSTOM) { wstring nm; if (AskText(h, L"Новый отказ", L"От чего отказываетесь?", nm)) QuitAdd(nm); else changed = false; }
                else if (c >= M_Q_PRESET0 && c < M_Q_PRESET0 + Q_PRESET_N) QuitAdd(Q_PRESETS[c - M_Q_PRESET0]);
                else {
                    int idx = (c - M_Q_RELAPSE0) % 30, op = (c - M_Q_RELAPSE0) / 30;
                    if (c < M_Q_RELAPSE0 || idx >= (int)menuQuits.size()) return;
                    const QuitItem &q = menuQuits[idx];
                    if (op == 0) {
                        wstring t = L"Начать отсчёт «" + q.name + L"» заново?\n\nСейчас: " + Span(time(NULL) - q.start) + L". Этот результат сохранится как рекорд.";
                        if (MessageBoxW(h, t.c_str(), L"Напоминалка", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_TOPMOST) == IDYES) QuitRelapse(q); else changed = false;
                    } else if (op == 1) QuitShift(q, 1);
                    else if (op == 2) QuitShift(q, -1);
                    else {
                        wstring t = L"Удалить отказ «" + q.name + L"» вместе с рекордом?";
                        if (MessageBoxW(h, t.c_str(), L"Напоминалка", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TOPMOST) == IDYES) QuitDelete(q); else changed = false;
                    }
                }
                if (changed && canShow) ShowPopup(P_QUITS);
            }
            else if (c == M_B_SHOW) BlackCreate(true);
            else if (c >= M_B_IDLE0 && c < M_B_IDLE0 + 7) SetCfg(L"AutoBlackMin", B_IDLE[c - M_B_IDLE0]);
            else if (c == M_B_START) {
                bool on = !GetCfg(L"BlackOnStart", 1); SetCfg(L"BlackOnStart", on);
                if (on) BlackCreate(false); else if (hBlack) DestroyWindow(hBlack);
            }
            else if (c == M_REP_TOGGLE) { bool on = !RepeatOn(); SetCfg(L"RepeatOn", on); Log(on ? L"Повторяющиеся напоминания включены" : L"Повторяющиеся напоминания выключены"); }
            else if (c == M_R_REP1 || c == M_R_REP2 || c == M_R_REP7) { if (!RepeatOn()) SetCfg(L"RepeatOn", 1); AskRepeat(h, c == M_R_REP1 ? 1 : c == M_R_REP2 ? 2 : 7); }
            else if (c >= M_R_SHIFT0 && c < M_R_SHIFT0 + 40) { size_t i = c - M_R_SHIFT0; if (i < menuRems.size() && menuRems[i].every) RemShift(menuRems[i], 1); }
            else if (c >= M_R_1H && c < M_R_DEL0 + 40) {
                time_t now = time(NULL);
                if (c == M_R_1H) AskReminder(h, now + 3600, false);
                else if (c == M_R_1D) AskReminder(h, now + 86400, false);
                else if (c == M_R_1M) AskReminder(h, AddMonths(now, 1), false);
                else if (c == M_R_PICK) AskReminder(h, 0, true);
                else {
                    size_t i = c - M_R_DEL0; if (i >= menuRems.size()) return;
                    wstring t = L"Удалить напоминание?\n\n" + WhenStr(menuRems[i].at) + L"  —  " + menuRems[i].text +
                                (menuRems[i].every ? L"\n\nОно повторяется " + EveryStr(menuRems[i].every) + L" — будут удалены все повторы." : L"");
                    if (MessageBoxW(h, t.c_str(), L"Напоминалка", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_TOPMOST) == IDYES) RemDelete(menuRems[i].id);
                }
            }
            else if (c >= M_N_SCHED && c < M_N_K0 + 4) {
                if (c == M_N_SCHED) SetCfg(L"NightOn", !GetCfg(L"NightOn", 1));
                else if (c == M_N_ON || c == M_N_OFF) { nightOverride = c == M_N_ON ? 1 : 2; overrideSchedState = NightScheduled(); }
                else if (c == M_N_AUTO) nightOverride = 0;
                else if (c >= M_N_START0 && c < M_N_START0 + 8) SetCfg(L"NightStart", N_STARTS[c - M_N_START0]);
                else if (c >= M_N_END0 && c < M_N_END0 + 7) SetCfg(L"NightEnd", N_ENDS[c - M_N_END0]);
                else if (c >= M_N_K0) { SetCfg(L"NightK", N_KS[c - M_N_K0]); nightMaxOkK = 0; nightLimited = false; previewUntil = GetTickCount() + 10000; SetTimer(h, T_NIGHT_PREVIEW, 10000, NULL); }
                else if (c == M_N_PREVIEW) { previewUntil = GetTickCount() + 10000; SetTimer(h, T_NIGHT_PREVIEW, 10000, NULL); }
                else if (c == M_N_UNLOCK) {
                    if (MessageBoxW(h, L"Windows по умолчанию не даёт программам делать экран слишком тёплым.\n\n"
                                       L"Можно снять это ограничение (так же делает f.lux). Windows спросит права администратора, "
                                       L"а заработает после перезагрузки компьютера.\n\nСнять ограничение?",
                                    L"Напоминалка", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) == IDYES) {
                        HINSTANCE r = ShellExecuteW(h, L"runas", L"reg.exe",
                            L"add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM\" /v GdiICMGammaRange /t REG_DWORD /d 256 /f", NULL, SW_HIDE);
                        if ((INT_PTR)r > 32) MessageBoxW(h, L"Готово. Перезагрузите компьютер, и полная сила ночного режима заработает.", L"Напоминалка", MB_ICONINFORMATION | MB_TOPMOST);
                    }
                    return;
                }
                NightUpdate(true);
            }
            else if (c == M_GAMES) ShowPopup(P_GAMES);
            else if (c == M_G_ADD) {
                wchar_t path[MAX_PATH] = L"";
                OPENFILENAMEW o = {}; o.lStructSize = sizeof o; o.hwndOwner = h;
                o.lpstrFilter = L"Игра (*.exe)\0*.exe\0"; o.lpstrFile = path; o.nMaxFile = MAX_PATH;
                o.lpstrTitle = L"Выберите .exe игры"; o.Flags = OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;
                if (GetOpenFileNameW(&o)) {
                    AddGameExe(path);
                    wstring t = L"Игра добавлена: " + knownGames[Lower(path)] + L"\n\nТеперь её время будет считаться, пока она запущена. Название можно поменять в меню.";
                    MessageBoxW(h, t.c_str(), L"Напоминалка", MB_ICONINFORMATION | MB_TOPMOST);
                }
            }
            else if (c >= M_G_REN0 && c < M_G_DEL0 + 40) {
                bool del = c >= M_G_DEL0; size_t i = c - (del ? M_G_DEL0 : M_G_REN0);
                if (i >= menuGames.size()) return;
                wstring g = menuGames[i];
                if (del) {
                    wstring t = L"«" + g + L"» — не игра?\n\nПрограмма перестанет её считать и удалит уже набранное время (" + HM(RegQ(GT_KEY, g)) + L").";
                    if (MessageBoxW(h, t.c_str(), L"Напоминалка", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_TOPMOST) == IDYES) IgnoreGame(g);
                } else {
                    wstring nm; if (AskText(h, L"Название игры", (L"Новое название для «" + g + L"»:").c_str(), nm)) RenameGame(g, nm);
                }
                if (!cur || cur->kind == P_GAMES) ShowPopup(P_GAMES);
            }
            else if (c == M_GAMES_RESET) {
                if (MessageBoxW(h, L"Удалить всё накопленное время в играх?", L"Напоминалка", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TOPMOST) == IDYES) {
                    RegDeleteKeyW(HKEY_CURRENT_USER, GT_KEY); RegDeleteKeyW(HKEY_CURRENT_USER, GD_KEY);
                    if (!cur || cur->kind == P_GAMES) ShowPopup(P_GAMES);
                }
            }
            else if (c == M_EXPORT) ExportStats(h);
            else if (c == M_IMPORT) { if (ImportStats(h) && (!cur || cur->kind == P_STATS || cur->kind == P_POSDEMO)) ShowPopup(P_STATS); }
            else if (c == M_DOTA_ON) ConnectDota(h);
            else if (c == M_DOTA_OFF) DisconnectDota(h);
            else if (c == M_AWAY) SetCfg(L"CountAway", !CountAway());
            else if (c == M_ALREADY) {   // встал сам, без напоминания: засчитать и начать час заново
                StatAddStandUp(); sittingSince = time(NULL); pendingKind = -1;
                Schedule(WORK_MIN, ACT_REMIND); ShowPopup(P_DONE);
            }
            else if (c >= M_ST_MINUS && c <= M_ST_RESET) {
                if (c == M_ST_MINUS) StatAdjustToday(-1);
                else if (c == M_ST_PLUS) StatAdjustToday(1);
                else if (c == M_ST_ZERO) StatAdjustToday(-1000000);
                else if (MessageBoxW(h, L"Удалить всю статистику за всё время? Вернуть её будет нельзя.", L"Напоминалка",
                                     MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TOPMOST) == IDYES) StatResetAll();
                if (!cur || cur->kind == P_STATS || cur->kind == P_POSDEMO) ShowPopup(P_STATS);   // показать, что получилось
            }
            else if (c >= M_POS0 && c < M_POS0 + 5) {
                SetCfg(L"Position", c - M_POS0);
                if (cur && !cur->dragging) { Layout(cur); cur->t = 1; Render(cur); }   // переносим уже открытое окно
                else if (!cur) ShowPopup(P_POSDEMO);
            }
            else if (c == M_UPD_CHECK) UpdCheck(true);
            else if (c == M_UPD_NOW) UpdStartDownload();
            else if (c == M_LOG) { Log(L"Открыт журнал"); ShellExecuteW(h, L"open", LogPath().c_str(), NULL, NULL, SW_SHOWNORMAL); }
            else if (c == M_EXIT) { Log(L"Программа закрыта через «Выход»"); DestroyWindow(h); }
}

// ======================= главное окно (красивое меню) =======================
// Тёмное окно с разделами слева. Все действия — те же команды, что и в меню значка (HandleCmd).
enum { A_NAV = 1, A_CMD, A_MENU, A_ROW };
struct Hit { RectF r; int act, arg; };
static HWND hHub = NULL;
static int hubTab = 0, hubHoverAct = 0, hubHoverArg = 0, hubPressAct = 0, hubPressArg = 0;
static float hubScroll[6] = {}, hubContentH = 0, hubViewH = 0;
static std::vector<Hit> hubHits;
static const Color H_BG(255, 0x12, 0x15, 0x1C), H_SIDE(255, 0x0D, 0x10, 0x16), H_CARD(255, 0x1A, 0x1F, 0x2A),
                   H_TEXT(255, 0xF3, 0xF5, 0xF9), H_SUB(255, 0x9A, 0xA3, 0xB5), H_DIM(255, 0x6B, 0x72, 0x80),
                   H_G1(255, 0x3D, 0xDC, 0x97), H_G2(255, 0x14, 0x9E, 0xA8);
enum Glyph2 { G_CHART = 10, G_BELL, G_PAD, G_GEAR, G_NO, G_HOME };
static const wchar_t *HUB_TABS[] = { L"Главная", L"Статистика", L"Отказы", L"Напоминания", L"Игры", L"Настройки" };
static const int HUB_GLYPH[] = { G_HOME, G_CHART, G_NO, G_BELL, G_PAD, G_GEAR };
// подменю из полного меню, по названиям пунктов
static const wchar_t *HUB_MENU_PATH[][2] = {
    { L"Ночной режим (тёплый экран)", L"Начало вечером" }, { L"Ночной режим (тёплый экран)", L"Конец утром" },
    { L"Ночной режим (тёплый экран)", L"Насколько тёплый" }, { L"Чёрный экран (OLED)", L"Включать сам" },
    { L"Голос", NULL }, { L"Громкость голоса", NULL }, { L"Где показывать окно", NULL }, { L"Сколько шагов в день я хожу", NULL },
    { L"Dota 2: напоминать между матчами", NULL }, { L"Отказы от вредного", L"Добавить" }, { L"Таймер на другое время", NULL },
    { L"Изменить статистику", NULL } };
enum { HM_NSTART, HM_NEND, HM_NK, HM_BLACK, HM_VOICE, HM_VOL, HM_POS, HM_STEPS, HM_DOTA, HM_QADD, HM_TIMER, HM_STATS };

static HMENU FindSub(HMENU m, const wchar_t *label) {
    if (!m || !label) return m;
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        HMENU sub = GetSubMenu(m, i); if (!sub) continue;
        wchar_t t[128]; GetMenuStringW(m, i, t, 128, MF_BYPOSITION);
        if (!wcscmp(t, label)) return sub;
    }
    return NULL;
}
static float MeasureW(const wstring &s, const wchar_t *fam, float px) {
    Bitmap bm(1, 1); Graphics g(&bm); g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    FontFamily ff0(fam); const FontFamily *ff = ff0.IsAvailable() ? &ff0 : FontFamily::GenericSansSerif();
    Font f(ff, px, FontStyleRegular, UnitPixel); RectF out;
    StringFormat sf(StringFormat::GenericTypographic()); sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
    g.MeasureString(s.c_str(), -1, &f, PointF(0, 0), &sf, &out);
    return out.Width;
}
static void HubIcon(Graphics &g, RectF r, int glyph, Color c) {      // маленькие значки разделов (линиями)
    float k = r.Width / 24, ox = r.X, oy = r.Y;
    Pen p(c, 1.9f * k); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound); p.SetLineJoin(LineJoinRound);
    SolidBrush b(c);
    auto P = [&](float x, float y) { return PointF(ox + x * k, oy + y * k); };
    switch (glyph) {
    case G_HOME: { PointF pts[5] = { P(4, 11), P(12, 4), P(20, 11), P(20, 20), P(4, 20) }; g.DrawPolygon(&p, pts, 5); g.DrawLine(&p, P(10, 20), P(10, 14)); g.DrawLine(&p, P(10, 14), P(14, 14)); g.DrawLine(&p, P(14, 14), P(14, 20)); break; }
    case G_CHART: g.DrawLine(&p, P(6, 19), P(6, 12)); g.DrawLine(&p, P(12, 19), P(12, 6)); g.DrawLine(&p, P(18, 19), P(18, 10)); break;
    case G_NO: g.DrawEllipse(&p, ox + 4 * k, oy + 4 * k, 16 * k, 16 * k); g.DrawLine(&p, P(7, 7), P(17, 17)); break;
    case G_BELL: {
        GraphicsPath bp; bp.AddArc(ox + 6 * k, oy + 4 * k, 12 * k, 12 * k, 180, 180);
        bp.AddLine(P(18, 10), P(19, 17)); bp.AddLine(P(19, 17), P(5, 17)); bp.AddLine(P(5, 17), P(6, 10));
        g.DrawPath(&p, &bp); g.DrawLine(&p, P(10, 20), P(14, 20)); break; }
    case G_PAD: {
        GraphicsPath pp; RR(pp, RectF(ox + 3 * k, oy + 7 * k, 18 * k, 11 * k), 5 * k); g.DrawPath(&p, &pp);
        g.DrawLine(&p, P(8, 10.5f), P(8, 14.5f)); g.DrawLine(&p, P(6, 12.5f), P(10, 12.5f));
        g.FillEllipse(&b, ox + 14.6f * k, oy + 10 * k, 2.2f * k, 2.2f * k); g.FillEllipse(&b, ox + 16.8f * k, oy + 12.6f * k, 2.2f * k, 2.2f * k); break; }
    case G_GEAR: {
        g.DrawEllipse(&p, ox + 8.5f * k, oy + 8.5f * k, 7 * k, 7 * k);
        for (int i = 0; i < 8; i++) { float a = i * 3.14159f / 4; g.DrawLine(&p, P(12 + cosf(a) * 6.5f, 12 + sinf(a) * 6.5f), P(12 + cosf(a) * 9, 12 + sinf(a) * 9)); }
        break; }
    }
}
// ---- элементы интерфейса; координаты y — внутри прокручиваемой области ----
struct HubCtx { Graphics *g; RectF view; float scroll; };
static RectF HR(const HubCtx &c, RectF r) { return RectF(r.X, r.Y - c.scroll, r.Width, r.Height); }   // с учётом прокрутки
static void AddHit(const HubCtx &c, RectF r, int act, int arg) {
    RectF v = HR(c, r); RectF in; if (!RectF::Intersect(in, v, c.view)) return;
    hubHits.push_back({ in, act, arg });
}
static bool Hov(int act, int arg) { return hubHoverAct == act && hubHoverArg == arg; }
static void HCard(const HubCtx &c, RectF r) {
    RectF v = HR(c, r); GraphicsPath p; RR(p, v, S(14));
    SolidBrush b(H_CARD); c.g->FillPath(&b, &p);
    Pen pen(Color(16, 255, 255, 255), 1.0f); c.g->DrawPath(&pen, &p);
}
static float HButton(const HubCtx &c, float x, float y, const wstring &label, int act, int arg, bool primary = false, float h = 0, float w = 0,
                     Color a1 = Color(0, 0, 0, 0), Color a2 = Color(0, 0, 0, 0)) {
    if (!h) h = S(36);
    if (!w) w = MeasureW(label, semiFont.c_str(), S(13)) + S(30);
    RectF r(x, y, w, h), v = HR(c, r);
    bool hv = Hov(act, arg); GraphicsPath p; RR(p, v, S(10));
    if (primary) {
        Color c1 = a1.GetA() ? a1 : H_G1, c2 = a2.GetA() ? a2 : H_G2;
        if (hv) { c1 = Mix(c1, Color(255, 255, 255, 255), 0.12f); c2 = Mix(c2, Color(255, 255, 255, 255), 0.12f); }
        LinearGradientBrush gb(PointF(v.X, v.Y), PointF(v.X + v.Width, v.Y + v.Height), c1, c2); c.g->FillPath(&gb, &p);
    } else {
        SolidBrush b(Color(hv ? 34 : 18, 255, 255, 255)); c.g->FillPath(&b, &p);
        Pen pen(Color(hv ? 40 : 24, 255, 255, 255), 1.0f); c.g->DrawPath(&pen, &p);
    }
    Text(*c.g, label, semiFont.c_str(), S(13), FontStyleRegular, H_TEXT, v, StringAlignmentCenter, StringAlignmentCenter, true);
    AddHit(c, r, act, arg);
    return w;
}
struct Flow { float x0, x, y, maxX; };              // ряд кнопок с переносом на новую строку
static void FBtn(const HubCtx &c, Flow &f, const wstring &label, int act, int arg, bool primary = false,
                 Color a1 = Color(0, 0, 0, 0), Color a2 = Color(0, 0, 0, 0)) {
    float w = MeasureW(label, semiFont.c_str(), S(13)) + S(30);
    if (f.x > f.x0 && f.x + w > f.maxX) { f.x = f.x0; f.y += S(36) + S(10); }
    HButton(c, f.x, f.y, label, act, arg, primary, 0, w, a1, a2);
    f.x += w + S(10);
}
static void HDropdown(const HubCtx &c, RectF r, const wstring &value, int menuIdx) {
    RectF v = HR(c, r); bool hv = Hov(A_MENU, menuIdx); GraphicsPath p; RR(p, v, S(10));
    SolidBrush b(Color(hv ? 34 : 18, 255, 255, 255)); c.g->FillPath(&b, &p);
    Pen pen(Color(hv ? 40 : 24, 255, 255, 255), 1.0f); c.g->DrawPath(&pen, &p);
    Text(*c.g, value, L"Segoe UI", S(13), FontStyleRegular, H_TEXT, RectF(v.X + S(12), v.Y, v.Width - S(36), v.Height), StringAlignmentNear, StringAlignmentCenter, true);
    Pen ch(H_SUB, S(1.6f)); ch.SetStartCap(LineCapRound); ch.SetEndCap(LineCapRound);
    float cx = v.X + v.Width - S(18), cy = v.Y + v.Height / 2;
    c.g->DrawLine(&ch, cx - S(4), cy - S(2), cx, cy + S(2)); c.g->DrawLine(&ch, cx, cy + S(2), cx + S(4), cy - S(2));
    AddHit(c, r, A_MENU, menuIdx);
}
static void HToggle(const HubCtx &c, RectF r, bool on, int cmd) {     // переключатель
    RectF v = HR(c, r); GraphicsPath p; RR(p, v, v.Height / 2 - 0.1f);
    if (on) { LinearGradientBrush gb(PointF(v.X, 0), PointF(v.X + v.Width, 0), H_G1, H_G2); c.g->FillPath(&gb, &p); }
    else { SolidBrush b(Color(Hov(A_CMD, cmd) ? 60 : 40, 255, 255, 255)); c.g->FillPath(&b, &p); }
    float d = v.Height - S(6), x = on ? v.X + v.Width - d - S(3) : v.X + S(3);
    SolidBrush kb(Color(255, 255, 255, 255)); c.g->FillEllipse(&kb, x, v.Y + S(3), d, d);
    AddHit(c, r, A_CMD, cmd);
}
static void HStat(const HubCtx &c, RectF r, const wstring &title, const wstring &value, const wstring &sub, Color accent) {
    HCard(c, r); RectF v = HR(c, r);
    Text(*c.g, title, L"Segoe UI", S(12), FontStyleRegular, H_SUB, RectF(v.X + S(16), v.Y + S(14), v.Width - S(32), S(18)), StringAlignmentNear, StringAlignmentNear, true);
    float px = S(26); while (px > S(15) && MeasureW(value, semiFont.c_str(), px) > v.Width - S(32)) px -= S(2);
    Text(*c.g, value, semiFont.c_str(), px, FontStyleRegular, accent, RectF(v.X + S(16), v.Y + S(34) + (S(26) - px) / 2, v.Width - S(32), S(36)), StringAlignmentNear, StringAlignmentNear, true);
    if (!sub.empty()) Text(*c.g, sub, L"Segoe UI", S(12), FontStyleRegular, H_DIM, RectF(v.X + S(16), v.Y + S(72), v.Width - S(32), S(18)), StringAlignmentNear, StringAlignmentNear, true);
}
static void HBars(const HubCtx &c, RectF r, const int *val, int n, bool minutes, Color c1, Color c2) {
    static const wchar_t *WD[] = { L"Вс", L"Пн", L"Вт", L"Ср", L"Чт", L"Пт", L"Сб" };
    RectF v = HR(c, r); time_t now = time(NULL); int mx = 1;
    for (int i = 0; i < n; i++) mx = std::max(mx, val[i]);
    float slot = v.Width / n, top = S(18), bot = S(20), maxH = v.Height - top - bot, bw = std::min(slot * 0.56f, S(34));
    SolidBrush line(Color(22, 255, 255, 255)); c.g->FillRectangle(&line, RectF(v.X, v.Y + top + maxH, v.Width, 1));
    for (int i = 0; i < n; i++) {
        time_t d = now - (time_t)(n - 1 - i) * 86400; struct tm *tmv = localtime(&d); int wd = tmv->tm_wday, md = tmv->tm_mday;
        bool today = i == n - 1;
        float bh = val[i] ? std::max(S(5), maxH * val[i] / mx) : S(3);
        RectF br(v.X + slot * i + (slot - bw) / 2, v.Y + top + maxH - bh, bw, bh);
        GraphicsPath bp; RR(bp, br, std::min(S(5), std::min(bh, bw) / 2 - 0.1f));
        if (!val[i]) { SolidBrush zb(Color(34, 255, 255, 255)); c.g->FillPath(&zb, &bp); }
        else if (today) { LinearGradientBrush tb(PointF(0, br.Y - 1), PointF(0, br.Y + br.Height + 1), c1, c2); c.g->FillPath(&tb, &bp); }
        else { SolidBrush ob(A(c1, 110)); c.g->FillPath(&ob, &bp); }
        if (val[i]) {
            wstring t = minutes ? (val[i] >= 60 ? std::to_wstring(val[i] / 60) + (val[i] % 60 >= 30 ? L",5" : L"") + L" ч" : std::to_wstring(val[i]) + L" м") : std::to_wstring(val[i]);
            Text(*c.g, t, semiFont.c_str(), S(11), FontStyleRegular, today ? H_TEXT : Color(255, 0xC5, 0xCC, 0xDA),
                 RectF(v.X + slot * i - S(6), br.Y - S(17), slot + S(12), S(15)), StringAlignmentCenter, StringAlignmentFar, true);
        }
        wstring lab = n <= 7 ? wstring(WD[wd]) : std::to_wstring(md);
        Text(*c.g, lab, today ? semiFont.c_str() : L"Segoe UI", S(11), FontStyleRegular, today ? H_TEXT : H_DIM,
             RectF(v.X + slot * i, v.Y + top + maxH + S(4), slot, S(15)), StringAlignmentCenter, StringAlignmentNear, true);
    }
}
static void HTitle(const HubCtx &c, float x, float y, float w, const wstring &title, const wstring &sub) {
    RectF v = HR(c, RectF(x, y, w, S(34)));
    Text(*c.g, title, semiFont.c_str(), S(24), FontStyleRegular, H_TEXT, v, StringAlignmentNear, StringAlignmentNear, true);
    if (!sub.empty()) Text(*c.g, sub, L"Segoe UI", S(13), FontStyleRegular, H_SUB, HR(c, RectF(x, y + S(36), w, S(20))), StringAlignmentNear, StringAlignmentNear, true);
}
static void HSection(const HubCtx &c, float x, float y, float w, const wstring &t) {
    Text(*c.g, t, semiFont.c_str(), S(15), FontStyleRegular, H_TEXT, HR(c, RectF(x, y, w, S(22))), StringAlignmentNear, StringAlignmentNear, true);
}
static void HLabel(const HubCtx &c, RectF r, const wstring &t, float px, Color col, bool semi = false, StringAlignment ha = StringAlignmentNear) {
    Text(*c.g, t, semi ? semiFont.c_str() : L"Segoe UI", px, FontStyleRegular, col, HR(c, r), ha, StringAlignmentCenter, true);
}

// ---- разделы ----
static float HubHome(const HubCtx &c, float x, float y, float w) {
    time_t now = time(NULL); struct tm *m = localtime(&now);
    static const wchar_t *WDF[] = { L"воскресенье", L"понедельник", L"вторник", L"среда", L"четверг", L"пятница", L"суббота" };
    static const wchar_t *MON[] = { L"января", L"февраля", L"марта", L"апреля", L"мая", L"июня", L"июля", L"августа", L"сентября", L"октября", L"ноября", L"декабря" };
    const wchar_t *hello = m->tm_hour < 5 ? L"Доброй ночи" : m->tm_hour < 12 ? L"Доброе утро" : m->tm_hour < 18 ? L"Добрый день" : L"Добрый вечер";
    HTitle(c, x, y, w, hello, wstring(WDF[m->tm_wday]) + L", " + std::to_wstring(m->tm_mday) + L" " + MON[m->tm_mon]);
    y += S(76);
    float gap = S(14), cw = (w - 2 * gap) / 3, ch = S(100);
    int today = (int)StatGet(DayKey(now));
    HStat(c, RectF(x, y, cw, ch), L"Сегодня вставал", Times(today), Min(today * STAND_MIN) + L" на ногах", H_G1);
    wstring nv, ns;
    if (AwayNow()) { nv = L"Пауза"; ns = L"вас нет за компьютером"; }
    else if (pendingKind >= 0) { nv = L"После игры"; ns = L"покажу, когда свернёте игру"; }
    else if (cur && cur->kind == P_REMIND) { nv = L"Сейчас"; ns = L"окно напоминания на экране"; }
    else if (dueAction == ACT_REMIND) { long mins = (long)((dueAt - now + 59) / 60); nv = Clock(dueAt); ns = L"через " + Min((int)std::max(0L, mins)); }
    else if (dueAction == ACT_DONE) { nv = L"Стоите"; ns = L"можно садиться в " + Clock(dueAt); }
    else { nv = L"—"; ns = L""; }
    HStat(c, RectF(x + cw + gap, y, cw, ch), L"Следующее напоминание", nv, ns, Color(255, 0x7D, 0xD3, 0xFC));
    HStat(c, RectF(x + 2 * (cw + gap), y, cw, ch), L"Серия", Days(StatStreak()), L"дней подряд вставал", Color(255, 0xFB, 0xBF, 0x24));
    y += ch + S(26);
    HSection(c, x, y, w, L"Быстрые действия"); y += S(32);
    Flow f = { x, x, y, x + w };
    FBtn(c, f, L"Я уже постоял " + Min(STAND_MIN), A_CMD, M_ALREADY, true);
    FBtn(c, f, L"Напомнить сейчас", A_CMD, M_NOW);
    if (timerEnd) FBtn(c, f, L"Стоп таймер  " + TimerLeft(), A_CMD, M_TIMER_STOP, true, Color(255, 0xFB, 0xBF, 0x24), Color(255, 0xF9, 0x73, 0x16));
    else FBtn(c, f, L"Таймер " + std::to_wstring((int)GetCfg(L"TimerMin", 4)) + L" мин", A_CMD, M_TIMER_LAST);
    FBtn(c, f, L"Другое время…", A_MENU, HM_TIMER);
    FBtn(c, f, L"Чёрный экран", A_CMD, M_B_SHOW);
    y = f.y + S(36) + S(26);
    HCard(c, RectF(x, y, w, S(200)));
    HLabel(c, RectF(x + S(18), y + S(14), w, S(22)), L"Подходы за неделю", S(14), H_TEXT, true);
    int v7[7]; for (int i = 0; i < 7; i++) v7[i] = (int)StatGet(DayKey(now - (time_t)(6 - i) * 86400));
    HBars(c, RectF(x + S(18), y + S(44), w - S(36), S(140)), v7, 7, false, H_G1, H_G2);
    return y + S(200);
}
static float HubStats(const HubCtx &c, float x, float y, float w) {
    time_t now = time(NULL); int today = (int)StatGet(DayKey(now)), total = (int)StatGet(L"Total"), steps = (int)GetCfg(L"StepsPerDay", 6500);
    HTitle(c, x, y, w, L"Статистика", L"Каждое «Да, встал» и каждый раз, когда вы отходили от компьютера");
    y += S(76);
    float gap = S(14), cw = (w - 3 * gap) / 4, ch = S(100);
    HStat(c, RectF(x, y, cw, ch), L"Сегодня", Times(today), Min(today * STAND_MIN), H_G1);
    HStat(c, RectF(x + (cw + gap), y, cw, ch), L"7 дней", Times(StatDays(7)), L"", H_TEXT);
    HStat(c, RectF(x + 2 * (cw + gap), y, cw, ch), L"30 дней", Times(StatDays(30)), L"", H_TEXT);
    HStat(c, RectF(x + 3 * (cw + gap), y, cw, ch), L"Всего", Times(total), L"лучший день: " + Times(StatBestDay()), H_TEXT);
    y += ch + S(16);
    HCard(c, RectF(x, y, w, S(210)));
    HLabel(c, RectF(x + S(18), y + S(14), w, S(22)), L"Последние 14 дней", S(14), H_TEXT, true);
    int v14[14]; for (int i = 0; i < 14; i++) v14[i] = (int)StatGet(DayKey(now - (time_t)(13 - i) * 86400));
    HBars(c, RectF(x + S(18), y + S(44), w - S(36), S(150)), v14, 14, false, H_G1, H_G2);
    y += S(226);
    HCard(c, RectF(x, y, w, S(150)));
    HLabel(c, RectF(x + S(18), y + S(14), w, S(22)), L"Польза за всё время", S(14), H_TEXT, true);
    wstring l1 = L"На ногах:  " + Dur(total * STAND_MIN) + L"   ·   ≈ " + Num((int)(total * STAND_MIN * 1.5 + 0.5)) + L" ккал сверх сидения";
    wstring l2 = L"Глаза отдыхали:  " + Times(total) + L"  (≈ " + Dur0(total * 20 / 60) + L" взгляда вдаль)";
    wstring l3 = L"Шаги:  " + Num(steps) + L" в день  ≈  " + Km(steps) + L" км и ≈ " + std::to_wstring((int)(steps * 0.04 + 0.5)) + L" ккал";
    wstring l4 = L"Серия:  " + Days(StatStreak()) + L" подряд";
    HLabel(c, RectF(x + S(18), y + S(44), w - S(36), S(22)), l1, S(13), H_SUB);
    HLabel(c, RectF(x + S(18), y + S(68), w - S(36), S(22)), l2, S(13), H_SUB);
    HLabel(c, RectF(x + S(18), y + S(92), w - S(36), S(22)), l3, S(13), H_SUB);
    HLabel(c, RectF(x + S(18), y + S(116), w - S(36), S(22)), l4, S(13), H_SUB);
    y += S(166);
    Flow f = { x, x, y, x + w };
    FBtn(c, f, L"−1 за сегодня", A_CMD, M_ST_MINUS);
    FBtn(c, f, L"+1 за сегодня", A_CMD, M_ST_PLUS);
    FBtn(c, f, L"Ещё (файл, сброс)…", A_MENU, HM_STATS);
    return f.y + S(36);
}
static float HubQuits(const HubCtx &c, float x, float y, float w) {
    HTitle(c, x, y, w, L"Отказы от вредного", L"Сколько вы уже держитесь");
    HButton(c, x + w - S(140), y + S(4), L"+ Добавить", A_MENU, HM_QADD, true, 0, S(140), Color(255, 0xA7, 0x8B, 0xFA), Color(255, 0x7C, 0x3A, 0xED));
    y += S(76);
    std::vector<QuitItem> q = QuitList();
    if (q.empty()) { HCard(c, RectF(x, y, w, S(80))); HLabel(c, RectF(x + S(18), y, w - S(36), S(80)), L"Пока пусто. Нажмите «+ Добавить» — например, «Сладкое».", S(14), H_SUB); return y + S(80); }
    for (size_t i = 0; i < q.size(); i++) {
        float ch = S(96); HCard(c, RectF(x, y, w, ch));
        HLabel(c, RectF(x + S(18), y + S(12), w - S(36), S(22)), q[i].name, S(15), H_TEXT, true);
        HLabel(c, RectF(x + S(18), y + S(36), w * 0.62f, S(30)), Elapsed(q[i].start), S(20), Color(255, 0xC4, 0xB5, 0xFD), true);
        wstring sub = L"с " + DateStr(q[i].start);
        if (q[i].best > time(NULL) - q[i].start) sub += L"   ·   рекорд: " + Span(q[i].best);
        HLabel(c, RectF(x + S(18), y + S(66), w * 0.62f, S(20)), sub, S(12), H_DIM);
        float bw = S(96), bh = S(30), bx = x + w - S(18) - bw, by = y + S(14);
        HButton(c, bx, by, L"Сорвался", A_ROW, M_Q_RELAPSE0 + (int)i, false, bh, bw);
        HButton(c, bx, by + bh + S(6), L"Удалить", A_ROW, M_Q_DEL0 + (int)i, false, bh, bw);
        HButton(c, bx - S(8) - S(74), by, L"+1 день", A_ROW, M_Q_PLUS0 + (int)i, false, bh, S(74));
        HButton(c, bx - S(8) - S(74), by + bh + S(6), L"−1 день", A_ROW, M_Q_MINUS0 + (int)i, false, bh, S(74));
        y += ch + S(12);
    }
    return y;
}
static float HubRems(const HubCtx &c, float x, float y, float w) {
    HTitle(c, x, y, w, L"Мои напоминания", L"Напомню в нужное время звуком и окном");
    y += S(76);
    Flow f = { x, x, y, x + w };
    FBtn(c, f, L"Дата и время…", A_CMD, M_R_PICK, true, Color(255, 0xF4, 0x72, 0xB6), Color(255, 0xDB, 0x27, 0x77));
    FBtn(c, f, L"Через 1 час", A_CMD, M_R_1H);
    FBtn(c, f, L"Через 1 день", A_CMD, M_R_1D);
    FBtn(c, f, L"Через 1 месяц", A_CMD, M_R_1M);
    y = f.y + S(36) + S(16);
    if (RepeatOn()) {
        Flow f2 = { x, x, y, x + w };
        Color p1(255, 0x60, 0xA5, 0xFA), p2(255, 0x63, 0x66, 0xF1);
        FBtn(c, f2, L"Повтор через день…", A_CMD, M_R_REP2, true, p1, p2);
        FBtn(c, f2, L"Повтор каждый день…", A_CMD, M_R_REP1);
        FBtn(c, f2, L"Повтор раз в неделю…", A_CMD, M_R_REP7);
        y = f2.y + S(36) + S(20);
    } else {
        HCard(c, RectF(x, y, w, S(70)));
        HLabel(c, RectF(x + S(18), y + S(12), w - S(170), S(22)), L"Повторяющиеся напоминания выключены", S(14), H_TEXT, true);
        HLabel(c, RectF(x + S(18), y + S(36), w - S(170), S(20)), L"Например, «Тренировка» через день в 18:00", S(12), H_DIM);
        HButton(c, x + w - S(18) - S(120), y + S(17), L"Включить", A_CMD, M_REP_TOGGLE, false, S(36), S(120));
        y += S(70) + S(20);
    }
    std::vector<Reminder> r = RemVisible();
    if (r.empty()) { HCard(c, RectF(x, y, w, S(80))); HLabel(c, RectF(x + S(18), y, w - S(36), S(80)), L"Запланированных напоминаний нет.", S(14), H_SUB); return y + S(80); }
    for (size_t i = 0; i < r.size(); i++) {
        float ch = S(70); HCard(c, RectF(x, y, w, ch));
        wstring when = WhenStr(r[i].at); if (r[i].every) when += L"   ·   повтор " + EveryStr(r[i].every);
        HLabel(c, RectF(x + S(18), y + S(10), w - S(260), S(22)), when, S(13), r[i].every ? Color(255, 0x93, 0xC5, 0xFD) : Color(255, 0xF9, 0xA8, 0xD4), true);
        HLabel(c, RectF(x + S(18), y + S(34), w - S(260), S(26)), r[i].text, S(15), H_TEXT);
        HButton(c, x + w - S(18) - S(100), y + (ch - S(32)) / 2, L"Удалить", A_ROW, M_R_DEL0 + (int)i, false, S(32), S(100));
        if (r[i].every) HButton(c, x + w - S(18) - S(100) - S(10) - S(110), y + (ch - S(32)) / 2, L"Сдвинуть +1 д", A_ROW, M_R_SHIFT0 + (int)i, false, S(32), S(110));
        y += ch + S(10);
    }
    return y;
}
static float HubGames(const HubCtx &c, float x, float y, float w) {
    HTitle(c, x, y, w, L"Время в играх", L"Игры, кроме Steam, — сами или добавленные вручную");
    y += S(76);
    GameAgg a = GameStats(); long long wk = 0, mo = 0; for (int i = 0; i < 30; i++) { mo += a.day[i]; if (i < 7) wk += a.day[i]; }
    float gap = S(14), cw = (w - 3 * gap) / 4, ch = S(100);
    Color gc(255, 0x7D, 0xD3, 0xFC);
    HStat(c, RectF(x, y, cw, ch), L"Сегодня", HM(a.day[0]), gamesNow.empty() ? L"" : L"сейчас: " + gamesNow[0], gc);
    HStat(c, RectF(x + (cw + gap), y, cw, ch), L"7 дней", HM(wk), L"", H_TEXT);
    HStat(c, RectF(x + 2 * (cw + gap), y, cw, ch), L"30 дней", HM(mo), L"", H_TEXT);
    HStat(c, RectF(x + 3 * (cw + gap), y, cw, ch), L"Всего", HM(a.all), L"", H_TEXT);
    y += ch + S(16);
    HCard(c, RectF(x, y, w, S(200)));
    HLabel(c, RectF(x + S(18), y + S(14), w, S(22)), L"По дням", S(14), H_TEXT, true);
    int v7[7]; for (int i = 0; i < 7; i++) v7[i] = (int)(a.day[6 - i] / 60);
    HBars(c, RectF(x + S(18), y + S(44), w - S(36), S(140)), v7, 7, true, Color(255, 0x38, 0xBD, 0xF8), Color(255, 0x25, 0x63, 0xEB));
    y += S(216);
    Flow f = { x, x, y, x + w };
    FBtn(c, f, L"Добавить игру (.exe)…", A_CMD, M_G_ADD);
    FBtn(c, f, L"Сбросить время…", A_CMD, M_GAMES_RESET);
    y = f.y + S(36) + S(16);
    if (!gamesLoaded) LoadGameLists();
    std::set<wstring> names; for (auto &kv : knownGames) names.insert(kv.second);
    std::vector<wstring> list(names.begin(), names.end()); if (list.size() > 40) list.resize(40);
    for (size_t i = 0; i < list.size(); i++) {
        const wstring &g = list[i]; float rh = S(70); HCard(c, RectF(x, y, w, rh));
        HLabel(c, RectF(x + S(18), y + S(10), w - S(260), S(24)), g, S(15), H_TEXT, true);
        wstring l2 = L"сегодня " + HM(a.today[g]) + L"   ·   7 дней " + HM(a.week[g]) + L"   ·   всего " + HM(a.total[g]);
        HLabel(c, RectF(x + S(18), y + S(36), w - S(260), S(22)), l2, S(12), H_SUB);
        HButton(c, x + w - S(18) - S(104), y + (rh - S(32)) / 2, L"Не игра", A_ROW, M_G_DEL0 + (int)i, false, S(32), S(104));
        HButton(c, x + w - S(18) - S(104) - S(10) - S(130), y + (rh - S(32)) / 2, L"Переименовать", A_ROW, M_G_REN0 + (int)i, false, S(32), S(130));
        y += rh + S(10);
    }
    if (list.empty()) { HLabel(c, RectF(x, y, w, S(40)), L"Игр пока нет: игра добавится сама, если открыть её на весь экран на минуту.", S(13), H_SUB); y += S(40); }
    return y;
}
static float HubSettings(const HubCtx &c, float x, float y, float w) {
    HTitle(c, x, y, w, L"Настройки", L"Версия " APP_VERSION L"  ·  создана " APP_CREATED);
    y += S(76);
    auto Row = [&](const wstring &label, const wstring &hint) {
        HCard(c, RectF(x, y, w, S(62)));
        HLabel(c, RectF(x + S(18), y + (hint.empty() ? S(20) : S(10)), w * 0.6f, S(22)), label, S(14), H_TEXT, true);
        if (!hint.empty()) HLabel(c, RectF(x + S(18), y + S(32), w * 0.6f, S(20)), hint, S(12), H_DIM);
    };
    auto Toggle = [&](const wstring &label, const wstring &hint, bool on, int cmd) {
        Row(label, hint); HToggle(c, RectF(x + w - S(18) - S(48), y + S(19), S(48), S(26)), on, cmd); y += S(62) + S(8);
    };
    auto Drop = [&](const wstring &label, const wstring &hint, const wstring &value, int menu) {
        Row(label, hint); HDropdown(c, RectF(x + w - S(18) - S(220), y + S(13), S(220), S(36)), value, menu); y += S(62) + S(8);
    };
    auto Btn = [&](const wstring &label, const wstring &hint, const wstring &btn, int cmd, int act = A_CMD) {
        Row(label, hint); float bw = MeasureW(btn, semiFont.c_str(), S(13)) + S(30);
        HButton(c, x + w - S(18) - bw, y + S(13), btn, act, cmd, false, S(36), bw); y += S(62) + S(8);
    };
    HSection(c, x, y, w, L"Напоминания"); y += S(30);
    Toggle(L"Запускать вместе с Windows", L"", AutoStartOn(), M_AUTO);
    Toggle(L"Озвучивать напоминание голосом", L"", VoiceOn(), M_VOICE);
    Drop(L"Голос", L"", VOICE_NAMES[std::min(6, (int)GetCfg(L"VoiceIdx", 0))], HM_VOICE);
    Drop(L"Громкость голоса", L"", std::to_wstring(GetCfg(L"Volume", 50)) + L"%", HM_VOL);
    { int pm = (int)GetCfg(L"Position", POS_BR); Drop(L"Где показывать окно", L"Окно можно и просто перетащить мышкой", pm >= 0 && pm < 5 ? POS_NAMES[pm] : L"Своё место", HM_POS); }
    Toggle(L"Засчитывать, когда отошёл от компьютера", L"5 минут без мыши и клавиатуры", CountAway(), M_AWAY);
    Toggle(L"Повторяющиеся напоминания", L"Например, тренировка через день — добавлять в разделе «Напоминания»", RepeatOn(), M_REP_TOGGLE);
    {
        bool dotaOn = !RegStr(HKEY_CURRENT_USER, CFG_KEY, L"DotaCfgFile").empty();
        wstring st = GsiActive() ? (gsiInMatch ? L"идёт матч" : L"в меню") : dotaOn ? L"подключена" : L"не подключена";
        Drop(L"Dota 2: напоминать между матчами", L"Сейчас: " + st, dotaOn ? L"Подключена" : L"Подключить…", HM_DOTA);
    }
    y += S(10); HSection(c, x, y, w, L"Ночной режим (тёплый экран)"); y += S(30);
    Toggle(L"По расписанию", L"", GetCfg(L"NightOn", 1) != 0, M_N_SCHED);
    Drop(L"Начало вечером", L"", HHMM((int)GetCfg(L"NightStart", 0)), HM_NSTART);
    Drop(L"Конец утром", L"", HHMM((int)GetCfg(L"NightEnd", 7 * 60)), HM_NEND);
    { int nk = (int)GetCfg(L"NightK", 3400); wstring nv = std::to_wstring(nk) + L" K"; for (int i = 0; i < 4; i++) if (N_KS[i] == nk) nv = N_KNAMES[i];
      Drop(L"Насколько тёплый", L"", nv, HM_NK); }
    Btn(L"Посмотреть, как будет", L"Экран станет тёплым на 10 секунд", L"Показать", M_N_PREVIEW);
    if (nightLimited) Btn(L"Windows ограничивает силу", L"Нужны права администратора и перезагрузка", L"Разрешить…", M_N_UNLOCK);
    y += S(10); HSection(c, x, y, w, L"Чёрный экран (OLED)"); y += S(30);
    Toggle(L"Иконка на панели задач при запуске", L"", GetCfg(L"BlackOnStart", 1) != 0, M_B_START);
    { int bi = (int)GetCfg(L"AutoBlackMin", 1); Drop(L"Включать сам", L"Если не трогать мышь и клавиатуру", bi ? L"Через " + std::to_wstring(bi) + L" мин" : wstring(L"Не включать"), HM_BLACK); }
    y += S(10); HSection(c, x, y, w, L"Прочее"); y += S(30);
    Drop(L"Сколько шагов в день я хожу", L"Для подсчёта пользы", Num((int)GetCfg(L"StepsPerDay", 6500)), HM_STEPS);
    Btn(L"Перенос на другой компьютер", L"Статистика, отказы, игры, напоминания", L"Сохранить в файл…", M_EXPORT);
    Btn(L"Загрузить из файла", L"После переустановки Windows", L"Загрузить…", M_IMPORT);
    Btn(L"Обновления", updVersion.empty() ? wstring(L"Сейчас версия " APP_VERSION) : L"Доступна версия " + updVersion,
        updVersion.empty() ? wstring(L"Проверить") : wstring(L"Обновить"), updVersion.empty() ? M_UPD_CHECK : M_UPD_NOW);
    Btn(L"Журнал событий", L"Когда приходили напоминания и что нажималось", L"Открыть", M_LOG);
    Btn(L"Выйти из программы", L"Напоминания перестанут приходить", L"Выход", M_EXIT);
    return y;
}

static void HubPaint(HWND h, HDC dc) {
    RECT cr; GetClientRect(h, &cr); int W = cr.right, H = cr.bottom; if (W <= 0 || H <= 0) return;
    Bitmap bmp(W, H, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);   // плавные буквы, без привязки к пиксельной сетке
    g.SetTextContrast(2);
    SolidBrush bg(H_BG); g.FillRectangle(&bg, 0, 0, W, H);
    hubHits.clear();
    // боковая панель
    float sw = S(220);
    SolidBrush sb(H_SIDE); g.FillRectangle(&sb, RectF(0, 0, sw, (REAL)H));
    Icon(g, RectF(S(20), S(22), S(36), S(36)), H_G1, H_G2, G_PERSON);
    Text(g, L"Напоминалка", semiFont.c_str(), S(17), FontStyleRegular, H_TEXT, RectF(S(66), S(22), sw - S(70), S(20)), StringAlignmentNear, StringAlignmentNear, true);
    Text(g, L"версия " APP_VERSION, L"Segoe UI", S(12), FontStyleRegular, H_DIM, RectF(S(66), S(42), sw - S(70), S(18)), StringAlignmentNear, StringAlignmentNear, true);
    HubCtx side = { &g, RectF(0, 0, sw, (REAL)H), 0 };
    for (int i = 0; i < 6; i++) {
        RectF r(S(12), S(88) + i * S(46), sw - S(24), S(40)); bool act = hubTab == i, hv = Hov(A_NAV, i);
        if (act || hv) { GraphicsPath p; RR(p, r, S(10)); SolidBrush b(act ? Color(30, 0x3D, 0xDC, 0x97) : Color(14, 255, 255, 255)); g.FillPath(&b, &p); }
        if (act) { SolidBrush ab(H_G1); g.FillRectangle(&ab, RectF(r.X, r.Y + S(10), S(3), r.Height - S(20))); }
        HubIcon(g, RectF(r.X + S(14), r.Y + S(9), S(22), S(22)), HUB_GLYPH[i], act ? H_G1 : H_SUB);
        Text(g, HUB_TABS[i], act ? semiFont.c_str() : L"Segoe UI", S(14), FontStyleRegular, act ? H_TEXT : H_SUB,
             RectF(r.X + S(46), r.Y, r.Width - S(50), r.Height), StringAlignmentNear, StringAlignmentCenter, true);
        AddHit(side, r, A_NAV, i);
    }
    // содержимое
    RectF view(sw, 0, W - sw, (REAL)H);
    Region old; g.GetClip(&old); g.SetClip(view);
    float pad = S(32), cx = sw + pad, cwid = W - sw - 2 * pad;
    if (hubScroll[hubTab] < 0) hubScroll[hubTab] = 0;
    HubCtx c = { &g, view, hubScroll[hubTab] };
    float endY = 0, y0 = S(28);
    switch (hubTab) {
    case 0: endY = HubHome(c, cx, y0, cwid); break;
    case 1: endY = HubStats(c, cx, y0, cwid); break;
    case 2: endY = HubQuits(c, cx, y0, cwid); break;
    case 3: endY = HubRems(c, cx, y0, cwid); break;
    case 4: endY = HubGames(c, cx, y0, cwid); break;
    default: endY = HubSettings(c, cx, y0, cwid); break;
    }
    hubContentH = endY + S(32); hubViewH = (float)H;
    float maxScroll = std::max(0.0f, hubContentH - hubViewH);
    if (hubScroll[hubTab] > maxScroll) { hubScroll[hubTab] = maxScroll; InvalidateRect(h, NULL, FALSE); }
    if (maxScroll > 0) {                              // полоса прокрутки
        float th = std::max(S(40), hubViewH * hubViewH / hubContentH), ty = (hubViewH - th) * (hubScroll[hubTab] / maxScroll);
        GraphicsPath sp; RR(sp, RectF(W - S(8), ty + S(4), S(4), th - S(8)), S(2)); SolidBrush tb(Color(50, 255, 255, 255)); g.FillPath(&tb, &sp);
    }
    g.SetClip(&old);
    Graphics screen(dc); screen.DrawImage(&bmp, 0, 0, W, H);
}
static void HubRefresh() { if (hHub) InvalidateRect(hHub, NULL, FALSE); }
static const Hit *HubHitAt(int x, int y) {
    PointF pt((REAL)x, (REAL)y);
    for (auto it = hubHits.rbegin(); it != hubHits.rend(); ++it) if (it->r.Contains(pt)) return &*it;
    return nullptr;
}
static void HubDo(const Hit &hit) {
    int act = hit.act, arg = hit.arg; RectF r = hit.r;
    if (act == A_NAV) { hubTab = arg; HubRefresh(); return; }
    if (act == A_CMD) { HandleCmd(hMain, arg); HubRefresh(); return; }
    if (act == A_ROW) { HMENU m = BuildFullMenu(); DestroyMenu(m); HandleCmd(hMain, arg); HubRefresh(); return; }   // свежие списки строк
    if (act == A_MENU) {
        HMENU root = BuildFullMenu();
        HMENU sub = FindSub(FindSub(root, HUB_MENU_PATH[arg][0]), HUB_MENU_PATH[arg][1]);
        if (sub) {
            POINT pt = { (LONG)r.X, (LONG)(r.Y + r.Height + S(4)) }; ClientToScreen(hHub, &pt);
            SetForegroundWindow(hHub);
            int c = TrackPopupMenu(sub, TPM_RETURNCMD | TPM_LEFTBUTTON, pt.x, pt.y, 0, hHub, NULL);
            DestroyMenu(root);
            if (c) HandleCmd(hMain, c);
        } else DestroyMenu(root);
        HubRefresh();
    }
}
static LRESULT CALLBACK HubProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); HubPaint(h, dc); EndPaint(h, &ps); return 0; }
    case WM_SIZE: HubRefresh(); return 0;
    case WM_GETMINMAXINFO: {
        RECT r = { 0, 0, (LONG)S(900), (LONG)S(560) }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        MINMAXINFO *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = r.right - r.left; mm->ptMinTrackSize.y = r.bottom - r.top; return 0; }
    case WM_TIMER: HubRefresh(); return 0;                               // часы, таймер, «через N мин»
    case WM_MOUSEMOVE: {
        const Hit *ht = HubHitAt((short)LOWORD(lp), (short)HIWORD(lp));
        int a = ht ? ht->act : 0, g2 = ht ? ht->arg : 0;
        if (a != hubHoverAct || g2 != hubHoverArg) { hubHoverAct = a; hubHoverArg = g2; HubRefresh(); }
        TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, h, 0 }; TrackMouseEvent(&te);
        return 0; }
    case WM_MOUSELEAVE: if (hubHoverAct) { hubHoverAct = hubHoverArg = 0; HubRefresh(); } return 0;
    case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(NULL, hubHoverAct ? IDC_HAND : IDC_ARROW)); return TRUE; } break;
    case WM_LBUTTONDOWN: { const Hit *ht = HubHitAt((short)LOWORD(lp), (short)HIWORD(lp)); hubPressAct = ht ? ht->act : 0; hubPressArg = ht ? ht->arg : 0; return 0; }
    case WM_LBUTTONUP: {
        const Hit *ht = HubHitAt((short)LOWORD(lp), (short)HIWORD(lp));
        if (ht && ht->act == hubPressAct && ht->arg == hubPressArg) { Hit copy = *ht; hubPressAct = 0; HubDo(copy); }
        hubPressAct = 0; return 0; }
    case WM_MOUSEWHEEL: {
        hubScroll[hubTab] -= GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * S(70);
        float mx = std::max(0.0f, hubContentH - hubViewH);
        hubScroll[hubTab] = std::max(0.0f, std::min(mx, hubScroll[hubTab])); HubRefresh(); return 0; }
    case WM_KEYDOWN: if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; } break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: if (hHub == h) hHub = NULL; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
static void HubShow() {
    if (hHub) { if (IsIconic(hHub)) ShowWindow(hHub, SW_RESTORE); SetForegroundWindow(hHub); HubRefresh(); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {}; wc.hInstance = hInst; wc.lpfnWndProc = HubProc; wc.lpszClassName = L"StandHub";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
        wc.hbrBackground = CreateSolidBrush(RGB(0x12, 0x15, 0x1C)); RegisterClassW(&wc); reg = true;
    }
    RECT r = { 0, 0, (LONG)S(1000), (LONG)S(680) };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME | WS_MAXIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = r.right - r.left, hh = std::min((int)(r.bottom - r.top), (int)(wa.bottom - wa.top - 20));
    hHub = CreateWindowExW(0, L"StandHub", L"Напоминалка", style, (wa.left + wa.right - w) / 2, (wa.top + wa.bottom - hh) / 2, w, hh, NULL, NULL, hInst, NULL);
    if (!hHub) return;
    BOOL dark = TRUE;                                               // тёмный заголовок окна
    DwmSetWindowAttribute(hHub, 20, &dark, sizeof dark); DwmSetWindowAttribute(hHub, 19, &dark, sizeof dark);
    COLORREF cap = RGB(0x0D, 0x10, 0x16); DwmSetWindowAttribute(hHub, 35, &cap, sizeof cap);
    ShowWindow(hHub, SW_SHOW); SetForegroundWindow(hHub);
    SetTimer(hHub, 1, 1000, NULL);
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (msgTaskbarCreated && m == msgTaskbarCreated) { AddTray(); return 0; }
    switch (m) {
    case WM_SHOWBLACK: BlackCreate(true); return 0;
    case WM_ALREADY:                                 // кто-то запустил программу ещё раз
        if (!ShouldDefer()) {                        // в игре — молчим
            nid.uFlags = NIF_INFO; wcscpy(nid.szInfoTitle, L"Напоминалка уже работает");
            wcscpy(nid.szInfo, L"Её значок — возле часов. Вторая копия не нужна.");
            nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND; Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
        return 0;
    case WM_UPDCHECKED: {
        updBusy = false; wstring *r = (wstring *)lp;
        if (!r) { if (updManual) MessageBoxW(h, L"Не удалось проверить обновления (нет интернета?). Попробуйте позже.", L"Напоминалка", MB_ICONWARNING | MB_TOPMOST); return 0; }
        size_t nl = r->find(L'\n'); wstring tag = r->substr(0, nl), url = r->substr(nl + 1); delete r;
        if (VerNum(tag) <= VerNum(APP_VERSION)) {
            updVersion.clear(); updUrl.clear();
            if (updManual) MessageBoxW(h, L"У вас последняя версия: " APP_VERSION L".", L"Напоминалка", MB_ICONINFORMATION | MB_TOPMOST);
            return 0;
        }
        updVersion = tag[0] == L'v' ? tag.substr(1) : tag; updUrl = url;
        Log(L"Найдена новая версия " + updVersion);
        if (updManual) {
            wstring q = L"Доступна версия " + updVersion + L" (у вас " APP_VERSION L").\n\nОбновить сейчас? Программа перезапустится, статистика и настройки сохранятся.";
            if (MessageBoxW(h, q.c_str(), L"Напоминалка", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) == IDYES) UpdStartDownload();
        } else updPopupPending = true;               // покажем, когда не будет игры и других окон
        return 0; }
    case WM_UPDDOWNLOADED:
        updBusy = false;
        if (wp) UpdInstall();
        else { Log(L"Обновление не скачалось"); MessageBoxW(h, L"Не получилось скачать обновление. Попробуйте позже.", L"Напоминалка", MB_ICONWARNING | MB_TOPMOST); }
        return 0;
    case WM_GSI:
        if (WSAGETSELECTEVENT(lp) == FD_ACCEPT) GsiAccept(h);
        return 0;
    case WM_TIMER:
        if (wp == T_GAMES) { GamePoll(); return 0; }
        if (wp == T_UPDCHECK) { SetTimer(h, T_UPDCHECK, 6 * 3600 * 1000, NULL); if (!testMode) UpdCheck(false); return 0; }
        if (wp == T_DELOLD) { KillTimer(h, T_DELOLD); DeleteFileW((ExePath() + L".old").c_str()); return 0; }
        if (wp == T_NIGHT) { NightUpdate(nightAppliedK < 6500); return 0; }
        if (wp == T_NIGHT_PREVIEW) { KillTimer(h, T_NIGHT_PREVIEW); previewUntil = 0; NightUpdate(true); return 0; }
        CheckAway();
        if (timerEnd) {
            if (time(NULL) >= timerEnd) {            // звук сразу, окно — как только можно показать
                Log(L"Сработал таймер");
                timerEnd = 0; timerFiredMin = timerMin; timerPending = true;
                if (VoiceOn()) Speak(2); else Chime();
            }
            UpdateTip();
        }
        RemCheck();
        if (updPopupPending && !ShouldDefer() && !away && (!cur || cur->kind == P_STATS || cur->kind == P_POSDEMO)) { updPopupPending = false; ShowPopup(P_UPDATE); }
        BlackAutoCheck();
        if (!remQueue.empty() && !ShouldDefer() && !(cur && (cur->kind == P_QUIT || cur->kind == P_TIMER || cur->kind == P_CUSTOM || cur->kind == P_REMIND || cur->kind == P_DONE || cur->dragging))) {
            wstring id = remQueue.front(); remQueue.erase(remQueue.begin());
            Reminder r;
            if (RemGet(id, r)) {
                if (cur && (cur->kind == P_REMIND || cur->kind == P_DONE)) pendingKind = cur->kind;
                showCustomId = id; ShowPopup(P_CUSTOM);
            }
        }
        if (timerPending && !ShouldDefer() && !(cur && (cur->kind == P_QUIT || cur->kind == P_CUSTOM || cur->dragging))) {
            timerPending = false;
            if (cur && (cur->kind == P_REMIND || cur->kind == P_DONE)) pendingKind = cur->kind;   // вернём напоминание после таймера
            ShowPopup(P_TIMER);
        }
        if (dueAction != ACT_NONE && time(NULL) >= dueAt) {
            int a = dueAction; dueAction = ACT_NONE;
            if (a == ACT_REMIND) { if (!away) pendingKind = P_REMIND; Log(away ? L"Время напоминания, но вас нет за компьютером — пауза" : L"Время напоминания «Пора встать»"); }
            else { sittingSince = time(NULL); pendingKind = P_DONE; Schedule(WORK_MIN, ACT_REMIND); }
            UpdateTip();
        }
        // показываем, только когда нет полноэкранного приложения и в Доте не идёт матч
        static bool deferLogged = false;
        if (pendingKind < 0) deferLogged = false;
        else if (!deferLogged && ShouldDefer()) {
            deferLogged = true;
            Log(GsiActive() && gsiInMatch ? wstring(L"Отложено: идёт матч Dota 2") : L"Отложено: открыто на весь экран — " + ForegroundExe());
        }
        // своё напоминание, которое висит дольше 2 минут, уступает место «Пора встать»
        if (pendingKind >= 0 && !ShouldDefer() && !(cur && (cur->kind == P_QUIT || cur->kind == P_TIMER || (cur->kind == P_CUSTOM && GetTickCount() - cur->shownAt <= 120000) || cur->dragging))) {
            int k = pendingKind; pendingKind = -1;
            ShowPopup((Kind)k); UpdateTip();
            Log(k == P_REMIND ? L"Показано окно «Пора встать»" : L"Показано окно «Можно садиться»");
        }
        // Страховка: если по какой-то причине напоминание нигде не запланировано — запланировать снова
        {
            static int lostFor = 0;
            bool lost = !away && dueAction == ACT_NONE && pendingKind < 0 && !(cur && (cur->kind == P_REMIND || cur->kind == P_QUIT));
            lostFor = lost ? lostFor + 1 : 0;
            if (lostFor >= 10) { lostFor = 0; Log(L"Напоминание оказалось не запланировано — исправлено, через " + std::to_wstring(SNOOZE_MIN) + L" мин"); Schedule(SNOOZE_MIN, ACT_REMIND); }
        }
        return 0;
    case WM_TRAY:
        if (lp == WM_LBUTTONUP) { HubShow(); return 0; }
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
            POINT pt; GetCursorPos(&pt);
            HMENU mn = CreatePopupMenu();
            AppendMenuW(mn, MF_STRING, M_HUB, L"Открыть Напоминалку");
            SetMenuDefaultItem(mn, M_HUB, FALSE);
            AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
            wstring next = AwayNow() ? wstring(L"Пауза — вас нет за компьютером") : pendingKind >= 0 ? wstring(L"Напомню после матча / игры") : dueAction == ACT_NONE ? wstring(L"Напоминалка") : L"Следующее в " + Clock(dueAt);
            AppendMenuW(mn, MF_STRING | MF_GRAYED, 0, next.c_str());
            wstring already = L"Я уже постоял " + Min(STAND_MIN);
            AppendMenuW(mn, MF_STRING, M_ALREADY, already.c_str());
            if (timerEnd) { wstring tl = L"Остановить таймер (" + TimerLeft() + L")"; AppendMenuW(mn, MF_STRING, M_TIMER_STOP, tl.c_str()); }
            else { wstring tq = L"Таймер на " + Min((int)GetCfg(L"TimerMin", 4)); AppendMenuW(mn, MF_STRING, M_TIMER_LAST, tq.c_str()); }
            AppendMenuW(mn, MF_STRING, M_B_SHOW, L"Чёрный экран");
            AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
            HMENU full = BuildFullMenu();
            AppendMenuW(mn, MF_POPUP, (UINT_PTR)full, L"Все настройки");
            AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
            AppendMenuW(mn, MF_STRING, M_EXIT, L"Выход");
            SetForegroundWindow(h);
            int c = TrackPopupMenu(mn, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
            DestroyMenu(mn);
            if (c == M_HUB) HubShow(); else if (c) HandleCmd(h, c);
            HubRefresh();
        }
        return 0;
    case WM_DESTROY:
        ApplyKelvin(6500);              // вернуть обычные цвета
        Shell_NotifyIconW(NIM_DELETE, &nid); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool FontOk(const wchar_t *name) { FontFamily f(name); return f.IsAvailable(); }

// Страховка: если программа упадёт, она перезапустится сама (не чаще раза в 2 минуты) — напоминания не пропадут
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *ep) {
    static bool inCrash = false; if (inCrash) return EXCEPTION_EXECUTE_HANDLER; inCrash = true;
    wchar_t code[32]; swprintf(code, 32, L"%08lX", ep && ep->ExceptionRecord ? (unsigned long)ep->ExceptionRecord->ExceptionCode : 0UL);
    Log(L"Сбой программы (код " + wstring(code) + L")");
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DWORD last = GetCfg(L"LastCrash", 0), now = (DWORD)time(NULL); SetCfg(L"LastCrash", now);
    if (!testMode && now - last > 120) {
        wstring cmd = L"\"" + ExePath() + L"\" --restart";
        STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
        if (CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); Log(L"Перезапуск после сбоя"); }
    } else Log(L"Два сбоя подряд — без перезапуска");
    return EXCEPTION_EXECUTE_HANDLER;
}
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int) {
    SetUnhandledExceptionFilter(CrashFilter);
    hInst = hi;
#ifdef TEST_MODE
    mtx = CreateMutexW(NULL, TRUE, L"StandReminder_SingleInstance_Test");   // тестовая не мешает основной запуститься
#else
    mtx = CreateMutexW(NULL, TRUE, L"StandReminder_SingleInstance");
#endif
    if (GetLastError() == ERROR_ALREADY_EXISTS && cmd && (wcsstr(cmd, L"--updated") || wcsstr(cmd, L"--restart"))) {   // старая копия ещё закрывается
        for (int i = 0; i < 50 && mtx; i++) { DWORD w = WaitForSingleObject(mtx, 200); if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) break; }
        SetLastError(0);
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (cmd && wcsstr(cmd, L"--black")) {        // нажали закреплённую иконку «Чёрный экран»
            HWND m = FindWindowW(L"StandMain", NULL);
            if (m) { AllowSetForegroundWindow(ASFW_ANY); PostMessageW(m, WM_SHOWBLACK, 0, 0); }
            return 0;
        }
        // Никаких окон: сообщение забирало фокус и сворачивало игру. Просто сообщаем работающей копии и выходим.
        Log(wstring(L"Попытка запустить вторую копию — закрыта молча. Параметры: «") + (cmd ? cmd : L"") + L"»");
        HWND m = FindWindowW(L"StandMain", NULL);
        if (m) PostMessageW(m, WM_ALREADY, 0, 0);
        return 0;
    }
#ifdef TEST_MODE
    WORK_MIN = STAND_MIN = SNOOZE_MIN = 1; IGNORE_MIN = 2; IGNORE_MS = 20000; testMode = true; (void)cmd;
#else
    if (cmd && wcsstr(cmd, L"test")) { WORK_MIN = STAND_MIN = SNOOZE_MIN = 1; IGNORE_MIN = 2; IGNORE_MS = 20000; }
#endif
    SetProcessDPIAware();
    HDC dc = GetDC(NULL); sc = GetDeviceCaps(dc, LOGPIXELSY) / 96.0f; ReleaseDC(NULL, dc);

    CoInitialize(NULL);
    GdiplusStartupInput gsi; ULONG_PTR tok; GdiplusStartup(&tok, &gsi, NULL);
    {
        semiFont = FontOk(L"Segoe UI Semibold") ? L"Segoe UI Semibold" : L"Segoe UI";

        WNDCLASSW wc = {}; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
        wc.lpfnWndProc = MainProc; wc.lpszClassName = L"StandMain"; RegisterClassW(&wc);
        wc.lpfnWndProc = PopupProc; wc.lpszClassName = L"StandPopup"; RegisterClassW(&wc);

        msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        hMain = CreateWindowW(L"StandMain", L"StandReminder", 0, 0, 0, 0, 0, NULL, NULL, hi, NULL);
        AddTray();
        sittingSince = time(NULL);
        if (!testMode && AutoStartOn()) SetAutoStart(true);   // exe перенесли/заменили — автозапуск указывает на текущий файл
        Log(L"Запуск, версия " APP_VERSION L". Первое напоминание через " + std::to_wstring(WORK_MIN) + L" мин");
        SetTimer(hMain, T_DELOLD, 4000, NULL);                       // убрать старый exe после обновления
        if (!testMode) SetTimer(hMain, T_UPDCHECK, 90 * 1000, NULL);   // первая проверка через полторы минуты, дальше раз в 6 часов
        if (cmd && wcsstr(cmd, L"--updated")) {
            nid.uFlags = NIF_INFO; wcscpy(nid.szInfoTitle, L"Напоминалка обновлена");
            wcscpy(nid.szInfo, L"Установлена версия " APP_VERSION L". Всё работает как раньше.");
            nid.dwInfoFlags = NIIF_INFO; Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
        Schedule(WORK_MIN, ACT_REMIND);
        GsiStart(hMain);
        GamePoll(); SetTimer(hMain, T_GAMES, 30000, NULL);
        NightUpdate(true); SetTimer(hMain, T_NIGHT, 30000, NULL);
        if (cmd && wcsstr(cmd, L"--black")) BlackCreate(true);
        else if (GetCfg(L"BlackOnStart", 1)) BlackCreate(false);
        SetTimer(hMain, T_MAIN, 1000, NULL);
        if (cmd && wcsstr(cmd, L"--fastidle")) awaySecOverride = 6;
        if (cmd && wcsstr(cmd, L"--quit-demo")) {
            time_t n = time(NULL);
            SetRegQ(QUIT_KEY, L"Сладкое", n - 45 * 86400 - 3600); SetRegQ(QUIT_KEY, L"Газировка", n - 3 * 86400 - 5 * 3600);
            SetRegQ(QUIT_KEY, L"Фастфуд", n - 400 * 86400); SetRegQ(QUIT_KEY, L"Энергетики", n - 2 * 3600 - 300); SetRegQ(QBEST_KEY, L"Энергетики", 20 * 86400);
        }
        if (cmd && wcsstr(cmd, L"--quits")) ShowPopup(P_QUITS);
        if (cmd && wcsstr(cmd, L"--games")) ShowPopup(P_GAMES);
        if (cmd && wcsstr(cmd, L"--remtest")) RemAdd(time(NULL) + 3, L"Позвонить маме");
        if (cmd && wcsstr(cmd, L"--rempick")) AskReminder(hMain, 0, true);
        if (cmd && wcsstr(cmd, L"--nighttest")) { previewUntil = GetTickCount() + 8000; NightUpdate(true); }
        if (cmd && wcsstr(cmd, L"--fastgames")) SetTimer(hMain, T_GAMES, 3000, NULL);
        if (cmd && wcsstr(cmd, L"--ask")) { wstring nm; if (AskText(hMain, L"Новый отказ", L"От чего отказываетесь?", nm)) QuitAdd(nm); ShowPopup(P_QUITS); }
        if (cmd && wcsstr(cmd, L"--timer5s")) { timerMin = 4; timerEnd = time(NULL) + 5; }
        if (cmd && wcsstr(cmd, L"--export")) ExportStats(hMain);
        if (cmd && wcsstr(cmd, L"--import")) ImportStats(hMain);
        if (cmd && wcsstr(cmd, L"--show-remind")) ShowPopup(P_REMIND);
        if (cmd && wcsstr(cmd, L"--show-done")) ShowPopup(P_DONE);
        if (cmd && wcsstr(cmd, L"--show-quit")) ShowPopup(P_QUIT);
        if (cmd && wcsstr(cmd, L"--speak")) Speak(0);
        if (cmd && wcsstr(cmd, L"--soon")) { dueAt = time(NULL) + 5; dueAction = ACT_REMIND; }
        if (cmd && wcsstr(cmd, L"--stats")) ShowPopup(P_STATS);

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (nid.hIcon) DestroyIcon(nid.hIcon);
    }
    GdiplusShutdown(tok);
    CoUninitialize();
    if (mtx) ReleaseMutex(mtx);
    return 0;
}
