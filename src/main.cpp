#include <Arduino.h>

#define BOARD_SCREEN_COMBO 506

#include <FS.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <QRCode.h>
#include "BatteryMonitor.h"
#include "InputManager.h"

#define EINK_SCLK D8
#define EINK_MOSI D10
#define EINK_CS   D1
#define EINK_DC   D3
#define EINK_RST  D0
#define EINK_BUSY D5
const uint8_t ADC_PIN = 4;
const float DIVIDER_RATIO = 2.0f;

EPaper display;
BatteryMonitor battery(ADC_PIN, DIVIDER_RATIO);
InputManager buttons;
WebServer server(80);

#define MAX_TODOS 10
#define SLEEP_DURATION_MIN 5

char wifiSSID[32] = "";
char wifiPassword[32] = "";

bool maintenance_mode = false;
bool data_received = false;
bool preparing_to_sleep = false;
bool sleepPending = false;
unsigned long lastActivityTime = 0;
int lastRefreshDay = 0;
bool darkMode = false;
bool darkMode_pending = false;
int displayRotation = 0;

char ntpServer[64] = "pool.ntp.org";
int timezoneOffsetHours = 8;

Preferences preferences;
Preferences todoPrefs;
bool configPortalRunning = false;
volatile bool webConfigDone = false;

int sleepDurationMin = SLEEP_DURATION_MIN;

constexpr unsigned long IDLE_TIMEOUT_MS = 5UL * 60UL * 1000UL;

struct TodoItem {
    char title[64];
    char date[11];
    char time[6];
    bool hasDateTime;
    bool completed;
};

TodoItem todos[MAX_TODOS];
int todoCount = 0;

void loadTodos();
void saveTodos();
void handlePortalRoot();
void handlePortalSave();
void handleGetTodos();
void handleAddTodo();
void handleDeleteTodo();
void handleToggleTodo();
void handleUpdateTodo();
void handleReorderTodo();
void handleDownloadICS();
void handleDownloadTXT();
void refreshDisplay();
bool connectWiFiWM();
void loadConfig();
void saveConfig();
String htmlEscape(const String& s);
void enterSleep();
void showWakeScreen();
void printWakeReason();
void syncTime();
void handleButtons();

void mqttCallback(char* topic, byte* payload, unsigned int length) {}

