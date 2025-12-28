#include <DFRobot_MAX30102.h>
#include <Wire.h>
#include <WiFiS3.h>    
#include <Arduino.h>

// Credentials for Hospital
// ==== WiFi credentials ====
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ==== SMTP server settings (ask hospital IT or use a provider that supports basic auth on 465) ====
const char* SMTP_HOST = "smtp.hospital-domain.org"; // e.g., "smtp.office365.com" (if they allow app passwords) or hospital relay
const uint16_t SMTP_PORT = 465;                     // Implicit SSL

// ==== SMTP login (username/password auth) ====
const char* SMTP_USER = "device-mailbox@hospital-domain.org";
const char* SMTP_PASS = "YOUR_SMTP_PASSWORD";

// ==== Email addresses ====
const char* FROM_EMAIL  = "device-mailbox@hospital-domain.org";
const char* FROM_NAME   = "Patient Sleep Monitor";
const char* TO_EMAIL    = "nurses.station@hospital-domain.org";

//////


//object storing variables for one sleep period
struct SleepLog {
  unsigned long start;     // milliseconds since the sleeping began
  unsigned long duration;  // duration in milliseconds
};

//constants
const int MAX_LOGS = 24;                      //capacity of sleeplog array which stores the different sleep periods
const int LOW_HR_THRESHOLD = 55;              //lower heart rate threshold (sleeping)
const int HIGH_HR_THRESHOLD = 65;             //above this = awake
const unsigned long MIN_SLEEP_TIME_MS = 30UL * 60UL * 1000UL;  // 1 hour

//Global Variables
DFRobot_MAX30102 particleSensor; // Allows us to call methods
SleepLog logs[MAX_LOGS];   // array that stores the different sleep periods
int logCount = 0;          // number of sleep periods that have been recorded
float totalSleepMin = 0;   // total sleep minutes for 1 day

//Oxiometer Variables
int32_t SPO2;           // oxygen saturation as a percent
int8_t SPO2Valid;       // tells you whether oxygen saturation is valid
int32_t heartRate;      // heart rate (BPM)
int8_t heartRateValid;  // tells you whether heart rate is valid

bool sleeping = false;     // current sleep state
unsigned long sleepStartTime = 0; // start time of sleep period since program execution


void setup() {
  Serial.begin(9600);
  connectWiFi();                    // move after Serial.begin()

  Wire.begin(); // I2C

  while (!particleSensor.begin()) {
    Serial.println("MAX30102 not found!");
    delay(1000);
  }

  particleSensor.sensorConfiguration(
    60, SAMPLEAVG_8, MODE_MULTILED, SAMPLERATE_400, PULSEWIDTH_411, ADCRANGE_16384
  ); // <-- semicolon

  Serial.println("Sleep Monitoring System Started");
  delay(1000);
}

// ---------------- MAIN LOOP ----------------
void loop() {
  detectSleepPeriod();

  // Send data every 24 hours to hospital
  //need to use this condition to ensure that data delay does not skip over send 
  if (((millis() % (1000UL * 60UL * 60UL * 24UL)) <= 5000) && millis() > 7000){
    sendDailyReport();
    checkSleepQuality();
  }

  delay(5000);  // check heart rate every 5 seconds
}

// ---------------- FUNCTIONS ----------------

// Read heart rate using oxiometer
int readHeartRate() {
  particleSensor.heartrateAndOxygenSaturation();
  Serial.print("Current heart rate: "); Serial.println(heartRate);
  return heartRate;
}


