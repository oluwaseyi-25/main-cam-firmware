#ifdef TEST_MODE
#define LOG(msg)         \
    Serial.println();    \
    Serial.println(msg); \
    Serial.flush();
#define LOGF(...) \
    Serial.printf(__VA_ARGS__);
#else
#define LOG(msg)
#define LOGF(...)
#endif

#ifdef ERROR_LOGGING
#define LOG_ERR(msg)     \
    Serial.println();    \
    Serial.println(msg); \
    Serial.flush();
#else
#define LOG_ERR(msg)
#endif

// ==========================
// Includes and Definitions
// ==========================

#include <Arduino.h>
#include <SPIFFS.h>
#include "esp_camera.h"
#include "soc/soc.h"          // For brownout detector settings
#include "soc/rtc_cntl_reg.h" // For RTC_CNTL_BROWN_OUT_EN

#include <WiFi.h>
#include <Arduino_JSON.h>
#include <WebSocketsClient.h>

// Camera parameters
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27

#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM    5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22
#define FRAMESIZE FRAMESIZE_XGA

// 4 for flash led or 33 for normal led
#define LED_GPIO_NUM   4

// ==========================
// Type Definitions
// ==========================

typedef struct
{
    String status;
    String body;
} CMD_RESPONSE;

typedef struct
{
    JSONVar args;
} CMD_INPUT;

typedef CMD_RESPONSE (*OpPtr)(CMD_INPUT);

// ==========================
// Global Variables
// ==========================

String ssid;
String password;

// ==========================
// Function Prototypes
// ==========================

String readFile(fs::FS &fs, const char *path);
bool writeFile(fs::FS &fs, const char *path, const char *message);
bool loadConfig();
void camera_init();

WebSocketsClient webSocket;
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);

JSONVar cmdResponseToJSON(CMD_RESPONSE response);
OpPtr opcodeToFunc(String opcode);
bool connect_to_network();

CMD_RESPONSE exec_cmd(JSONVar cmd);
CMD_RESPONSE change_wifi(CMD_INPUT cmd_input);
CMD_RESPONSE take_photo(CMD_INPUT cmd_input);