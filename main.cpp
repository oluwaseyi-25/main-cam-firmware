#define TEST_MODE
#define ERROR_LOGGING

#include "main.h"

void setup()
{
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Initialize Serial Monitor
  Serial.begin(921600);
  while (!Serial)
    ; // Wait for Serial Monitor to open
  LOG("Serial Monitor started at 921600 baud.");

  // Initialize SPIFFS
  SPIFFS.begin(true);
  if (!SPIFFS.begin(true))
  {
    LOG_ERR("An Error has occurred while mounting SPIFFS");
  }
  LOG("SPIFFS mounted successfully.");

  // Load configuration from SPIFFS
  if (!loadConfig())
  {
    LOG_ERR("Failed to load configuration. Using default values.");
    ssid = "wifi_ssid";
    password = "password";
  }

  if (connect_to_network())
    LOGF("Connected to WiFi. IP address: %s\n", WiFi.localIP().toString().c_str());

  camera_init();

  webSocket.begin("192.168.0.200", 5000, "/command");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(15000, 3000, 2); // Enable heartbeat with 15s ping interval, 3s pong timeout, and 2 disconnect attempts

  pinMode(LED_GPIO_NUM, OUTPUT);
  analogWrite(LED_GPIO_NUM, 100); // Turn off the LED initially
  delay(500); // Wait for 1 second before starting the loop
  analogWrite(LED_GPIO_NUM, 0); // Turn on the LED to indicate that the setup is complete
}

void loop()
{
  // listen for incoming commands on serial port
  if (Serial.available())
  {
    String command = Serial.readStringUntil('\n');
    Serial.printf("\nReceived command: %s\n", command.c_str());
    // Confirm if the command is a valid json string
    JSONVar json = JSON.parse(command);
    if (JSON.typeof(json) == "undefined")
    {
      Serial.println("Parsing input failed!");
    }
    else
    {
      Serial.println(JSON.stringify(
                             cmdResponseToJSON(
                                 exec_cmd(json)))
                         .c_str());
    }
  }
  webSocket.loop();
}

/**
 * @brief Reads a file from the SPIFFS file system.
 *
 * This function opens a file for reading and reads its content into a string.
 * It returns the content of the file as a string.
 *
 * @param fs The file system object (SPIFFS).
 * @param path The path of the file to read.
 * @return String The content of the file as a string.
 */
String readFile(fs::FS &fs, const char *path)
{
  static String file_content;
  LOGF("Reading file: %s\r\n", path);

  fs::File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    LOG_ERR("- failed to open file for reading");
    return String("");
  }

  LOG("- read from file:");
  char read_char;
  while (file.available())
  {
    read_char = file.read();
    file_content += read_char;
  }
  file.close();
  return file_content;
}

/**
 * @brief Writes a message to a file in the SPIFFS file system.
 *
 * This function opens a file for writing and writes the provided message to it.
 * It creates the file if it does not exist.
 *
 * @param fs The file system object (SPIFFS).
 * @param path The path of the file to write to.
 * @param message The message to write to the file.
 * @return true if the write operation was successful, false otherwise.
 */
bool writeFile(fs::FS &fs, const char *path, const char *message)
{
  LOGF("Writing file: %s\r\n", path);

  fs::File file = fs.open(path, FILE_WRITE, true);
  if (!file)
  {
    LOG_ERR("- failed to open file for writing");
    return false;
  }
  if (!file.print(message))
  {
    LOG_ERR("- write failed");
    file.close();
    return false;
  }
  LOG("- file written\n");
  file.close();
  return true;
}

/**
 * @brief Loads the configuration from the SPIFFS file system.
 *
 * This function reads the configuration file and parses it as JSON.
 * It extracts the SSID and password from the JSON object.
 *
 * @return true if the configuration was loaded successfully, false otherwise.
 */
bool loadConfig()
{
  String fileContent = readFile(SPIFFS, "/config.json");

  if (fileContent.isEmpty())
    return false;

  Serial.println(fileContent.c_str());
  JSONVar json = JSON.parse(fileContent);
  if (JSON.typeof(json) == "undefined")
  {
    Serial.println("Parsing input failed!");
    return false;
  }

  Serial.println("JSON parsed successfully.");
  ssid = (const char *)json["ssid"];
  password = (const char *)json["password"];
  Serial.printf("SSID: %s", ssid.c_str());
  Serial.printf("\t Password: %s\n", password.c_str());
  return true;
}

/**
 * @brief Maps an opcode string to a function pointer.
 *
 * This function takes an opcode string and returns a function pointer
 * to the corresponding operation function.
 *
 * @param opcode The opcode string representing the operation.
 * @return OpPtr A function pointer to the corresponding operation function.
 */
