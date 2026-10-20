/**
 * @file c6_link.h
 * @brief Software restarts of the P4 become a CORE reset, so that the SDIO link to the
 *        ESP32-C6 Wi-Fi co-processor comes back on the next boot (see c6_link.cpp).
 */
#pragma once

// True when esp_restart() really ends with a CORE reset. Called once, as the initial value
// of the `c6_link_ready` global (firmware/packages/board-waveshare-7b.yaml): it also keeps
// c6_link.cpp, and its constructor, in the binary.
bool c6_link_setup();
