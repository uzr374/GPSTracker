#include <Esp.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <WiFi.h>
#include <Wire.h>
#include <Button2.h>
#include <esp_adc_cal.h>
#include <esp_wifi.h>

#define USE_MQTT 1

#if USE_MQTT
#include <PubSubClient.h>
#else
#include <HTTPClient.h>
#endif

#define IS_DEBUG 0

#define ADC_EN                  14  //  ADC_EN is the ADC detection enable port
#define ADC_PIN                 34
#define BUTTON_1_PIN            35
#define BUTTON_2_PIN            0
#define FULL_CHARGE_VOLTAGE     4.31
#define FULL_DISCHARGE_VOLTAGE  3.00
#define BATTCHARG_MIN_V         4.6
#define BATTCHARG_MAX_V         4.85
#define TFT_BACKLIGHT_OFF       LOW
#define HIGH_CHARGE_THRESH      70
#define LOW_CHARGE_THRESH       20
#define DISPLAY_OFF_TIMEOUT     60000 //in ms

int vref = 1100; //In mV
float curv = 0.0f;

static constexpr struct ThingSpeakCredentials {  
  #if USE_MQTT
  static constexpr auto channel_id = "2137943";
  static constexpr auto mqtt_clnt_id = "";
  static constexpr auto mqtt_usrname = "";
  static constexpr auto mqtt_pwd =     "";
  #else
  static constexpr auto th_write_key = "", th_read_key = "";
  #endif
} creds;

static constexpr auto UPDATE_TIMEOUT = 12000;
static constexpr auto wifi_ssid = "hotspot", wifi_pwd = "-12345678";

#if USE_MQTT
static constexpr auto mqtt_server = "mqtt3.thingspeak.com"; // The MQTT server
static constexpr auto mqtt_portnum = 1883;
#else
static constexpr auto http_url = "https://api.thingspeak.com/update?api_key=";
#endif

struct GPSData {
 float lat = 0.0, lng = 0.0, alt = 0.0, speed = 0.0;
 int sat = 0;
 int year = -1, month = -1, day = -1;
 int hour = -1, min = -1, sec = -1; 
 bool is_uploaded = true;
} last_datapoint;

auto& gps_serial = Serial2; //Alias for serial port #2(pins 25, 26, probably 27)
auto& dbg_serial = Serial;
auto& this_device = ESP;
auto& wifi = WiFi;

TFT_eSPI tftd;
TinyGPSPlus gps_inst;
Button2 btn1(BUTTON_1_PIN), btn2(BUTTON_2_PIN);

#if USE_MQTT
WiFiClient espClient;
PubSubClient client(espClient);
#endif

static uint32_t last_update_time = 0LL, last_display_update_time = 0LL;
bool displayOn = true;
static char msgbuf[512] = {}; //Debug message buffer, 511 symb max debug message

void logMessage(const char* const fmt, ...) {
#ifdef IS_DEBUG
  va_list valst;
  va_start(valst, fmt);
  vsprintf(msgbuf, fmt, valst);
  dbg_serial.println(msgbuf);
  va_end(valst);
  dbg_serial.flush();
#endif
}

float getVoltage()
{
    setupADC();
    const auto v = analogRead(ADC_PIN);
    curv = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
    return curv;
}

bool battIsCharging() {
    return curv >= FULL_CHARGE_VOLTAGE;
}

float _calcPercentage(float volts, float max, float min) {
    float percentage = (volts - min) * 100 / (max - min);
    if (percentage > 100) {
        percentage = 100;
    }
    if (percentage < 0) {
        percentage = 0;
    }
    return percentage;
}

float battCalcPercentage(float volts) {
    if (battIsCharging()){
      return _calcPercentage(volts,BATTCHARG_MAX_V,BATTCHARG_MIN_V);
    } else {
      return _calcPercentage(volts,FULL_CHARGE_VOLTAGE ,FULL_DISCHARGE_VOLTAGE );
    }
}