OpPtr opcodeToFunc(String opcode)
{
  OpPtr ret;
  if (opcode == "change_wifi")
    ret = change_wifi;
  else if (opcode == "take_photo")
    ret = take_photo;
  else if (opcode == "test")
    ret = [](CMD_INPUT cmd_input) -> CMD_RESPONSE
    {
      CMD_RESPONSE ret = {"OK", "Test command executed successfully"};
      return ret;
    };
  else if (opcode == "diagnostics")
    ret = [](CMD_INPUT cmd_input) -> CMD_RESPONSE
    {
      CMD_RESPONSE ret = {"OK", "Diagnostics command executed successfully"};
      // Return a report of the current state of the system
      String wifiStatus = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
      String ipAddress = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "N/A";

      String cameraStatus = (esp_camera_sensor_get() != nullptr) ? "Initialized" : "Not Initialized";

      // Get memory stats
      uint32_t freeHeap = ESP.getFreeHeap();
      uint32_t totalHeap = ESP.getHeapSize();
      uint32_t usedHeap = totalHeap - freeHeap;

      // Check for PSRAM availability
      String psramStatus = psramFound() ? "Available" : "Not Available";
      uint32_t psramSize = psramFound() ? ESP.getPsramSize() : 0;
      uint32_t freePsram = psramFound() ? ESP.getFreePsram() : 0;
      uint32_t usedPsram = psramSize - freePsram;

      ret.body = "Camera Diagnostics Report:\n";
      ret.body += "WiFi Status: " + wifiStatus + "\n";
      ret.body += "IP Address: " + ipAddress + "\n";
      ret.body += "Camera Status: " + cameraStatus + "\n";
      ret.body += "Memory Stats:\n";
      ret.body += "  Total Heap: " + String(totalHeap) + " bytes\n";
      ret.body += "  Used Heap: " + String(usedHeap) + " bytes\n";
      ret.body += "  Free Heap: " + String(freeHeap) + " bytes\n";
      ret.body += "PSRAM Stats:\n";
      ret.body += "  PSRAM Status: " + psramStatus + "\n";
      if (psramFound())
      {
        ret.body += "  Total PSRAM: " + String(psramSize) + " bytes\n";
        ret.body += "  Used PSRAM: " + String(usedPsram) + " bytes\n";
        ret.body += "  Free PSRAM: " + String(freePsram) + " bytes\n";
      }
      return ret;
    };
  else
    ret = [](CMD_INPUT cmd_input) -> CMD_RESPONSE
    {
      CMD_RESPONSE ret = {"ERR", "Operation not supported yet"};
      return ret;
    };
  return ret;
}

/**
 * @brief Executes a command based on the provided JSON input.
 *
 * This function parses the command and its arguments from the JSON object,
 * and calls the appropriate function to execute the command.
 *
 * @param cmd The JSON object containing the command and its arguments.
 * @return CMD_RESPONSE A response object indicating the status and result of the operation.
 */
CMD_RESPONSE exec_cmd(JSONVar cmd)
{
  CMD_INPUT cmd_input = {(JSONVar)cmd["args"]};
  return opcodeToFunc(cmd["cmd"])(cmd_input);
}

/**
 * @brief Converts a CMD_RESPONSE object to a JSON object.
 *
 * This function takes a CMD_RESPONSE object and converts it to a JSON object
 * with the status and body fields.
 *
 * @param response The CMD_RESPONSE object to convert.
 * @return JSONVar A JSON object representing the CMD_RESPONSE.
 */
JSONVar cmdResponseToJSON(CMD_RESPONSE response)
{
  JSONVar ret;
  ret["cmd_response_status"] = response.status;
  ret["cmd_response_body"] = response.body;
  return ret;
}

/**
 * @brief Connects to the specified WiFi network.
 *
 * This function attempts to connect to the WiFi network using the provided SSID and password.
 * It waits for a maximum of 10 seconds for the connection to be established.
 *
 * @return true if the connection was successful, false otherwise.
 */
