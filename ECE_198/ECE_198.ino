/*
 * ECE 198: Group 36 Sleep Monitor Project
 * Hardware: Arduino UNO R4 WiFi + Pulse Oximeter (MAX30102/MAX30100)
 * 
 * Logic Sources:
 * - Sleep detection based on HR thresholds (55/65 BPM) 
 * - Updates status every minute based on average HR 
 * - Sends alert when irregular heart rate pattern is detected
 */

#include <WiFiS3.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// --- WiFi Credentials ---
const char* ssid = "YOUR_WIFI_SSID";        
const char* password = "YOUR_WIFI_PASSWORD"; 

// --- Email Configuration ---
const char* emailSender = "michaelshi095@gmail.com";
const char* emailRecipient = "caregiver@example.com"; 
const char* smtpServer = "mail.smtp2go.com";  
const int smtpPort = 2525;

// --- Global Variables & Constants ---
MAX30105 particleSensor;

const int THRESHOLD_SLEEP_START = 55;
const int THRESHOLD_SLEEP_END = 65;

const unsigned long UPDATE_INTERVAL = 60000;     

unsigned long lastUpdateTimer = 0;
unsigned long projectStartTime = 0;

// Heart Rate Detection Variables
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;

// Data Averaging Variables
long hrSum = 0;
int hrSampleCount = 0;
int lastValidBPM = 0;

// Heart Rate Pattern Tracking for Irregularity Detection
const int HR_HISTORY_SIZE = 10;  
int hrHistory[HR_HISTORY_SIZE];
int hrHistoryIndex = 0;
int hrHistoryCount = 0;
bool lastAlertSent = false;

// Sleep State Tracking
bool isSleeping = false;
float currentSleepStart = 0;

// Data Storage
struct SleepPeriod {
  float startTime;
  float duration;
};

SleepPeriod sleepHistory[50];
int sleepIndex = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== Sleep Monitor Starting ===");

  // Initialize Sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("ERROR: MAX30105 not found. Check wiring!");
    while (1);
  }
  
  Serial.println("Sensor initialized successfully");
  
  // Configure sensor for heart rate detection
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  // Initialize WiFi
  connectToWiFi();

  projectStartTime = millis();
  lastUpdateTimer = millis();
  
  Serial.println("Setup complete. Monitoring heart rate...");
}

void loop() {
  // Continuously read sensor data
  long irValue = particleSensor.getIR();
  
  // Check if finger is on sensor (IR value above threshold)
  if (irValue > 50000) {
    
    // Detect heartbeat using the library function
    if (checkForBeat(irValue) == true) {
      // Calculate time between beats
      long delta = millis() - lastBeat;
      lastBeat = millis();

      // Calculate BPM from time between beats
      int beatsPerMinute = 60 / (delta / 1000.0);

      // Filter out unrealistic values (sensor noise/errors)
      if (beatsPerMinute > 30 && beatsPerMinute < 220) {
        // Store in circular buffer for averaging
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;

        // Calculate average of last few readings
        int beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) {
          beatAvg += rates[x];
        }
        beatAvg /= RATE_SIZE;

        // Add to minute accumulator
        hrSum += beatAvg;
        hrSampleCount++;
        lastValidBPM = beatAvg;

        // Optional: Print current reading
        Serial.print("BPM: ");
        Serial.print(beatsPerMinute);
        Serial.print(" | Avg: ");
        Serial.println(beatAvg);
      }
    }
  } else {
    Serial.println("No finger detected. Place finger on sensor.");
    delay(1000);
  }

  // Process logic every minute
  if (millis() - lastUpdateTimer > UPDATE_INTERVAL) {
    processMinuteLogic();
    lastUpdateTimer = millis();
  }
}

void processMinuteLogic() {
  // Use last valid reading if no samples this minute
  int averageHR = (hrSampleCount > 0) ? (hrSum / hrSampleCount) : lastValidBPM;
  
  Serial.println("\n--- Minute Update ---");
  Serial.print("Average HR: ");
  Serial.print(averageHR);
  Serial.println(" BPM");

  // Reset counters
  hrSum = 0;
  hrSampleCount = 0;

  // Store in history for pattern analysis
  if (averageHR > 0) {
    hrHistory[hrHistoryIndex] = averageHR;
    hrHistoryIndex = (hrHistoryIndex + 1) % HR_HISTORY_SIZE;
    if (hrHistoryCount < HR_HISTORY_SIZE) hrHistoryCount++;
  }

  // Check for irregular patterns
  checkForIrregularities(averageHR);

  // Check sleep thresholds
  if (!isSleeping && averageHR > 0 && averageHR < THRESHOLD_SLEEP_START) {
    startSleepPeriod();
  }
  else if (isSleeping && averageHR > THRESHOLD_SLEEP_END) {
    endSleepPeriod();
  }
  
  Serial.print("Status: ");
  Serial.println(isSleeping ? "SLEEPING" : "AWAKE");
  Serial.println("--------------------\n");
}