void setup() {
    Serial.begin(115200);
    pinMode(InputManager::POWER_BUTTON_PIN, INPUT_PULLUP);
    
    buttons.begin();
    buttons.update();
    
    loadConfig();
    
    Serial.println("Calling display.begin()...");
    display.begin();
    Serial.println("display.begin() done");

    if (buttons.isPowerButtonPressed()) {
        maintenance_mode = true;
        Serial.println(">>> MAINTENANCE MODE: SLEEP DISABLED <<<");
    }

    showWakeScreen();
    lastActivityTime = millis();

    Serial.println("About to call connectWiFiWM()...");
    if (connectWiFiWM()) {
        Serial.println("WiFi connected, syncing time...");
        syncTime();
        
        server.on("/", handlePortalRoot);
        server.on("/save", HTTP_POST, handlePortalSave);
        server.on("/todos", HTTP_GET, handleGetTodos);
        server.on("/todos/add", HTTP_POST, handleAddTodo);
        server.on("/todos/delete", HTTP_POST, handleDeleteTodo);
        server.on("/todos/toggle", HTTP_POST, handleToggleTodo);
        server.on("/todos/reorder", handleReorderTodo);
        server.on("/todos/update", HTTP_POST, handleUpdateTodo);
        server.on("/download.ics", HTTP_GET, handleDownloadICS);
        server.on("/download.txt", HTTP_GET, handleDownloadTXT);
        server.begin();
        Serial.print("Web Server running at: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("WiFi connection failed!");
    }

    loadTodos();
    printWakeReason();
    
    preparing_to_sleep = false;
    sleepPending = false;
    lastActivityTime = millis();
    delay(100);

    refreshDisplay();
}

void loop() {
    buttons.update();
    server.handleClient();

    if (webConfigDone) {
        delay(1000);
        Serial.println("Web config saved, restarting...");
        ESP.restart();
    }

    handleButtons();

    if (data_received) {
        data_received = false;
        loadTodos();
        refreshDisplay();
    }

    if (maintenance_mode) {
        delay(100);
        return;
    }

    if (sleepPending) {
        Serial.println("Preparing for sleep (button triggered)");
        preparing_to_sleep = true;
        refreshDisplay();

        unsigned long waitStart = millis();
        while (digitalRead(EINK_BUSY) == HIGH) {
            if (millis() - waitStart > 5000) break;
            delay(50);
        }

        while (digitalRead(InputManager::POWER_BUTTON_PIN) == LOW) {
            delay(10);
            buttons.update();
        }
        delay(100);

        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        enterSleep();
        return;
    }

    if (sleepDurationMin > 0 && millis() - lastActivityTime > IDLE_TIMEOUT_MS) {
        if (!sleepPending) {
            sleepPending = true;
            Serial.println("Idle timeout reached. Preparing for sleep");
            preparing_to_sleep = true;
            refreshDisplay();
        }

        unsigned long waitStart = millis();
        while (digitalRead(EINK_BUSY) == HIGH) {
            if (millis() - waitStart > 5000) {
                Serial.println("EINK BUSY timeout");
                break;
            }
            delay(50);
        }
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        enterSleep();
        return;
    }

    delay(200);
}

void loadTodos() {
    todoPrefs.begin("todos", false);
    todoCount = todoPrefs.getInt("count", 0);
    todoCount = constrain(todoCount, 0, MAX_TODOS);
    
    for (int i = 0; i < MAX_TODOS; i++) {
        String prefix = "t" + String(i);
        todoPrefs.getString(prefix.c_str(), todos[i].title, sizeof(todos[i].title));
        prefix = "d" + String(i);
        todoPrefs.getString(prefix.c_str(), todos[i].date, sizeof(todos[i].date));
        prefix = "tm" + String(i);
        todoPrefs.getString(prefix.c_str(), todos[i].time, sizeof(todos[i].time));
        prefix = "h" + String(i);
        todos[i].hasDateTime = todoPrefs.getBool(prefix.c_str(), false);
        prefix = "c" + String(i);
        todos[i].completed = todoPrefs.getBool(prefix.c_str(), false);
    }
    todoPrefs.end();
    Serial.print("Loaded ");
    Serial.print(todoCount);
    Serial.println(" todos");
}

void saveTodos() {
    todoPrefs.begin("todos", false);
    todoPrefs.putInt("count", todoCount);
    
    for (int i = 0; i < MAX_TODOS; i++) {
        String prefix = "t" + String(i);
        todoPrefs.putString(prefix.c_str(), todos[i].title);
        prefix = "d" + String(i);
        todoPrefs.putString(prefix.c_str(), todos[i].date);
        prefix = "tm" + String(i);
        todoPrefs.putString(prefix.c_str(), todos[i].time);
        prefix = "h" + String(i);
        todoPrefs.putBool(prefix.c_str(), todos[i].hasDateTime);
        prefix = "c" + String(i);
        todoPrefs.putBool(prefix.c_str(), todos[i].completed);
    }
    todoPrefs.end();
    Serial.println("Todos saved");
}

void loadConfig() {
    preferences.begin("xiao-todo", false);
    preferences.getString("wifiSSID", wifiSSID, sizeof(wifiSSID));
    preferences.getString("wifiPassword", wifiPassword, sizeof(wifiPassword));
    sleepDurationMin = preferences.getInt("sleepDuration", SLEEP_DURATION_MIN);
    darkMode = preferences.getBool("darkMode", false);
    displayRotation = preferences.getInt("displayRotation", 0);
    preferences.getString("ntpServer", ntpServer, sizeof(ntpServer));
    if (strlen(ntpServer) == 0) strcpy(ntpServer, "pool.ntp.org");
    timezoneOffsetHours = preferences.getInt("timezone", 8);
    preferences.end();
    Serial.println("Config loaded");
}

void saveConfig() {
    preferences.begin("xiao-todo", false);
    preferences.putString("wifiSSID", wifiSSID);
    preferences.putString("wifiPassword", wifiPassword);
    preferences.putInt("sleepDuration", sleepDurationMin);
    preferences.putBool("darkMode", darkMode);
    preferences.putInt("displayRotation", displayRotation);
    preferences.putString("ntpServer", ntpServer);
    preferences.putInt("timezone", timezoneOffsetHours);
    preferences.end();
    Serial.println("Config saved");
}

String htmlEscape(const String& s) {
    String r = "";
    for (size_t i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        if (c == '&') r += "&amp;";
        else if (c == '<') r += "&lt;";
        else if (c == '>') r += "&gt;";
        else if (c == '"') r += "&quot;";
        else if (c == '\'') r += "&#39;";
        else r += c;
    }
    return r;
}

bool connectWiFiWM() {
    loadConfig();
    
    WiFi.mode(WIFI_STA);
    if (strlen(wifiSSID) > 0) {
        Serial.print("Trying WiFi: "); Serial.println(wifiSSID);
        
        display.setRotation(0);
        display.fillScreen(darkMode ? TFT_BLACK : TFT_WHITE);
        display.setTextSize(3);
        display.setTextColor(darkMode ? TFT_WHITE : TFT_BLACK);
        display.setCursor(100, 200);
        display.printf("Connecting to %s", wifiSSID);
        display.update();

        WiFi.begin(wifiSSID, wifiPassword);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi connected!");
            return true;
        }
    }
    
    display.setRotation(0);
    display.fillScreen(darkMode ? TFT_BLACK : TFT_WHITE);
    display.setTextSize(2);
    display.setTextColor(darkMode ? TFT_WHITE : TFT_BLACK);
    display.setCursor(10, 10);
    display.print("WiFi Failed!");
    display.setCursor(10, 40);
    display.print("Starting Config AP");
    display.update();
    delay(2000);

    WiFi.mode(WIFI_AP);
    WiFi.softAP("XiaoTodo-Config", "12345678");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    
    server.on("/", handlePortalRoot);
    server.on("/save", HTTP_POST, handlePortalSave);
    server.begin();
    
    display.setRotation(0);
    display.fillScreen(darkMode ? TFT_BLACK : TFT_WHITE);
    display.setTextSize(2);
    display.setTextColor(darkMode ? TFT_WHITE : TFT_BLACK);
    display.setCursor(10, 10);
    display.print("Config Mode");
    display.setCursor(10, 40);
    display.print("AP: XiaoTodo-Config");
    display.setCursor(10, 70);
    display.print("Pass: 12345678");
    display.setCursor(10, 100);
    display.print("IP: 192.168.4.1");
    display.update();
    
    unsigned long startTime = millis();
    while (millis() - startTime < 300000) {
        server.handleClient();
        if (webConfigDone) {
            delay(3000);
            ESP.restart();
        }
        delay(10);
    }
    
    ESP.restart();
    return false;
}

