A GPS tracker prototype based on ESP32 TTGO T-Display and Neo 6m GPS receiver. The data flow in the system is as follows:
![arch](https://github.com/user-attachments/assets/f2e0a4f2-ac4c-4301-a31c-b22a92225c09)
Prototype's front view:
![dispfront](https://github.com/user-attachments/assets/91b42d12-4899-48ca-89b0-de7cae87211e)
The board sends data points to the cloud storage(the project uses ThingSpeak, you can use whatever serves your needs the best) through the network using the
MQTT protocol to improve the quality of communicaation in networks with limited coverage. The web app continuously polls the cloud for new data and
displays it on a map.
Libraries used: TFT_eSPI(to display text info), PubSubClient(for MQTT) and TinyGPSPlus(to parse GPS messages sent by the module).
The schematics is as follows:
![scheme](https://github.com/user-attachments/assets/593167b0-b82f-44f6-8d13-af41319b2e1e)
The battery is connected to its corresponding connector on the board
![scheme](https://github.com/user-attachments/assets/7a2f96a0-cf84-4538-9089-ae3bfbb57882)
The prototype's housing looks like this
![viewfront](https://github.com/user-attachments/assets/7ca74d45-6195-4d4d-b3b9-dfa2c5ed4bee)
and like this from the inside
![internals](https://github.com/user-attachments/assets/7349f0ed-3dc2-4825-8aee-70d0b8cc1806)
The web app traces the path as a polyline with a speed gradient added over it(red == fast, blue == slow)
![path_street](https://github.com/user-attachments/assets/a2c01b86-ee06-49b2-a153-84544279c8f8)
A 2000 mAh battery lasts a full 24 hours, but the autonomy could be improved if deep sleep mode is implemented(currently it isn't as it shuts down the gps sensor)
![battery](https://github.com/user-attachments/assets/5be81c72-3a1e-4634-95fa-509f8cc808a8)