void enterDeepSleep(){
        if(!displayOn){
          displayOn = true;
          setTFTBacklightOn(displayOn);
        }

        tftd.fillScreen(TFT_BLACK);
        tftd.setCursor(0, 0);
        tftd.setTextColor(TFT_WHITE);
        tftd.println("Going to sleep...");
        esp_wifi_stop();
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
        //esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
        delay(1000);
        esp_deep_sleep_start();
}

void setupButtons()
{
    btn1.setLongClickHandler([](Button2 & b) {
        enterDeepSleep();
    });

    btn2.setPressedHandler([](Button2 & b) {
        displayOn = !displayOn;
        setTFTBacklightOn(displayOn);
    });
}

void button_loop()
{
    btn1.loop();
    btn2.loop();
}

void espDelay(int ms)
{
  button_loop();
  delay(ms);

  // if(ms < 30){
  //   delay(ms);
  //   return;
  // }
  
    // esp_sleep_enable_timer_wakeup(ms * 1000);
    // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
    // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
    // esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);
    // delay(60);
    // esp_light_sleep_start();
}

int estimateBatteryPercentage(float voltage) {
  return (int)battCalcPercentage(voltage);
}

bool WiFiIsConnected(){
  return wifi.isConnected() && (wifi.status() == WL_CONNECTED);
}

void setupADC(){
  pinMode(ADC_EN, OUTPUT);
  digitalWrite(ADC_EN, HIGH);
  //return;

  vref = 1100;
  esp_adc_cal_characteristics_t adc_chars{};
    const auto val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, 
    (adc_atten_t)ADC1_CHANNEL_6, (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
    //Check type of calibration value used to characterize ADC
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
       // Serial.printf("-->[BATT] ADC eFuse Vref:%u mV\n", adc_chars.vref);
        vref = adc_chars.vref;
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        //Serial.printf("-->[BATT] ADC Two Point --> coeff_a:%umV coeff_b:%umV\n", adc_chars.coeff_a, adc_chars.coeff_b);
    } else {
        //Serial.printf("-->[BATT] ADC Default Vref: %u mV\n", vref);
    }
}

void setTFTBacklightOn(bool on){
  if(TFT_BL > 0){
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, (on ? TFT_BACKLIGHT_ON : TFT_BACKLIGHT_OFF));
  }
}

void setupWifiConn(int timeout = 3000){
  wifi.mode(WIFI_STA);//Client connects to the network
  const auto nnetw = wifi.scanNetworks();
  if (nnetw <= 0) return;
  else{
    bool found = false;
    for(int n = 0; n < nnetw; ++n){
      if(wifi.SSID(n) == String(wifi_ssid)){
          found = true;
          break;
      }
    }
    if(!found) return;
  }
  wifi.setAutoReconnect(true);
  wifi.begin(wifi_ssid, wifi_pwd);

  const auto start_ts = millis();
  while(!WiFiIsConnected()){
    delay(10);
    if(millis() - start_ts >= timeout){
      logMessage("Failed to connect to Wi-Fi!");
      break;
    }
  }

  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
}

void setupNetworkConnection(){
  setupWifiConn();

#if USE_MQTT
  client.setServer(mqtt_server, mqtt_portnum); // Configure MQTT server and port
  if(!client.setBufferSize(1024)) { //Packet max size
  logMessage("Failed to use 1024 byte buffer size");
    client.setBufferSize(128);
  } else{
    logMessage("Using 1024 byte buffer size");
  }
  #endif
}

void setupGPSReceiver(){
  static constexpr auto GPS_NEO6MV2_BAUDRATE = 9600;
  static constexpr int8_t GPS_RXPIN = 26, GPS_TXPIN = 27;
  gps_serial.begin(GPS_NEO6MV2_BAUDRATE, SERIAL_8N1, GPS_RXPIN, GPS_TXPIN);
  while(!gps_serial){}
}