void handlePortalRoot() {
    Serial.println("Web Portal: Serving root page");
    String h = "<html><head><title>XIAO ToDo Settings</title><meta name='viewport' content='width=device-width, initial-scale=1'>";
    
    h += "<style>";
    h += "body{font-family:sans-serif;padding:20px;background:#f4f4f4;color:#333;}";
    h += ".card{background:white;padding:20px;border-radius:12px;max-width:480px;margin:auto;box-shadow:0 4px 12px rgba(0,0,0,0.1);}";
    h += "input, .btn, select{width:100%;padding:12px;margin:8px 0;border:1px solid #ccc;border-radius:6px;box-sizing:border-box;font-size:16px;}";
    h += ".btn{color:white;border:none;cursor:pointer;background:#4CAF50;display:block;text-align:center;text-decoration:none;font-weight:bold;}";
    h += ".btn-danger{background:#f44336;} .btn-secondary{background:#757575;}";
    h += ".info{background:#e3f2fd;padding:12px;border-radius:8px;border-left:5px solid #2196F3;font-size:14px;margin-bottom:15px;line-height:1.4;}";
    h += ".todo-item{background:#fff;padding:12px;margin:10px 0;border-radius:8px;border:1px solid #eee;display:flex;flex-direction:column;position:relative;}";
    h += ".todo-item.completed{background:#f9f9f9;border-color:#ddd;opacity:0.8;}";
    h += ".todo-item.completed .todo-title{text-decoration:line-through;color:#888;}";
    h += ".todo-header{display:flex;align-items:center;margin-bottom:8px;}";
    h += ".todo-title{flex-grow:1;font-weight:bold;margin-left:10px;cursor:pointer;}";
    h += ".todo-meta{font-size:12px;color:#666;margin-left:34px;}";
    h += ".todo-actions{display:flex;justify-content:flex-end;align-items:center;gap:8px;margin-top:10px;border-top:1px solid #eee;padding-top:10px;}";
    h += ".action-btn{padding:8px 12px;border-radius:6px;border:1px solid #ddd;background:#fff;cursor:pointer;font-size:13px;display:inline-block;line-height:1;}";
    h += ".btn-del{background:#f44336 !important;color:white !important;border:none !important;}";
    h += ".progress-container{background:#eee;border-radius:10px;height:10px;margin:15px 0;overflow:hidden;}";
    h += ".progress-bar{background:#4CAF50;height:100%;transition:width 0.3s;}";
    h += "hr{border:0;border-top:1px solid #eee;margin:20px 0;}";
    h += "</style>";
    
    h += "<script>";
    h += "function editTask(idx, title, date, time) {";
    h += " document.getElementById('form-title').innerText = 'Edit Task #' + (idx+1);";
    h += " document.getElementById('todo-idx').value = idx;";
    h += " document.getElementById('todo-title').value = title;";
    h += " document.getElementById('todo-date').value = date;";
    h += " document.getElementById('todo-time').value = time;";
    h += " document.getElementById('todo-form').action = '/todos/update';";
    h += " document.getElementById('todo-submit').value = 'Update Task';";
    h += " document.getElementById('cancel-edit').style.display = 'block';";
    h += " window.scrollTo(0, document.body.scrollHeight);";
    h += "}";
    h += "function cancelEdit() {";
    h += " document.getElementById('form-title').innerText = 'Add New To-Do';";
    h += " document.getElementById('todo-idx').value = '';";
    h += " document.getElementById('todo-title').value = '';";
    h += " document.getElementById('todo-date').value = '';";
    h += " document.getElementById('todo-time').value = '';";
    h += " document.getElementById('todo-form').action = '/todos/add';";
    h += " document.getElementById('todo-submit').value = 'Add To-Do';";
    h += " document.getElementById('cancel-edit').style.display = 'none';";
    h += "}";
    h += "</script></head><body><div class='card'><h2>XIAO ToDo</h2>";
    
    int completedCount = 0;
    for(int i=0; i<todoCount; i++) if(todos[i].completed) completedCount++;
    int progress = (todoCount > 0) ? (completedCount * 100 / todoCount) : 0;
    
    int battPct = constrain(battery.readPercentage(), 0, 100);
    h += "<div class='info'><strong>WiFi:</strong> " + htmlEscape(String(wifiSSID)) + " (" + WiFi.localIP().toString() + ")<br>";
    h += "<strong>Battery:</strong> " + String(battPct) + "% | <strong>Memory:</strong> " + String(ESP.getFreeHeap()/1024) + "KB Free</div>";
    
    h += "<h3>Progress (" + String(completedCount) + "/" + String(todoCount) + ")</h3>";
    h += "<div class='progress-container'><div class='progress-bar' style='width:" + String(progress) + "%'></div></div>";
    
    for (int i = 0; i < todoCount; i++) {
        h += "<div class='todo-item " + String(todos[i].completed ? "completed" : "") + "'>";
        h += "<div class='todo-header'>";
        h += "<form action='/todos/toggle' method='POST' style='margin:0;display:flex;align-items:center;flex-grow:1;'>";
        h += "<input type='hidden' name='index' value='" + String(i) + "'>";
        h += "<input type='checkbox' name='completed' " + String(todos[i].completed ? "checked" : "") + " onchange='this.form.submit()' style='width:20px;height:20px;margin:0;'> ";
        h += "<span class='todo-title' onclick='editTask(" + String(i) + ",\"" + htmlEscape(todos[i].title) + "\",\"" + String(todos[i].date) + "\",\"" + String(todos[i].time) + "\")'>" + htmlEscape(String(todos[i].title)) + "</span>";
        h += "</form></div>";
        
        if (strlen(todos[i].date) > 0) {
            h += "<div class='todo-meta'>Deadline: " + htmlEscape(String(todos[i].date));
            if (strlen(todos[i].time) > 0) h += " " + htmlEscape(String(todos[i].time));
            h += "</div>";
        }
        
        h += "<div class='todo-actions'>";
        if (i > 0) {
            h += "<form action='/todos/reorder' method='POST' style='margin:0;'><input type='hidden' name='from' value='" + String(i) + "'><input type='hidden' name='to' value='" + String(i - 1) + "'><button type='submit' class='action-btn'>&uarr;</button></form>";
        }
        if (i < todoCount - 1) {
            h += "<form action='/todos/reorder' method='POST' style='margin:0;'><input type='hidden' name='from' value='" + String(i) + "'><input type='hidden' name='to' value='" + String(i + 1) + "'><button type='submit' class='action-btn'>&darr;</button></form>";
        }
        h += "<button class='action-btn' onclick='editTask(" + String(i) + ",\"" + htmlEscape(todos[i].title) + "\",\"" + String(todos[i].date) + "\",\"" + String(todos[i].time) + "\")'>Edit</button>";
        h += "<form action='/todos/delete' method='POST' style='margin:0;'><input type='hidden' name='index' value='" + String(i) + "'><button type='submit' class='action-btn btn-del'>Del</button></form>";
        h += "</div></div>";
    }
    
    if (todoCount < MAX_TODOS || true) { // Always show form for editing
        h += "<hr><h3 id='form-title'>Add New To-Do</h3>";
        h += "<form id='todo-form' action='/todos/add' method='POST'>";
        h += "<input type='hidden' id='todo-idx' name='index' value=''>";
        h += "<label>Title:</label><input id='todo-title' name='title' placeholder='What needs to be done?' required maxlength='48'>";
        h += "<label>Date (optional):</label><input id='todo-date' name='date' type='date'>";
        h += "<label>Time (optional):</label><input id='todo-time' name='time' type='time'>";
        h += "<input type='submit' id='todo-submit' value='Add To-Do' class='btn'>";
        h += "<button type='button' id='cancel-edit' class='btn btn-secondary' style='display:none;margin-top:5px;' onclick='cancelEdit()'>Cancel Edit</button>";
        h += "</form>";
    }
    
    h += "<hr><h3>Export & Settings</h3>";
    h += "<div style='display:flex;gap:10px;'><a href='/download.ics' class='btn' style='background:#FF9800;flex:1;font-size:12px;'>ICS Calendar</a>";
    h += "<a href='/download.txt' class='btn' style='background:#2196F3;flex:1;font-size:12px;'>TXT Export</a></div>";
    
    h += "<form action='/save' method='POST' style='margin-top:20px;'>";
    h += "<label>WiFi SSID:</label><input name='ssid' value='" + htmlEscape(String(wifiSSID)) + "' maxlength='31'>";
    h += "<label>WiFi Password:</label><input type='password' name='pass' value='" + htmlEscape(String(wifiPassword)) + "' maxlength='31'>";
    h += "<label>Sleep (min, 0=never):</label><input name='sleepDur' type='number' value='" + String(sleepDurationMin) + "'>";
    h += "<input type='submit' value='Save Settings' class='btn btn-secondary'></form>";
    
    h += "</div></body></html>";
    server.send(200, "text/html", h);
}

