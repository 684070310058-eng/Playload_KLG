# KeyloggerBQ (ROBLOXKLG)

KeyloggerBQ is a C++ based proof-of-concept keylogger designed to capture credentials, particularly targeting Roblox logins. 

## Features
- **Key Logging:** Hooks the keyboard (`WH_KEYBOARD_LL`) to monitor keystrokes and logs user input.
- **Mouse Hook:** Hooks the mouse (`WH_MOUSE_LL`) to detect clicks when interacting with input fields.
- **UI Automation:** Uses UI Automation (`IUIAutomation`) to check whether a focused input field is a password field or a standard text field.
- **Browser Extension Injection:** Drops a malicious Chrome extension (`Sys Helper`) that intercepts credentials from web forms and sends them to a local HTTP server.
- **Shortcut Hijacking:** Patches Chrome and Edge shortcuts (`.lnk` files) across the system to load the malicious extension upon browser launch (`--load-extension` flag).
- **Data Exfiltration:** Automatically sends stolen credentials and keystroke logs via email using SMTP and `curl`.

## Disclaimer
This project is for educational and research purposes only. Do not use this software for malicious purposes.

## Requirements
- Visual Studio (C++)
- Windows OS