// Detect when sleep starts and ends
void detectSleepPeriod() {
  readHeartRate();

  if (heartRate < LOW_HR_THRESHOLD && !sleeping){
    sleeping = true;
    sleepStartTime = millis();
    Serial.println("Starting sleep timer");
  }

  //end sleep period if heart rate becomes higher than threshold
  if (heartRate > HIGH_HR_THRESHOLD && sleeping) {
    sleeping = false;
    //total program running time - time when sleep period began
    unsigned long duration = millis() - sleepStartTime;

    //if sleep duration is longer than minimum time then log it
    if (duration >= MIN_SLEEP_TIME_MS) {
      logSleepPeriod(sleepStartTime, duration);
      Serial.println("Sleep period logged.");
    } else {
      Serial.println("Sleep period too short, so not logged.");
    }
  }
}

// Log sleep period into memory
void logSleepPeriod(unsigned long start, unsigned long duration) {
  if (logCount < MAX_LOGS) {
    logs[logCount].start = start;
    logs[logCount].duration = duration;
    logCount++;
  } else {
    Serial.println("Log memory full. Sleep period not recorded.");
  }
}

//Send data to hospital staff
void sendDailyReport() {
  Serial.println("Daily Sleep Report");

  String body;
  body += "Daily Sleep Report\n";
  body += "=============================\n";

  if (logCount == 0) {
    Serial.println("No sleep records found.");
    body += "No sleep records found.\n";
  } else {
    for (int i = 0; i < logCount; i++) {
      // hours since program start
      float startHr = logs[i].start / 3600000.0;
      float durMin  = logs[i].duration / 60000.0;

      Serial.print("Period "); Serial.print(i + 1);
      Serial.print(": Start="); Serial.print(startHr, 2);
      Serial.print(" h since program execution, Duration=");
      Serial.print(durMin, 1); Serial.println(" min");

      body += "Period " + String(i + 1) +
              ": Start=" + String(startHr, 2) +
              " h since program execution, Duration=" +
              String(durMin, 1) + " min\n";
    }
  }
  body += "=============================\n";

  String subject = "Sleep Report - UNO R4 WiFi";

  bool ok = sendEmailSMTP_SSL(
      SMTP_HOST, SMTP_PORT,
      SMTP_USER, SMTP_PASS,
      FROM_EMAIL, FROM_NAME,
      TO_EMAIL,
      subject, body
  );
  if (!ok) {
    Serial.println("Failed to email daily report.");
  }
}


//Check if sleep quality is good
void checkSleepQuality() {
  totalSleepMin = 0;
  for (int i = 0; i < logCount; i++) {
    totalSleepMin += logs[i].duration / 60000.0;
  }

  float totalSleepHr = totalSleepMin / 60.0;
  if (totalSleepHr < 8.0){
    Serial.println("Patient had poor sleep (" + String(totalSleepHr, 1) + " hrs)");
  } else {
    Serial.println("Sleep quality OK (" + String(totalSleepHr, 1) + " hrs)");
  }
}



sketch_nov2a.ino
5 KB


// Code for Sending Board to mail

WiFiSSLClient smtpClient;