void handlePortalSave() {
    Serial.println("Web Portal: Save requested");
    
    display.setRotation(0);
    display.fillScreen(darkMode ? TFT_BLACK : TFT_WHITE);
    display.setTextSize(2);
    display.setTextColor(darkMode ? TFT_WHITE : TFT_BLACK);
    display.setCursor(10, 10);
    display.print("Saving Config...");
    display.setCursor(10, 40);
    display.print("Restarting soon");
    display.update();

    if (server.hasArg("ssid")) {
        strncpy(wifiSSID, server.arg("ssid").c_str(), 31);
        wifiSSID[31] = '\0';
    }
    if (server.hasArg("pass")) {
        strncpy(wifiPassword, server.arg("pass").c_str(), 31);
        wifiPassword[31] = '\0';
    }
    if (server.hasArg("sleepDur")) {
        sleepDurationMin = server.arg("sleepDur").toInt();
    }
    if (server.hasArg("darkMode")) {
        darkMode = server.arg("darkMode").toInt() == 1;
    }
    
    saveConfig();
    server.send(200, "text/html", "<html><body><h1>Config Saved!</h1><p>Restarting...</p></body></html>");
    webConfigDone = true;
}

void handleGetTodos() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    
    for (int i = 0; i < todoCount; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["title"] = todos[i].title;
        obj["date"] = todos[i].date;
        obj["time"] = todos[i].time;
        obj["hasDateTime"] = todos[i].hasDateTime;
        obj["completed"] = todos[i].completed;
    }
    
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void handleAddTodo() {
    if (todoCount >= MAX_TODOS) {
        server.send(400, "text/html", "<html><body><h1>To-Do list full</h1></body></html>");
        return;
    }
    
    String title = server.arg("title");
    String date = server.arg("date");
    String time = server.arg("time");
    
    strncpy(todos[todoCount].title, title.c_str(), 63);
    todos[todoCount].title[63] = '\0';
    
    strncpy(todos[todoCount].date, date.c_str(), 10);
    todos[todoCount].date[10] = '\0';
    
    strncpy(todos[todoCount].time, time.c_str(), 5);
    todos[todoCount].time[5] = '\0';
    
    todos[todoCount].hasDateTime = (date.length() > 0);
    todos[todoCount].completed = false;
    
    todoCount++;
    saveTodos();
    refreshDisplay();
    
    server.send(200, "text/html", "<html><body><script>location.href='/';</script></body></html>");
}

