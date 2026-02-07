#pragma once 

#include "bsp/board_api.h"
#include "tusb.h"
#include "platform/pico/input/hw_hid_types.h"


// Defines
#define USE_ANSI_ESCAPE   0
#define MAX_REPORT  4


/** Hid instance device type */
enum EHIDDeviceType {
  EHIDDeviceType_None,
  EHIDDeviceType_Keyboard,
  EHIDDeviceType_Mouse,
  EHIDDeviceType_GamePad
};


/** Hid state
 */
typedef struct hid_info_t
{
  int mounted;
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
  enum EHIDDeviceType deviceType;
} hid_info_t;


/** Sony ds4 controller */
typedef struct TU_ATTR_PACKED
{
  uint8_t x, y, z, rz; // joystick

  struct {
    uint8_t dpad     : 4; // (hat format, 0x08 is released, 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW)
    uint8_t square   : 1; // west
    uint8_t cross    : 1; // south
    uint8_t circle   : 1; // east
    uint8_t triangle : 1; // north
  };

  struct {
    uint8_t l1     : 1;
    uint8_t r1     : 1;
    uint8_t l2     : 1;
    uint8_t r2     : 1;
    uint8_t share  : 1;
    uint8_t option : 1;
    uint8_t l3     : 1;
    uint8_t r3     : 1;
  };

  struct {
    uint8_t ps      : 1; // playstation button
    uint8_t tpad    : 1; // track pad click
    uint8_t counter : 6; // +1 each report
  };

  uint8_t l2_trigger; // 0 released, 0xff fully pressed
  uint8_t r2_trigger; // as above

} sony_ds4_report_t;


typedef struct TU_ATTR_PACKED {
  uint8_t set_rumble : 1;
  uint8_t set_led : 1;
  uint8_t set_led_blink : 1;
  uint8_t set_ext_write : 1;
  uint8_t set_left_volume : 1;
  uint8_t set_right_volume : 1;
  uint8_t set_mic_volume : 1;
  uint8_t set_speaker_volume : 1;
  uint8_t set_flags2;

  uint8_t reserved;

  uint8_t motor_right;
  uint8_t motor_left;

  uint8_t lightbar_red;
  uint8_t lightbar_green;
  uint8_t lightbar_blue;
  uint8_t lightbar_blink_on;
  uint8_t lightbar_blink_off;

  uint8_t ext_data[8];

  uint8_t volume_left;
  uint8_t volume_right;
  uint8_t volume_mic;
  uint8_t volume_speaker;

  uint8_t other[9];
} sony_ds4_output_report_t;


/** Generic gamepad */
typedef struct TU_ATTR_PACKED {  
  uint8_t dummy0;
  uint8_t dummy1;
  uint8_t dummy2;
  uint8_t dpad_leftright_data;
  uint8_t dpad_updown_data;
  uint8_t right_btns_data;
  uint8_t mid_btns_data;
  uint8_t dummy3;

} dragon_crap_t;


void hid_init();
struct HIDCMD_getState* hid_get_state(); 