bool connect_to_network()
{
  // Connect to WiFi network
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("Connecting to WiFi...");
  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - startAttemptTime > 10000) // 10 seconds timeout
    {
      Serial.println("WiFi connection timed out...\nCheck your credentials...");
      return false;
    }
    Serial.print(".");
    delay(500);
    yield(); // Allow other tasks to run
  }
  Serial.printf("\nConnected to WiFi network. IP address: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

/**
 * @brief Initializes the ESP32 camera module.
 *
 * This function configures and initializes the ESP32 camera module with specific settings.
 * It also adjusts the camera settings based on the availability of PSRAM.
 */
void camera_init()
{
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  // config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (psramFound())
  {
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  }
  else
  {
    // Limit the frame size when PSRAM is not available
    config.frame_size = FRAMESIZE;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV2640_PID)
  {
    s->set_framesize(s, FRAMESIZE);     // Set frame size to SVGA
    s->set_quality(s, 10);                   // Set JPEG quality (lower is better quality)
    s->set_brightness(s, 0);                 // Set brightness (-2 to 2)
    s->set_contrast(s, 0);                   // Set contrast (-2 to 2)
    s->set_saturation(s, 0);                 // Set saturation (-2 to 2)
    s->set_special_effect(s, 0);             // Disable special effects (0-7)
    s->set_whitebal(s, true);                // Enable auto white balance
    s->set_awb_gain(s, true);                // Enable auto white balance gain
    s->set_wb_mode(s, 0);                    // Set white balance mode (0: auto)
    s->set_exposure_ctrl(s, true);           // Enable auto exposure control
    s->set_aec2(s, true);                    // Enable 2nd auto exposure control
    s->set_ae_level(s, 0);                   // Set auto exposure level (-2 to 2)
    s->set_gain_ctrl(s, true);               // Enable auto gain control
    s->set_agc_gain(s, 0);                   // Set AGC gain (0 to 30)
    s->set_gainceiling(s, (gainceiling_t)0); // Set gain ceiling (0 to 6)
    s->set_bpc(s, false);                    // Disable black pixel correction
    s->set_wpc(s, true);                     // Enable white pixel correction
    s->set_raw_gma(s, true);                 // Enable gamma correction
    s->set_lenc(s, true);                    // Enable lens correction
    s->set_hmirror(s, false);                // Disable horizontal mirror
    s->set_vflip(s, true);                   // Disable vertical flip
    s->set_dcw(s, true);                     // Enable downsize EN
    s->set_colorbar(s, false);               // Disable color bar test pattern
  }
}

/**
 * @brief Handles WebSocket events.
 *
 * This function processes various WebSocket events such as connection, disconnection,
 * receiving text messages, and errors. It also handles ping and pong events.
 *
 * @param type The type of WebSocket event.
 * @param payload The payload data associated with the event.
 * @param length The length of the payload data.
 */
void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    Serial.printf("[WSc] Disconnected!\n");
    break;
  case WStype_CONNECTED:
    Serial.printf("[WSc] Connected to url: %s\n", payload);
    webSocket.sendTXT("Connected");
    break;
  case WStype_TEXT:
    Serial.printf("[WSc] get text: %s\n", payload);
    break;
  case WStype_ERROR:
    Serial.printf("[WSc] Error occurred!\n");
    break;
  case WStype_PING:
    Serial.printf("[WSc] Ping received!\n");
    break;
  case WStype_PONG:
    Serial.printf("[WSc] Pong received!\n");
    break;
  default:
    Serial.printf("[WSc] Unhandled event type: %d\n", type);
    break;
  }
}

/**
 * @brief Changes the WiFi credentials and reconnects to the network.
 *
 * This function updates the WiFi SSID and password based on the provided command input.
 * It writes the new credentials to a configuration file and attempts to reconnect to the network.
 *
 * @param cmd_input Command input containing the new SSID and password.
 * @return CMD_RESPONSE A response object indicating the status and result of the operation.
 */
CMD_RESPONSE change_wifi(CMD_INPUT cmd_input)
{
  CMD_RESPONSE ret = {"OK", "WiFi changed successfully"};
  if (cmd_input.args.hasOwnProperty("ssid") && cmd_input.args.hasOwnProperty("pwd"))
  {
    ssid = (const char *)cmd_input.args["ssid"];
    password = (const char *)cmd_input.args["pwd"];
    if (!writeFile(SPIFFS, "/config.json", JSON.stringify(cmd_input.args).c_str()))
    {
      ret.status = "ERR";
      ret.body = "Failed to write to config file";
      return ret;
    }
    WiFi.disconnect();
    if (!connect_to_network())
    {
      ret.status = "ERR";
      ret.body = "Failed to connect to new WiFi network";
      return ret;
    }
  }
  else
  {
    ret.status = "ERR";
    ret.body = "SSID and password are required";
  }
  return ret;
}

/**
 * @brief Captures a photo using the ESP32 camera module.
 *
 * This function captures an image using the ESP32 camera module and prints the image size
 * and to the serial monitor and sends the image content over the websocket connection.
 *
 * @param cmd_input Command input containing any additional arguments (not used in this function).
 * @return CMD_RESPONSE A response object indicating the status and result of the operation.
 */
CMD_RESPONSE take_photo(CMD_INPUT cmd_input)
{
  CMD_RESPONSE ret = {"OK", "Photo taken successfully"};
  #ifdef TEST_MODE
  analogWrite(LED_GPIO_NUM, 100);
  delay(500); 
  #endif
  camera_fb_t *fb = esp_camera_fb_get();
  #ifdef TEST_MODE
  analogWrite(LED_GPIO_NUM, 0);
  #endif
  if (!fb)
  {
    ret.status = "ERR";
    ret.body = "Failed to capture image";
    return ret;
  }
  // Send Image metadata to the server
  JSONVar imageMetadata;
  imageMetadata = cmd_input.args;
  // Send image metadata and content over websocket abd check for errors
  if (!webSocket.sendTXT(JSON.stringify(imageMetadata).c_str()) || !webSocket.sendBIN(fb->buf, fb->len)) {
    ret.status = "ERR";
    ret.body = "Failed to send image over websocket";
  }
  esp_camera_fb_return(fb);
  return ret;
}