// Simple base64 encoder (small + synchronous)
const char b64_tbl[] PROGMEM =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(const String &in) {
  String out;
  int val = 0, valb = -6;
  for (uint8_t c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out += (char)pgm_read_byte(&b64_tbl[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out += (char)pgm_read_byte(&b64_tbl[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.length() % 4) out += '=';
  return out;
}

bool readLineUntil(WiFiSSLClient &c, String &line, uint32_t timeoutMs = 10000) {
  line = "";
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (c.available()) {
      char ch = (char)c.read();
      line += ch;
      if (line.endsWith("\r\n")) return true;
    }
    delay(1);
  }
  return false;
}

bool expectCode(WiFiSSLClient &c, const char* expected3Digits, uint32_t timeoutMs = 10000) {
  String line;
  bool gotAny = false;
  String last;
  // SMTP multi-line responses end with a line starting with "XYZ " (space, not hyphen)
  while (readLineUntil(c, line, timeoutMs)) {
    // Serial.print("S: "); Serial.print(line);  // enable for debug
    last = line;
    gotAny = true;
    if (line.length() >= 4 && isDigit(line[0]) && isDigit(line[1]) && isDigit(line[2])) {
      if (line[3] == ' ') break; // last line of a multiline reply
    }
  }
  if (!gotAny) return false;
  return last.startsWith(expected3Digits);
}

bool smtpWrite(WiFiSSLClient &c, const String &cmd) {
  // Serial.print("C: "); Serial.print(cmd);     // enable for debug
  return c.print(cmd) == (int)cmd.length();
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("Connecting to WiFi: "); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected. IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect FAILED");
  }
}

// Sends a simple text email over implicit SSL (port 465) using AUTH LOGIN.
// subject/body: plain text (use \n for newlines).
bool sendEmailSMTP_SSL(const char* host,
                       uint16_t port,
                       const char* user,
                       const char* pass,
                       const char* fromEmail,
                       const char* fromName,
                       const char* toEmail,
                       const String& subject,
                       const String& body)
{
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Email aborted: no WiFi.");
    return false;
  }

  Serial.print("Connecting to SMTP: "); Serial.print(host); Serial.print(':'); Serial.println(port);
  if (!smtpClient.connect(host, port)) {
    Serial.println("TLS connect failed.");
    return false;
  }

  // 220
  if (!expectCode(smtpClient, "220")) { Serial.println("No 220 greeting."); goto FAIL; }

  // EHLO
  smtpWrite(smtpClient, "EHLO uno-r4\r\n");
  if (!expectCode(smtpClient, "250")) { Serial.println("EHLO failed."); goto FAIL; }

  // AUTH LOGIN
  smtpWrite(smtpClient, "AUTH LOGIN\r\n");
  if (!expectCode(smtpClient, "334")) { Serial.println("AUTH LOGIN not accepted."); goto FAIL; }

  // Username (base64)
  smtpWrite(smtpClient, base64Encode(user) + "\r\n");
  if (!expectCode(smtpClient, "334")) { Serial.println("Username rejected."); goto FAIL; }

  // Password (base64)
  smtpWrite(smtpClient, base64Encode(pass) + "\r\n");
  if (!expectCode(smtpClient, "235")) { Serial.println("Password rejected."); goto FAIL; }

  // MAIL FROM
  smtpWrite(smtpClient, "MAIL FROM:<" + String(fromEmail) + ">\r\n");
  if (!expectCode(smtpClient, "250")) { Serial.println("MAIL FROM failed."); goto FAIL; }

  // RCPT TO
  smtpWrite(smtpClient, "RCPT TO:<" + String(toEmail) + ">\r\n");
  if (!expectCode(smtpClient, "250")) { Serial.println("RCPT TO failed."); goto FAIL; }

  // DATA
  smtpWrite(smtpClient, "DATA\r\n");
  if (!expectCode(smtpClient, "354")) { Serial.println("DATA not accepted."); goto FAIL; }

  // Headers + body (minimal)
  String msg;
  msg  = "From: " + String(fromName) + " <" + String(fromEmail) + ">\r\n";
  msg += "To: <" + String(toEmail) + ">\r\n";
  msg += "Subject: " + subject + "\r\n";
  msg += "MIME-Version: 1.0\r\n";
  msg += "Content-Type: text/plain; charset=utf-8\r\n";
  msg += "Content-Transfer-Encoding: 7bit\r\n";
  msg += "\r\n";
  msg += body;
  msg += "\r\n.\r\n";   // end of DATA

  if (!smtpWrite(smtpClient, msg)) { Serial.println("Write message failed."); goto FAIL; }
  if (!expectCode(smtpClient, "250")) { Serial.println("Server rejected DATA."); goto FAIL; }

  // QUIT
  smtpWrite(smtpClient, "QUIT\r\n");
  expectCode(smtpClient, "221"); // optional check

  smtpClient.stop();
  Serial.println("Email sent OK.");
  return true;

FAIL:
  smtpClient.stop();
  Serial.println("Email send FAILED.");
  return false;
}