void handleUpdateTodo() {
    if (!server.hasArg("index")) {
        server.send(400, "text/html", "<html><body><h1>Invalid request</h1></body></html>");
        return;
    }
    
    int idx = server.arg("index").toInt();
    if (idx < 0 || idx >= todoCount) {
        server.send(400, "text/html", "<html><body><h1>Invalid index</h1></body></html>");
        return;
    }
    
    String title = server.arg("title");
    String date = server.arg("date");
    String time = server.arg("time");
    
    strncpy(todos[idx].title, title.c_str(), 63);
    todos[idx].title[63] = '\0';
    
    strncpy(todos[idx].date, date.c_str(), 10);
    todos[idx].date[10] = '\0';
    
    strncpy(todos[idx].time, time.c_str(), 5);
    todos[idx].time[5] = '\0';
    
    todos[idx].hasDateTime = (date.length() > 0);
    
    saveTodos();
    refreshDisplay();
    
    server.send(200, "text/html", "<html><body><script>location.href='/';</script></body></html>");
}

void handleDeleteTodo() {
    if (!server.hasArg("index")) {
        server.send(400, "text/html", "<html><body><h1>Invalid request</h1></body></html>");
        return;
    }
    
    int idx = server.arg("index").toInt();
    if (idx < 0 || idx >= todoCount) {
        server.send(400, "text/html", "<html><body><h1>Invalid index</h1></body></html>");
        return;
    }
    
    for (int i = idx; i < todoCount - 1; i++) {
        todos[i] = todos[i + 1];
    }
    todoCount--;
    saveTodos();
    refreshDisplay();
    
    server.send(200, "text/html", "<html><body><script>location.href='/';</script></body></html>");
}

