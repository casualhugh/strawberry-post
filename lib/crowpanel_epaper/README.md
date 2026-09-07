# CrowPanel e-paper driver

This directory vendors Elecrow's Arduino driver files for the 5.79-inch
CrowPanel from upstream commit
`453aa9ec9ccb94bc0c91c81c68eaeef851317aee`.

Source directory upstream:
`example/arduino/Examples/5.79_Global_refresh`

Local changes are intentionally small:

- the BUSY wait yields to the ESP32 runtime;
- a 15-second timeout replaces the upstream unbounded wait;
- operation-status functions let the application disable display updates after
  a timeout without stopping Wi-Fi, DNS, HTTP, or storage.

The unusual 800 by 272 framebuffer is required by the two SSD1683 controllers.
The physical display is 792 by 272 pixels. Elecrow's mapping inserts the
controller seam while writing the framebuffer.