void setupDisplay(){
  tftd.init();
  setTFTBacklightOn(displayOn);
  tftd.fillScreen(TFT_BLACK);
  tftd.setCursor(0, 0);
  tftd.setRotation(3);
  tftd.setTextSize(2);
  tftd.setTextColor(TFT_WHITE);
  tftd.println("Initializing...");
}

void setupDebugSerial(){
#ifdef IS_DEBUG
  dbg_serial.begin(115200);
  while(!dbg_serial){}
#endif
}

void handleOutOfMem(){
#ifdef IS_DEBUG
  dbg_serial.println("Out of memory, rebooting...");
#endif
  this_device.restart();
}

String formatISO8601DateTime() {
  const auto year = last_datapoint.year;
  const auto month = last_datapoint.month;
  const auto day = last_datapoint.day;

  const auto hour = last_datapoint.hour;
  const auto minute = last_datapoint.min;
  const auto second = last_datapoint.sec;

  return String(year) + "-" +
                       (month < 10 ? "0" : "") + String(month) + "-" +
                       (day < 10 ? "0" : "") + String(day) + "T" +
                       (hour < 10 ? "0" : "") + String(hour) + ":" +
                       (minute < 10 ? "0" : "") + String(minute) + ":" +
                       (second < 10 ? "0" : "") + String(second) + "Z";
}

void setup() {
  setupADC();
  setupDebugSerial();
  setupDisplay();
  setupNetworkConnection();
  setupGPSReceiver();
  setupButtons();
}

String buildMqttPayload() {
  String payload = "field1=";
  payload += String(last_datapoint.lat, 6);
  payload += "&field2=";
  payload += String(last_datapoint.lng, 6);
  payload += "&field3=";
  payload += String(last_datapoint.alt, 2);
  payload += "&field4=";
  payload += String(last_datapoint.speed, 2);
  payload += "&field5=";
  payload += String(last_datapoint.sat);
  payload += "&field6=";
  payload +=  String(battCalcPercentage(curv), 2);
  payload += "&created_at=";
  payload += formatISO8601DateTime();
  
  return payload;
}

#if not USE_MQTT
String buildHTTPURL(){
  return String(http_url) + String(creds.th_write_key) + String("&") + buildMqttPayload();
}
#endif

void updateDisplay() {
  if(!displayOn) return;
  tftd.fillScreen(TFT_BLACK);
  tftd.setCursor(0, 0);

  // Network status
  tftd.setCursor(0, 0);
  tftd.setTextColor(TFT_WHITE);
  tftd.print("WiFi:");
  if (WiFiIsConnected()) {
    tftd.setTextColor(TFT_GREEN);
    auto ssid = wifi.SSID();
    if(ssid.length() > 5){
      ssid = ssid.substring(0, 4);
    }
    const auto rssi = wifi.RSSI();
    tftd.println(ssid + String(",RSSI:") + String(rssi));
  } else {
    tftd.setTextColor(TFT_RED);
    tftd.println("Disconnected");
  }
  
  // GPS data
  tftd.setTextColor(TFT_WHITE);
  tftd.println(String("Latitude: ") + String(last_datapoint.lat, 6));
  tftd.println(String("Longitude: ") + String(last_datapoint.lng, 6));
  const int satellite_count = last_datapoint.sat;
  tftd.setTextColor(satellite_count > 3 ? TFT_WHITE : TFT_RED);
  tftd.println(String("Satellites: ") + String(satellite_count));

  //Battery status
  const auto volt = getVoltage();
  const auto pcnt = estimateBatteryPercentage(volt);
  auto batColor = TFT_YELLOW;
  if(pcnt >= HIGH_CHARGE_THRESH) {
    batColor = TFT_GREEN;
  }
  else if(pcnt <= LOW_CHARGE_THRESH){
    batColor = TFT_RED;
  }
  tftd.setTextColor(batColor);
  tftd.println(String("Bat:") + (battIsCharging() ? String("Chrg,") : String()) + 
  (String(pcnt) + "%") + String("(") + String(volt, 2) + String("V)"));
}

