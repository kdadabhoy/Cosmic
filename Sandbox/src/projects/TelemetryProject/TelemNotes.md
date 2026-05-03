## Telemetry & Chaos Engineering Project

This project is a high-speed telemetry system designed to monitor data from an ESP32/Arduino device while simultaneously testing the **resilience** of the data processing pipeline through "Chaos Engineering" principles.

---

### Purpose of the Project
The core objective is to move beyond simple data visualization and build a **robust receiver**. 

1.  **High-Speed Monitoring:** Processing and plotting numerical data (a 0–100 sine wave) in real-time with a rolling history of 1,000 points.
2.  **Chaos Testing:** Intentionally injecting "Noise Packets" and "Buffer Overflows" to ensure the C++ desktop application doesn't crash when it encounters malformed serial data.
3.  **Thread Stability:** Monitoring how the telemetry engine behaves on specific CPU cores (e.g., Core 10) to ensure high-priority data isn't blocked by UI rendering.

---

### The Arduino (ESP32) Script
The microcontroller acts as both a **signal generator** and a **troublemaker**. It transmits three distinct types of information over the Serial bus:

#### 1. The Clean Signal
The script calculates a value between **0 and 100** using a sine function. This simulates a real sensor reading (like temperature or pressure).
*   **Format:** `[RAW]: 87.37`
*   **Frequency:** Usually sent every 10–20ms.

#### 2. The Noise Packet
Periodically, the script stops sending numbers and sends a header: `--- NOISE_DATA_PACKET ---`. This is used as a delimiter to tell the desktop app that a "glitch" is occurring.

#### 3. The Error Simulation
Immediately following the noise header, the script sends `ERR: Buffer Overflow Simulation`. 
*   **The Intent:** This tests the C++ `try-catch` blocks and the string parser. It proves that the app can skip the text and successfully find the next valid number without losing its place in the stream.

---

### How it Works (Logic Flow)


1.  **ESP32** sends a mix of valid floats and "Buffer Overflow" text strings.
2.  **Desktop App** receives the raw string and logs everything to the **Serial Monitor**.
3.  **The Parser** looks for the last space in each line to extract the number.
4.  **The Filter** checks if the extracted string is actually a number. If it sees "ERR:" or "NOISE", it safely ignores it.
5.  **The Visualizer** updates the **Live Plot**, showing a smooth sine wave despite the "garbage" data being mixed in.

---

### Features
*   **Dynamic Scaling:** The plot can automatically add 10% padding to the top and bottom of the data so the line never hits the edge of the window.
*   **Robust Logging:** A high-speed, thread-safe log that handles up to 100,000 characters before recycling memory.
*   **Copy to Clipboard:** One-button export of the entire session log for external analysis.
*   **Connection Management:** Automatic port scanning and baud rate selection.










Use this ardunio script on the esp32:

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled!
#endif

BluetoothSerial SerialBT;

// Timing variable for the noise generator
uint32_t lastNoiseTime = 0;

void setup() {
  // USB Serial for local debugging
  Serial.begin(115200);
  
  // Bluetooth Serial for the TelemetryProject
  SerialBT.begin("ESP32_Stress_Tester"); 
  
  Serial.println("Telemetry Stress Test Started...");
  Serial.println("Connect your Cosmic Engine to 'ESP32_Stress_Tester'");
}

void loop() {
  // 1. Generate a sine wave (0.0 to 100.0)
  // Dividing millis by 500.0f creates a slower, smoother wave for visualization
  float time = millis() / 500.0f; 
  float val = sin(time) * 50.0f + 50.0f;

  // 2. Send valid data over Bluetooth
  // This is what your std::stof() parser is looking for
  SerialBT.println(val); 

  // 3. Robustness Test: Send noise every 50ms
  // Using a subtraction check is better than '%' to prevent multiple triggers per ms
  if (millis() - lastNoiseTime >= 50) {
    lastNoiseTime = millis();
    
    SerialBT.println("--- NOISE_DATA_PACKET ---");
    SerialBT.println("ERR: Buffer Overflow Simulation");
    
    // Optional: Log to USB so you know when noise was injected
    Serial.println(">> Injected Noise Packet");
  }

  // 4. Mirror to USB Serial for local verification
  Serial.println(val);

  // 5. THE STABILIZER
  // 1ms delay = ~1000 samples per second.
  // This prevents the Bluetooth buffer from saturating and dropping packets.
  delay(5); 
}