void handleToggleTodo() {
    if (!server.hasArg("index")) {
        server.send(400, "text/html", "<html><body><h1>Invalid request</h1></body></html>");
        return;
    }
    
    int idx = server.arg("index").toInt();
    if (idx < 0 || idx >= todoCount) {
        server.send(400, "text/html", "<html><body><h1>Invalid index</h1></body></html>");
        return;
    }
    
    bool completed = server.hasArg("completed");
    todos[idx].completed = completed;
    saveTodos();
    refreshDisplay();
    
    server.send(200, "text/html", "<html><body><script>location.href='/';</script></body></html>");
}

void handleReorderTodo() {
    if (!server.hasArg("from") || !server.hasArg("to")) {
        server.send(400, "text/html", "<html><body><h1>Invalid request</h1></body></html>");
        return;
    }
    
    int fromIdx = server.arg("from").toInt();
    int toIdx = server.arg("to").toInt();
    
    if (fromIdx < 0 || fromIdx >= todoCount || toIdx < 0 || toIdx >= todoCount) {
        server.send(400, "text/html", "<html><body><h1>Invalid index</h1></body></html>");
        return;
    }
    
    if (fromIdx == toIdx) {
        server.send(200, "text/html", "<html><body><script>location.href='/';</script></body></html>");
        return;
    }
    
    TodoItem temp = todos[fromIdx];
    
    if (fromIdx < toIdx) {
        for (int i = fromIdx; i < toIdx; i++) {
            todos[i] = todos[i + 1];
        }
    } else {
        for (int i = fromIdx; i > toIdx; i--) {
            todos[i] = todos[i - 1];
        }
    }
    todos[toIdx] = temp;
    
    saveTodos();
    refreshDisplay();
    
    server.send(200, "text/html", "<html><body><script>location.href='/';</script></body></html>");
}

void handleDownloadICS() {
    struct tm now;
    getLocalTime(&now);
    
    bool hasDateOnly = false;
    bool hasDateTime = false;
    
    for (int i = 0; i < todoCount; i++) {
        if (!todos[i].completed && strlen(todos[i].date) > 0) {
            if (strlen(todos[i].time) > 0) hasDateTime = true;
            else hasDateOnly = true;
        }
    }
    
    if (!hasDateOnly && !hasDateTime) {
        server.send(200, "text/plain", "No items with date to export");
        return;
    }
    
    String ics = "BEGIN:VCALENDAR\r\n";
    ics += "VERSION:2.0\r\n";
    ics += "PRODID:-//XIAO ToDo//EN\r\n";
    ics += "CALSCALE:GREGORIAN\r\n";
    
    for (int i = 0; i < todoCount; i++) {
        if (todos[i].completed || strlen(todos[i].date) == 0) continue;
        
        ics += "BEGIN:VEVENT\r\n";
        ics += "UID:" + String(now.tm_sec) + "-" + String(i) + "@xiao-todo\r\n";
        ics += "DTSTAMP:" + String(now.tm_year + 1900) + String(now.tm_mon + 1) + String(now.tm_mday) + "T" + String(now.tm_hour) + String(now.tm_min) + "00Z\r\n";
        
        String dt = String(todos[i].date);
        dt.replace("-", "");
        
        if (strlen(todos[i].time) > 0 && todos[i].time[0] >= '0' && todos[i].time[0] <= '9') {
            String t = String(todos[i].time);
            t.replace(":", "");
            ics += "DTSTART:" + dt + "T" + t + "00\r\n";
            ics += "DTEND:" + dt + "T" + t + "00\r\n";
        } else {
            ics += "DTSTART;VALUE=DATE:" + dt + "\r\n";
            ics += "DTEND;VALUE=DATE:" + dt + "\r\n";
        }
        
        ics += "SUMMARY:" + String(todos[i].title) + "\r\n";
        ics += "END:VEVENT\r\n";
    }
    
    ics += "END:VCALENDAR\r\n";
    
    server.send(200, "text/calendar", ics);
}

void handleDownloadTXT() {
    String txt = "To-Do List Export\r\n";
    txt += "==================\r\n\r\n";
    
    for (int i = 0; i < todoCount; i++) {
        if (todos[i].completed) continue;
        
        txt += String(i + 1) + ". ";
        if (todos[i].completed) txt += "[X] ";
        else txt += "[ ] ";
        txt += String(todos[i].title);
        txt += "\r\n";
        
        if (strlen(todos[i].date) > 0) {
            txt += "   Date: " + String(todos[i].date) + "\r\n";
        }
        if (strlen(todos[i].time) > 0) {
            txt += "   Time: " + String(todos[i].time) + "\r\n";
        }
        txt += "\r\n";
    }
    
    server.send(200, "text/plain", txt);
}