void startSleepPeriod() {
  isSleeping = true;
  currentSleepStart = (float)(millis() - projectStartTime) / 3600000.0;
  
  Serial.println(">>> SLEEP PERIOD STARTED <<<");
}

void endSleepPeriod() {
  isSleeping = false;
  float endTime = (float)(millis() - projectStartTime) / 3600000.0;
  float durationHours = endTime - currentSleepStart;
  
  // Store in array
  if (sleepIndex < 50) {
    sleepHistory[sleepIndex].startTime = currentSleepStart;
    sleepHistory[sleepIndex].duration = durationHours * 60; // minutes
    sleepIndex++;
    
    Serial.println(">>> SLEEP PERIOD ENDED <<<");
    Serial.print("Duration: ");
    Serial.print(durationHours * 60);
    Serial.println(" minutes");
  }
}

void checkForIrregularities(int currentHR) {
  // Need at least 5 minutes of data to detect patterns
  if (hrHistoryCount < 5) {
    return;
  }

  bool irregularDetected = false;
  String alertReason = "";

  // 1. Check for dangerously high heart rate
  if (currentHR > 120 && !isSleeping) {
    irregularDetected = true;
    alertReason = "VERY HIGH heart rate detected: " + String(currentHR) + " BPM while awake";
  }
  else if (currentHR > 90 && isSleeping) {
    irregularDetected = true;
    alertReason = "ELEVATED heart rate during sleep: " + String(currentHR) + " BPM";
  }

  // 2. Check for dangerously low heart rate
  if (currentHR < 40 && currentHR > 0) {
    irregularDetected = true;
    alertReason = "CRITICALLY LOW heart rate: " + String(currentHR) + " BPM";
  }

  // 3. Check for high variability (erratic pattern)
  // Calculate standard deviation of last 10 minutes
  if (hrHistoryCount >= HR_HISTORY_SIZE) {
    float mean = 0;
    for (int i = 0; i < HR_HISTORY_SIZE; i++) {
      mean += hrHistory[i];
    }
    mean /= HR_HISTORY_SIZE;

    float variance = 0;
    for (int i = 0; i < HR_HISTORY_SIZE; i++) {
      float diff = hrHistory[i] - mean;
      variance += diff * diff;
    }
    variance /= HR_HISTORY_SIZE;
    float stdDev = sqrt(variance);

    // If standard deviation is high, heart rate is very erratic
    if (stdDev > 15.0) {
      irregularDetected = true;
      alertReason = "ERRATIC heart rate pattern detected (StdDev: " + String(stdDev, 1) + ")";
    }
  }

  // 4. Check for sudden large changes (>30 BPM jump)
  if (hrHistoryCount >= 2) {
    int prevIndex = (hrHistoryIndex - 2 + HR_HISTORY_SIZE) % HR_HISTORY_SIZE;
    int hrChange = abs(currentHR - hrHistory[prevIndex]);
    
    if (hrChange > 30) {
      irregularDetected = true;
      alertReason = "SUDDEN heart rate change: " + String(hrChange) + " BPM in 1 minute";
    }
  }

  // Send alert if irregularity detected and we haven't sent one recently
  if (irregularDetected && !lastAlertSent) {
    sendIrregularityAlert(alertReason, currentHR);
    lastAlertSent = true;
  }
  else if (!irregularDetected && lastAlertSent) {
    // Reset alert flag when pattern normalizes
    lastAlertSent = false;
    Serial.println("Heart rate pattern normalized.");
  }
}

void sendIrregularityAlert(String reason, int currentHR) {
  Serial.println("\n\n!!! IRREGULAR PATTERN DETECTED !!!");
  Serial.println("====================================");
  Serial.println(reason);
  
  String emailBody = "⚠️ SLEEP MONITOR ALERT ⚠️\n";
  emailBody += "===========================\n\n";
  emailBody += "An irregular heart rate pattern has been detected:\n\n";
  emailBody += "ALERT REASON: " + reason + "\n\n";
  
  emailBody += "Current Heart Rate: " + String(currentHR) + " BPM\n";
  emailBody += "Status: " + String(isSleeping ? "SLEEPING" : "AWAKE") + "\n";
  emailBody += "Time: " + String((millis() - projectStartTime) / 3600000.0, 2) + " hours since device start\n\n";
  
  // Include recent history
  emailBody += "Recent Heart Rate History (last 10 minutes):\n";
  emailBody += "--------------------------------------------\n";
  for (int i = 0; i < hrHistoryCount; i++) {
    int index = (hrHistoryIndex - hrHistoryCount + i + HR_HISTORY_SIZE) % HR_HISTORY_SIZE;
    emailBody += "  " + String(i + 1) + " min ago: " + String(hrHistory[index]) + " BPM\n";
  }
  
  emailBody += "\nRECOMMENDATION: Please check on the monitored individual.\n";
  emailBody += "\n--- End of Alert ---\n";

  // Print to Serial
  Serial.println(emailBody);

  // Send via email
  if (WiFi.status() == WL_CONNECTED) {
    sendEmail(emailBody, true);  // true = this is an alert
  } else {
    Serial.println("ERROR: WiFi disconnected. Cannot send alert.");
    connectToWiFi();
  }
  
  Serial.println("====================================\n\n");
}

