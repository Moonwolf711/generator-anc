// esp_passthrough.ino -- TEMPORARY Teensy 4.x sketch to flash the ESP-12E that sits
// behind the Teensy on the BurgessWorld carrier (ESP <-> Serial1, Teensy pins 0/1).
//
// The ESP has no direct USB. This bridges USB Serial <-> Serial1 and follows the host
// baud so esptool can reach the ESP through the Teensy.
//
// FLASH SEQUENCE:
//   1) Upload this sketch to the Teensy (replaces generator_anc temporarily).
//   2) Put the ESP in its bootloader: hold ESP-PGM, tap reset, release ESP-PGM.
//   3) Flash the ESP from the PC. esptool cannot toggle the ESP reset through this
//      bridge, so the chip must already be in the bootloader (step 2) and esptool must
//      skip its own reset:  --before no_reset --after hard_reset
//   4) Re-flash generator_anc_teensy to restore normal operation.

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);          // RX1=pin0, TX1=pin1  <->  ESP-12E
}

void loop() {
  static uint32_t cur = 115200;
  const uint32_t b = Serial.baud();       // esptool may bump the baud mid-flash
  if (b && b != cur) { cur = b; Serial1.begin(cur); }
  while (Serial.available())  Serial1.write(Serial.read());
  while (Serial1.available()) Serial.write(Serial1.read());
}