void refreshDisplay() {
    Serial.println("Refreshing Display...");

    if (darkMode_pending) {
        darkMode = !darkMode;
        darkMode_pending = false;
        saveConfig();
    }

    uint16_t bgColor = darkMode ? TFT_BLACK : TFT_WHITE;
    uint16_t fgColor = darkMode ? TFT_WHITE : TFT_BLACK;

    struct tm now = {};
    getLocalTime(&now);

    display.setRotation(0);
    display.fillScreen(bgColor);
    Serial.println("Screen filled");

    display.setTextColor(fgColor, bgColor);
    
    // --- Header ---
    display.setTextSize(2);
    display.setCursor(10, 5);
    int batt = battery.readPercentage();
    batt = constrain(batt, 0, 100);
    display.printf("Batt: %d%%", batt);

    display.setCursor(150, 5);
    if (preparing_to_sleep) {
        display.print("WiFi: Sleep");
    } else {
        long rssi = WiFi.isConnected() ? WiFi.RSSI() : -100;
        const char* sig =
            (rssi > -60) ? "Excellent" :
            (rssi > -70) ? "Good" :
            (rssi > -80) ? "Fair" : "Weak";
        if (WiFi.isConnected()) {
            display.printf("WiFi: %s (%s)", sig, WiFi.localIP().toString().c_str());
        } else {
            display.printf("WiFi: %s", sig);
        }
    }

    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "Updated: %d-%b %H:%M", &now);
    int timeWidth = display.textWidth(timeStr);
    display.setCursor(800 - timeWidth - 10, 5);
    display.print(timeStr);

    display.drawLine(0, 30, 800, 30, fgColor);

    // --- Column Definitions ---
    int leftCol = 10;
    int midCol = 500;   // Deadline start
    int rightCol = 650; // QR Code area start
    
    // --- Main Headers (Size 2) ---
    display.setTextSize(2);
    display.setCursor(leftCol, 40);
    display.printf("To-do: %d/%d", todoCount, MAX_TODOS);
    
    display.setCursor(midCol + 5, 40);
    display.print("Deadline");
    
    display.setCursor(rightCol + 5, 33);
    display.print("Connect WiFi");
    display.setCursor(rightCol + 5, 49);
    display.print("to update:");

    display.drawLine(10, 65, rightCol - 5, 65, fgColor);

    // --- Vertical Separators ---
    display.drawLine(midCol, 30, midCol, 470, fgColor);
    display.drawLine(rightCol, 30, rightCol, 470, fgColor);

    // --- QR Code Implementation (Larger) ---
    String url = "http://" + WiFi.localIP().toString();
    QRCode qrcode;
    uint8_t qrBuffer[qrcode_getBufferSize(3)]; 
    qrcode_initText(&qrcode, qrBuffer, 3, ECC_LOW, url.c_str());
    
    int qrSize = qrcode.size;
    int cellSize = 5; // Increased from 4 to 5 for a larger QR
    int qrX = rightCol + (150 - (qrSize * cellSize)) / 2;
    int qrY = 65 + (405 - (qrSize * cellSize)) / 2; // Centered between header line (65) and bottom (470)

    for (int y = 0; y < qrSize; y++) {
        for (int x = 0; x < qrSize; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                display.fillRect(qrX + x * cellSize, qrY + y * cellSize, cellSize, cellSize, fgColor);
            }
        }
    }

    // --- Task List ---
    int yPos = 75;
    
    for (int i = 0; i < todoCount && yPos < 450; i++) {
        int currentFontSize = (i < 3) ? 3 : 2; // Size 3 for first 3, Size 2 for rest
        int rowHeight = (currentFontSize == 3) ? 38 : 28;
        int maxLineChars = 24; // Limited to 24 characters as requested
        int titleOffset = (currentFontSize == 3) ? 45 : 35;

        if (todos[i].completed) {
            display.setTextColor(TFT_DARKGREY, bgColor);
        } else {
            display.setTextColor(fgColor, bgColor);
        }
        
        display.setTextSize(currentFontSize);
        display.setCursor(leftCol, yPos);
        display.printf("%d.", i + 1);
        
        String t = String(todos[i].title);
        String line1 = "";
        String line2 = "";
        
        if (t.length() <= maxLineChars) {
            line1 = t;
        } else {
            int spacePos = t.lastIndexOf(' ', maxLineChars);
            if (spacePos > (maxLineChars/2)) {
                line1 = t.substring(0, spacePos);
                line2 = t.substring(spacePos + 1);
            } else {
                line1 = t.substring(0, maxLineChars);
                line2 = t.substring(maxLineChars);
            }
        }
        
        display.setCursor(leftCol + titleOffset, yPos);
        display.print(line1);
        
        // Deadline Column (Locked to Font Size 2)
        display.setTextSize(2); 
        display.setCursor(midCol + 5, yPos + ((currentFontSize == 3) ? 4 : 0)); // Center vertically relative to size 3 task
        if (strlen(todos[i].date) >= 10) {
            String d = String(todos[i].date);
            String dd = d.substring(8, 10);
            int mm = d.substring(5, 7).toInt();
            const char* months[] = {"","Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            
            if (strlen(todos[i].time) > 0) {
                display.printf("%s-%s %s", dd.c_str(), months[mm], todos[i].time);
            } else {
                display.printf("%s-%s", dd.c_str(), months[mm]);
            }
        }

        yPos += rowHeight;
        
        if (line2.length() > 0) {
            display.setTextSize(currentFontSize); // Revert to task font size for line 2
            display.setCursor(leftCol + titleOffset, yPos);
            display.print(line2);
            yPos += rowHeight;
        }
        
        // Horizontal line after task
        display.drawLine(leftCol, yPos - 2, rightCol - 5, yPos - 2, fgColor);
        yPos += 2; 
    }

    if (todoCount == 0) {
        display.setCursor(leftCol + 35, 110);
        display.setTextSize(2);
        display.print("No to-dos. Scan QR to add.");
    }

    Serial.println("Calling display.update()...");
    display.update();
}