#if USE_MQTT
bool MQTTMaybeReconnect() {
  bool connected = client.connected();
  if (!connected) {
    logMessage("Attempting MQTT connection...");
    if (client.connect(creds.mqtt_clnt_id, creds.mqtt_usrname, creds.mqtt_pwd, 
      nullptr, MQTTQOS0, false, nullptr, true)) {
      logMessage("Connected to MQTT server");
      connected = true;
    } else {
      logMessage("Failed to connect, trying again in ~2 seconds");
    }
  }

  return connected;
}
#endif

bool readGPSData()
{
  bool success = false;
  static constexpr uint32_t ms = 2000;//2 second timeout for retrieving GPS coordinates
  const auto start = millis();
  while((millis() - start) <= ms)
  {
    if (gps_serial.available() > 0){
      success = gps_inst.encode(gps_serial.read());
      if(success) break;
    } else{
      espDelay(20);
    }
  }
  return success;
}

bool maybeUpdateGPSDatapoint(){
  auto& gps = gps_inst;
  const auto data_was_read = readGPSData();  
  const bool data_available = data_was_read && gps.location.isValid() && gps.altitude.isValid() 
  && gps.satellites.isValid() && (gps.satellites.value() > 3) && gps.date.isValid() 
  && gps.time.isValid() && gps.speed.isValid();

  if(data_available) {
    last_datapoint.lat = gps_inst.location.lat();
    last_datapoint.lng = gps_inst.location.lng();
    last_datapoint.sat = gps_inst.satellites.value();
    last_datapoint.speed = gps_inst.speed.kmph();
    last_datapoint.alt = gps_inst.altitude.meters();
    last_datapoint.year = gps_inst.date.year();
    last_datapoint.month = gps_inst.date.month();
    last_datapoint.day = gps_inst.date.day();
    last_datapoint.hour = gps_inst.time.hour();
    last_datapoint.min = gps_inst.time.minute();
    last_datapoint.sec = gps_inst.time.second();
    last_datapoint.is_uploaded = false;
  }

  return data_available;
}

void maybeUpdateGPSData() {
if(getVoltage() <= FULL_DISCHARGE_VOLTAGE){
  enterDeepSleep();
}

  #if USE_MQTT
  client.loop();
  #endif
  button_loop();
  const auto cur_time = millis();

  const auto has_new_data = maybeUpdateGPSDatapoint();

  if(!last_display_update_time || ((cur_time - last_display_update_time) >= 300)){
    last_display_update_time = cur_time;
    if(displayOn){
    updateDisplay();
    }
  }
  
  if(last_update_time && ((cur_time - last_update_time) < UPDATE_TIMEOUT)){
    espDelay(250);
    return;
  }
  else{
    last_update_time = cur_time;

    if (!last_datapoint.is_uploaded) { 
      if(!WiFiIsConnected()){
        setupWifiConn(700);
      } 
    if (WiFiIsConnected()) {
      #if USE_MQTT
      const auto connected = MQTTMaybeReconnect();
      if(connected){
      client.loop();  
      const auto mqtt_payload = buildMqttPayload();
       //logMessage("Publishing to MQTT: %s", mqtt_payload.c_str());
      if(client.publish((String("channels/") + String(creds.channel_id) + String("/publish")).c_str(), mqtt_payload.c_str(), false)) {
        last_datapoint.is_uploaded = true;
      //logMessage("Published to MQTT: %s", mqtt_payload.c_str());
      } else{
        //logMessage("Error while publishing");
      }
      }
      #else
          HTTPClient clnt;
          const auto msg = buildHTTPURL();
          //logMessage("Sending %s", msg.c_str());
          clnt.begin(msg.c_str());
          const auto resp = clnt.GET();
          clnt.end();
          last_datapoint.is_uploaded = (resp == 200);
          //logMessage("Sent %s, code: %d", msg.c_str(), resp);
      #endif
    }
  }
  }
}

void loop() {
maybeUpdateGPSData();
}
