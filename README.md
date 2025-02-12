Am GPS tracker prototype based on ESP32 TTGO T-Display and Neo 6m GPS receiver. The data flow in the system is as follows:
![arch](https://github.com/user-attachments/assets/f2e0a4f2-ac4c-4301-a31c-b22a92225c09)
The board sends data points to the cloud storage(I used ThingSpeak, you can use whatever serves your needs the best) through the network using the
MQTT protocol to improve the quality of communicaation in networks with limited coverage. The web app continuously polls the cloud for new data and
displays it on a map.
Libraries used: TFT_eSPI(to display text info), PubSubClient(for MQTT) and TinyGPSPlus(to parse GPS messages sent by the module).