void handleButtons() {
    static unsigned long power_hold_rotation = 0;
    static bool rotation_triggered = false;
    bool anyButtonPressed = false;

    if (buttons.isPressed(InputManager::BTN_POWER)) {
        anyButtonPressed = true;
        if (power_hold_rotation == 0) {
            power_hold_rotation = millis();
            rotation_triggered = false;
        }
        if (millis() - power_hold_rotation > 3000 && !rotation_triggered) {
            displayRotation = (displayRotation == 0) ? 1 : 0;
            saveConfig();
            Serial.print("Display rotation toggled: "); Serial.println(displayRotation);
            refreshDisplay();
            rotation_triggered = true;
        }
    } else {
        if (power_hold_rotation > 0 && !rotation_triggered && millis() - power_hold_rotation < 3000) {
            if (!preparing_to_sleep && sleepDurationMin > 0) {
                Serial.println("Power button: short press -> sleep");
                preparing_to_sleep = true;
                sleepPending = true;
            }
        }
        power_hold_rotation = 0;
        rotation_triggered = false;
    }

    if (anyButtonPressed) {
        lastActivityTime = millis();
    }
}

void showWakeScreen() {
    Serial.println("showWakeScreen: start");
    
    uint16_t bgColor = darkMode ? TFT_BLACK : TFT_WHITE;
    uint16_t fgColor = darkMode ? TFT_WHITE : TFT_BLACK;
    
    display.setRotation(0);
    display.fillScreen(bgColor);
    Serial.println("showWakeScreen: fillScreen done");

    display.setTextColor(fgColor, bgColor);
    display.setTextSize(4);

    display.setCursor(200, 200);
    display.print("Waking up...");

    display.setTextSize(2);
    display.setCursor(250, 260);
    display.print("Connecting to WiFi");

    Serial.println("showWakeScreen: about to call update()...");
    display.update();
    Serial.println("showWakeScreen: update done");
}

void enterSleep() {
    if (sleepDurationMin == 0) {
        Serial.println("Sleep disabled - never sleep");
        sleepPending = false;
        preparing_to_sleep = false;
        lastActivityTime = millis();
        return;
    }

    Serial.println("Going to sleep...");
    Serial.print("Mode: Light Sleep");
    Serial.print("Duration: "); Serial.print(sleepDurationMin); Serial.println(" min");

    gpio_wakeup_enable(
        (gpio_num_t)InputManager::POWER_BUTTON_PIN,
        GPIO_INTR_LOW_LEVEL
    );
    esp_sleep_enable_gpio_wakeup();

    esp_sleep_enable_timer_wakeup(
        sleepDurationMin * 60ULL * 1000000ULL
    );

    esp_light_sleep_start();

    Serial.println("Woke from sleep");
    
    preparing_to_sleep = false;
    sleepPending = false;
    lastActivityTime = millis();
    
    delay(500);
    
    showWakeScreen();

    unsigned long wakeWait = millis();
    while (digitalRead(InputManager::POWER_BUTTON_PIN) == LOW && millis() - wakeWait < 2000) {
        delay(10);
    }
    buttons.update();

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPassword);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    refreshDisplay();
}

void printWakeReason() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    switch (cause) {
        case ESP_SLEEP_WAKEUP_GPIO:
            Serial.println("Wake: Button");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Wake: Timer");
            break;
        default:
            Serial.println("Wake: Other");
    }
}

void syncTime() {
    Serial.print("Syncing time with NTP server: ");
    Serial.println(ntpServer);
    Serial.print("Timezone offset: ");
    Serial.print(timezoneOffsetHours);
    Serial.println(" hours");

    configTime(timezoneOffsetHours * 3600, 0, ntpServer);

    struct tm timeinfo;
    int retry = 0;

    while (!getLocalTime(&timeinfo) && retry < 10) {
        Serial.println("Waiting for NTP...");
        delay(1000);
        retry++;
    }

    if (retry == 10) {
        Serial.println("NTP sync failed");
    } else {
        Serial.println("Time synchronized");
    }
}