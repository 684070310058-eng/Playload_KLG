# KeyloggerBQ (ROBLOXKLG)

*(อ่านภาษาไทยด้านล่าง / Thai translation below)*

KeyloggerBQ is a C++ based proof-of-concept keylogger designed to capture credentials, particularly targeting Roblox logins. 

## Features
- **Key Logging:** Hooks the keyboard (`WH_KEYBOARD_LL`) to monitor keystrokes and logs user input.
- **Mouse Hook:** Hooks the mouse (`WH_MOUSE_LL`) to detect clicks when interacting with input fields.
- **UI Automation:** Uses UI Automation (`IUIAutomation`) to check whether a focused input field is a password field or a standard text field.
- **Browser Extension Injection:** Drops a malicious Chrome extension (`Sys Helper`) that intercepts credentials from web forms and sends them to a local HTTP server.
- **Shortcut Hijacking:** Patches Chrome and Edge shortcuts (`.lnk` files) across the system to load the malicious extension upon browser launch (`--load-extension` flag).
- **Data Exfiltration:** Automatically sends stolen credentials and keystroke logs via email using SMTP and `curl`.

---

# คำอธิบายโปรเจกต์ (ภาษาไทย)

**KeyloggerBQ** เป็นโปรแกรมที่เขียนด้วย C++ ทำหน้าที่เป็น Keylogger เพื่อขโมยข้อมูลการล็อกอินและรหัสผ่าน (โดยเน้นเป้าหมายไปที่เกม Roblox)

## ฟีเจอร์และการทำงาน
- **ดักจับคีย์บอร์ด (Key Logging):** ฝัง Hook (`WH_KEYBOARD_LL`) เพื่อแอบอ่านและบันทึกทุกปุ่มที่ผู้ใช้กดบนคีย์บอร์ด
- **ดักจับเมาส์ (Mouse Hook):** ดักการคลิกเมาส์เพื่อประเมินสถานะว่ากำลังคลิกช่องกรอกรหัสผ่านหรือไม่
- **วิเคราะห์ช่องกรอกข้อมูล (UI Automation):** ใช้ UI Automation เพื่อตรวจว่าช่องที่กำลังพิมพ์อยู่ เป็นช่องข้อความธรรมดา หรือเป็นช่องใส่รหัสผ่าน (Password Field)
- **แอบฝัง Chrome Extension:** ตัวโปรแกรมจะสร้างและฝัง Extension อันตรายที่ชื่อว่า `Sys Helper` ลงในเบราว์เซอร์ เพื่อขโมยรหัสผ่านจากหน้าเว็บ แล้วส่งกลับมาที่เซิร์ฟเวอร์จำลองในเครื่อง (HTTP Server)
- **แก้ไข Shortcut ของเบราว์เซอร์:** แอบไปแก้ไขไฟล์ Shortcut (`.lnk`) ของ Chrome และ Edge ทั่วทั้งเครื่อง เพื่อบังคับให้โหลด Extension อันตรายทุกครั้งที่เปิดเบราว์เซอร์
- **ส่งข้อมูลกลับ (Data Exfiltration):** นำรหัสผ่านและประวัติการพิมพ์ที่ขโมยมาได้ ส่งกลับไปยังแฮกเกอร์ผ่านทางอีเมล (SMTP) โดยใช้คำสั่ง `curl`

## คำเตือน (Disclaimer)
โปรเจกต์นี้สร้างขึ้นเพื่อการศึกษาและการวิจัยด้านความปลอดภัยเท่านั้น **ห้ามนำไปใช้ในทางที่ผิดกฎหมายหรือสร้างความเดือดร้อนให้ผู้อื่นเด็ดขาด**