void sendDailyReport() {
  Serial.println("\n\n========================================");
  Serial.println("     GENERATING DAILY SLEEP REPORT");
  Serial.println("========================================\n");
  
  String emailBody = "DAILY SLEEP REPORT\n";
  emailBody += "==================\n\n";
  
  float totalSleepTimeHours = 0;

  if (sleepIndex == 0) {
    emailBody += "No sleep periods recorded in the past 24 hours.\n\n";
  } else {
    emailBody += "Sleep Periods:\n";
    emailBody += "--------------\n";
    
    for (int i = 0; i < sleepIndex; i++) {
      emailBody += "Period " + String(i + 1) + ":\n";
      emailBody += "  Start: " + String(sleepHistory[i].startTime, 2) + " hours since device start\n";
      emailBody += "  Duration: " + String(sleepHistory[i].duration, 1) + " minutes\n\n";
      
      totalSleepTimeHours += (sleepHistory[i].duration / 60.0);
    }
  }

  emailBody += "Summary:\n";
  emailBody += "--------\n";
  emailBody += "Total Sleep Time: " + String(totalSleepTimeHours, 2) + " hours\n\n";

  emailBody += "Sleep Quality Assessment: ";
  if (totalSleepTimeHours < 6.0) {
    emailBody += "POOR (Less than 6 hours - Insufficient)\n";
  } else if (totalSleepTimeHours < 8.0) {
    emailBody += "FAIR (6-8 hours - Below recommended)\n";
  } else if (totalSleepTimeHours <= 9.0) {
    emailBody += "GOOD (8-9 hours - Recommended range)\n";
  } else {
    emailBody += "EXCESSIVE (Over 9 hours)\n";
  }

  emailBody += "\nRecommendation: Aim for 7-9 hours of sleep per night.\n";
  emailBody += "\n--- End of Report ---\n";

  // Print to Serial
  Serial.println(emailBody);

  // Send via email
  if (WiFi.status() == WL_CONNECTED) {
    sendEmail(emailBody);
  } else {
    Serial.println("ERROR: WiFi disconnected. Cannot send email.");
    connectToWiFi();
  }

  // Clear data for next period
  sleepIndex = 0;
  
  Serial.println("========================================\n\n");
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Connection Failed!");
  }
}

void sendEmail(String body, bool isAlert = false) {
  if (isAlert) {
    Serial.println("Attempting to send ALERT email...");
  } else {
    Serial.println("Attempting to send email...");
  }
  
  WiFiClient client;
  
  if (!client.connect(smtpServer, smtpPort)) {
    Serial.println("ERROR: Could not connect to SMTP server");
    return;
  }
  
  Serial.println("Connected to SMTP server");
  
  // Wait for server greeting
  delay(1000);
  
  // SMTP conversation
  client.println("HELO arduino");
  delay(500);
  
  client.println("MAIL FROM: <" + String(emailSender) + ">");
  delay(500);
  
  client.println("RCPT TO: <" + String(emailRecipient) + ">");
  delay(500);
  
  client.println("DATA");
  delay(500);
  
  // Email headers
  client.println("From: Sleep Monitor <" + String(emailSender) + ">");
  client.println("To: <" + String(emailRecipient) + ">");
  
  if (isAlert) {
    client.println("Subject: ⚠️ URGENT: Irregular Heart Rate Alert");
    client.println("Importance: high");
  } else {
    client.println("Subject: Sleep Monitor Report");
  }
  
  client.println("Content-Type: text/plain; charset=UTF-8");
  client.println();
  
  // Email body
  client.println(body);
  
  // End of message
  client.println(".");
  delay(500);
  
  client.println("QUIT");
  client.stop();
  
  if (isAlert) {
    Serial.println("ALERT email sent successfully!");
  } else {
    Serial.println("Email sent successfully!");
  }
}