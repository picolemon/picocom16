#pragma GCC optimize ("O0")
#include "hid_app.h"
#include "picocom/input/input.h"

// Globals
static uint8_t const keycode2ascii[128][2] =  { HID_KEYCODE_TO_ASCII };
HID_MouseStateData mouseState;  // current mouse state
HID_KeyboardStateData keyboardState;  // current kb state
HID_GamepadStateData gamepadState; // current gamepad state
HIDCMD_getState hidState;  // picocom hid state 
hid_info_t hid_info[CFG_TUH_HID];


// hid driver
void hid_driver_init()
{
  // init host stack on configured roothub port
  tuh_init(BOARD_TUH_RHPORT);
  board_init_after_tusb();

  memset(&hid_info, 0, sizeof(hid_info));
  memset(&mouseState, 0, sizeof(mouseState));
  memset(&keyboardState, 0, sizeof(keyboardState));
  memset(&gamepadState, 0, sizeof(gamepadState));
  memset(&hidState, 0, sizeof(hidState));
}


struct HIDCMD_getState* hid_driver_get_state()
{
  return &hidState;
}


void hid_driver_update()
{
    // tinyusb host task
    tuh_task();    

#if CFG_TUH_CDC
    cdc_task();
#endif

#if CFG_TUH_HID
    hid_app_task();
#endif

}


static inline bool is_sony_ds4(uint8_t dev_addr)
{
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  return ( (vid == 0x054c && (pid == 0x09cc || pid == 0x05c4)) // Sony DualShock4
           || (vid == 0x0f0d && pid == 0x005e)                 // Hori FC4
           || (vid == 0x0f0d && pid == 0x00ee)                 // Hori PS4 Mini (PS4-099U)
           || (vid == 0x1f4f && pid == 0x1002)                 // ASW GG xrd controller
         );
}


static inline bool is_generic_dragon_crap(uint8_t dev_addr)
{
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  return ( (vid == 0x0079 && pid == 0x11 ) );
}


void hid_app_task(void)
{
}


void update_hid_state()
{  
  hidState.keyboardConnected = 0;
  hidState.mouseConnected = 0;
  hidState.gamepadConnected = 0;

  for(int i=0;i<CFG_TUH_HID;i++)
  {
    hid_info_t* info = &hid_info[i];
    if(info->mounted)
    {
      switch (info->deviceType) {
        case EHIDDeviceType_Keyboard:
        {
          hidState.keyboardConnected++;
          break;
        }
        case EHIDDeviceType_Mouse:
        {
          hidState.mouseConnected++;
          break;
        }        
        case EHIDDeviceType_GamePad:
        {
          hidState.gamepadConnected++;
          break;
        }         
      }
    }
  }
}


void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  (void)desc_report;
  (void)desc_len;

  if(instance >= CFG_TUH_HID)
    return;
  
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
  printf("VID = %04x, PID = %04x\r\n", vid, pid);

  // Interface protocol (hid_interface_protocol_enum_t)
  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  printf("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);

  enum EHIDDeviceType deviceType = EHIDDeviceType_None;
  
  if ( itf_protocol == HID_ITF_PROTOCOL_NONE )
  {
    hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
    printf("HID has %u reports \r\n", hid_info[instance].report_count);
  }
  else {
    switch (itf_protocol) 
    {
      case HID_ITF_PROTOCOL_KEYBOARD:
      {
        deviceType = EHIDDeviceType_Keyboard;
        break;
      }
      case HID_ITF_PROTOCOL_MOUSE:
      {
        deviceType = EHIDDeviceType_Mouse;
        break;
      }      
    }
  }

  // Detect device type
  if ( is_sony_ds4(dev_addr) )
  {    
    if ( !tuh_hid_receive_report(dev_addr, instance) )
    {
      printf("Error: cannot request to receive report\r\n");
    }
    else 
    {
      deviceType = EHIDDeviceType_GamePad;
    }
  } 
  else if(is_generic_dragon_crap(dev_addr))
  {
    if ( !tuh_hid_receive_report(dev_addr, instance) )
    {
      printf("Error: cannot request to receive report\r\n");
    }  
    else 
    {
      deviceType = EHIDDeviceType_GamePad;
    }
  }
  else
  {
    if ( !tuh_hid_receive_report(dev_addr, instance) )
    {
      printf("Error: cannot request to receive report\r\n");
    }    
  }

  hid_info[instance].deviceType = deviceType;
  hid_info[instance].mounted = 1;
  update_hid_state();
}


void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  if(instance >= CFG_TUH_HID)
    return;

  printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
  hid_info[instance].mounted = 0;
  update_hid_state();
}


bool diff_than_2(uint8_t x, uint8_t y)
{
  return (x - y > 2) || (y - x > 2);
}


bool diff_report_ds4(sony_ds4_report_t const* rpt1, sony_ds4_report_t const* rpt2)
{
  bool result;

  // x, y, z, rz must different than 2 to be counted
  result = diff_than_2(rpt1->x, rpt2->x) || diff_than_2(rpt1->y , rpt2->y ) ||
           diff_than_2(rpt1->z, rpt2->z) || diff_than_2(rpt1->rz, rpt2->rz);

  // check the rest with mem compare
  result |= memcmp(&rpt1->rz + 1, &rpt2->rz + 1, sizeof(sony_ds4_report_t)-6);

  return result;
}


void process_soy_ds4(uint8_t const* report, uint16_t len)
{
  static HID_GamepadStateData lastState = {};

  // previous report used to compare for changes
  static sony_ds4_report_t prev_report = { 0 };

  uint8_t const report_id = report[0];
  report++;
  len--;

  // all buttons state is stored in ID 1
  if (report_id == 1)
  {
    sony_ds4_report_t ds4_report;
    memcpy(&ds4_report, report, sizeof(ds4_report));
    prev_report.counter = ds4_report.counter;

    //if ( diff_report_ds4(&prev_report, &ds4_report) )
    {
        // released, 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW)
        bool dpadUp = ( ds4_report.dpad == 0x08);
        bool dpad_left = !dpadUp && (ds4_report.dpad == 5 || ds4_report.dpad == 6 || ds4_report.dpad == 7);
        bool dpad_right = !dpadUp && (ds4_report.dpad == 1 || ds4_report.dpad == 2 || ds4_report.dpad == 3);
        bool dpad_up = !dpadUp && (ds4_report.dpad == 0 || ds4_report.dpad == 1 || ds4_report.dpad == 7);
        bool dpad_down = !dpadUp && (ds4_report.dpad == 3 || ds4_report.dpad == 4 || ds4_report.dpad == 5);

        // Fake axis
        gamepadState.axes[GamepadAxis_LeftStick_X] = (((float)ds4_report.x / (float)0xff) - 0.5f) * 2.0f;
        gamepadState.axes[GamepadAxis_LeftStick_Y] = (((float)ds4_report.y / (float)0xff) - 0.5f) * 2.0f;
        gamepadState.axes[GamepadAxis_RightStick_X] = (((float)ds4_report.z / (float)0xff) - 0.5f) * 2.0f;   
        gamepadState.axes[GamepadAxis_RightStick_Y] = (((float)ds4_report.rz / (float)0xff) - 0.5f) * 2.0f;
        gamepadState.axes[GamepadAxis_LeftTrigger] = ds4_report.l2_trigger;
        gamepadState.axes[GamepadAxis_RightTrigger] = ds4_report.r2_trigger;        
        
        // Dpad
        gamepadState.dpad = 0;
        if(dpad_left)
            gamepadState.dpad |= GamepadDpad_Left;
        if(dpad_right)
            gamepadState.dpad |= GamepadDpad_Right;
        if(dpad_up)
            gamepadState.dpad |= GamepadDpad_Up;
        if(dpad_down)
            gamepadState.dpad |= GamepadDpad_Down;

        // Buttons (mapping)
        gamepadState.buttons = 0;
        if(ds4_report.circle)
            gamepadState.buttons |= GamepadButton_B;    // Nes style BA flipped, A on left
        if(ds4_report.cross)
            gamepadState.buttons |= GamepadButton_A;    // Nes style BA flipped, A on left
        if(ds4_report.share)
            gamepadState.buttons |= GamepadButton_Select;  
        if(ds4_report.ps)
            gamepadState.buttons |= GamepadButton_Start;    

        if(memcmp(&lastState,&gamepadState,sizeof(lastState)) == 0)
            return;

        // append hid state
        if(hidState.gamepadCnt >= INPUT_HID_MAX_MOUSE_BUFFER_CNT)
        {
            hidState.gamepadOverflowCnt++;
            return;
        }
        hidState.gamepad[hidState.gamepadCnt++] = gamepadState;
        hidState.id++;  

        lastState = gamepadState;
    }

    prev_report = ds4_report;
  }
}


void process_dragon_crap(uint8_t const* report, uint16_t len)
{
  static HID_GamepadStateData lastState = {};

  dragon_crap_t* state = (dragon_crap_t*)report;
  bool dpad_left = state->dpad_leftright_data == 0x00;
  bool dpad_right = state->dpad_leftright_data == 0xff;
  bool dpad_up = state->dpad_updown_data == 0x00;
  bool dpad_down = state->dpad_updown_data == 0xff;
  bool btn_a_down = (state->right_btns_data & 0xf0) == 0x20;
  bool btn_b_down = (state->right_btns_data & 0xf0) == 0x10;
  bool btn_select_down = (state->mid_btns_data & 0xf0) == 0x10;
  bool btn_start_down = (state->mid_btns_data & 0xf0) == 0x20;

  // Debug state
  //printf("L: %d, R: %d, U: %d, D: %d, A: %d, B: %d, SEL: %d, start: %d\n", dpad_left, dpad_right, dpad_up, dpad_down, btn_a_down, btn_b_down, btn_select_down, btn_start_down);

  // Fake axis
  gamepadState.axes[GamepadAxis_LeftStick_X] = 0;
  if(dpad_left)
    gamepadState.axes[GamepadAxis_LeftStick_X] -= 1;
  if(dpad_right)
    gamepadState.axes[GamepadAxis_LeftStick_X] += 1;  

  gamepadState.axes[GamepadAxis_LeftStick_Y] = 0;
  if(dpad_up)
    gamepadState.axes[GamepadAxis_LeftStick_Y] -= 1;
  if(dpad_down)
    gamepadState.axes[GamepadAxis_LeftStick_Y] += 1;  

  // Dpad
  gamepadState.dpad = 0;
  if(dpad_left)
    gamepadState.dpad |= GamepadDpad_Left;
  if(dpad_right)
    gamepadState.dpad |= GamepadDpad_Right;
  if(dpad_up)
    gamepadState.dpad |= GamepadDpad_Up;
  if(dpad_down)
    gamepadState.dpad |= GamepadDpad_Down;

  // Buttons (mapping)
  gamepadState.buttons = 0;
  if(btn_a_down)
    gamepadState.buttons |= GamepadButton_B;    // Nes style BA flipped, A on left
  if(btn_b_down)
    gamepadState.buttons |= GamepadButton_A;    // Nes style BA flipped, A on left
  if(btn_select_down)
    gamepadState.buttons |= GamepadButton_Select;  
  if(btn_start_down)
    gamepadState.buttons |= GamepadButton_Start;    

  if(memcmp(&lastState,&gamepadState,sizeof(lastState)) == 0)
    return;

  // append hid state
  if(hidState.gamepadCnt >= INPUT_HID_MAX_MOUSE_BUFFER_CNT)
  {
    hidState.gamepadOverflowCnt++;
    return;
  }
  hidState.gamepad[hidState.gamepadCnt++] = gamepadState;
  hidState.id++;  

  lastState = gamepadState;
}


static void process_mouse_report( hid_mouse_report_t const* report )
{
  // Update state
  mouseState.id++;
  mouseState.deltaX = report->x;
  mouseState.deltaY = report->y;
  mouseState.deltaScroll = report->wheel;

  // capture button state
  mouseState.buttons = 0;
  if( report->buttons & MOUSE_BUTTON_LEFT )
    mouseState.buttons |= 1 << 0;
  if( report->buttons & MOUSE_BUTTON_RIGHT )
    mouseState.buttons |= 1 << 1;    
  if( report->buttons & MOUSE_BUTTON_MIDDLE )
    mouseState.buttons |= 1 << 2;      
  // Extras
  if( report->buttons & MOUSE_BUTTON_BACKWARD )
    mouseState.buttons |= 1 << 3;        
  if( report->buttons & MOUSE_BUTTON_FORWARD )
    mouseState.buttons |= 1 << 4;          

  // append hid state
  if(hidState.mouseCnt >= INPUT_HID_MAX_MOUSE_BUFFER_CNT)
  {
    hidState.mouseOverflowCnt++;
    return;
  }
  hidState.mouse[hidState.mouseCnt++] = mouseState;
  hidState.id++;
}


static void process_kbd_report(hid_keyboard_report_t const *report)
{
  keyboardState.id++;  
  keyboardState.modifier = report->modifier;
  for(int i=0;i<6;i++)
    keyboardState.keycode[i] = report->keycode[i];

  // append hid state
  if(hidState.keyboardCnt >= INPUT_HID_MAX_MOUSE_BUFFER_CNT)
  {
    hidState.keyboardOverflowCnt++;
    return;
  }
  hidState.keyboard[hidState.keyboardCnt++] = keyboardState;
  hidState.id++;  
}


static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) dev_addr;
  (void) len;

  // Check for specific controllers
  if( is_sony_ds4(dev_addr) )
  {
    process_soy_ds4(report, len);
    return;
  } 
  else if( is_generic_dragon_crap(dev_addr) )
  {
    process_dragon_crap(report, len);
    return;
  }

  // Generic HID
  uint8_t const rpt_count = hid_info[instance].report_count;
  tuh_hid_report_info_t* rpt_info_arr = hid_info[instance].report_info;
  tuh_hid_report_info_t* rpt_info = NULL;

  if ( rpt_count == 1 && rpt_info_arr[0].report_id == 0)
  {
    rpt_info = &rpt_info_arr[0];
  }
  else
  {
    uint8_t const rpt_id = report[0];

    for(uint8_t i=0; i<rpt_count; i++)
    {
      if (rpt_id == rpt_info_arr[i].report_id )
      {
        rpt_info = &rpt_info_arr[i];
        break;
      }
    }

    report++;
    len--;
  }

  if (!rpt_info)
  {
    printf("Couldn't find report info !\r\n");
    return;
  }

  if ( rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP )
  {
    switch (rpt_info->usage)
    {
      case HID_USAGE_DESKTOP_KEYBOARD:
        process_kbd_report( (hid_keyboard_report_t const*) report );
      break;

      case HID_USAGE_DESKTOP_MOUSE:
        process_mouse_report( (hid_mouse_report_t const*) report );
      break;
      default: break;
    }
  }
}


void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
   uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  switch (itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      process_kbd_report( (hid_keyboard_report_t const*) report );
    break;

    case HID_ITF_PROTOCOL_MOUSE:
      process_mouse_report( (hid_mouse_report_t const*) report );
    break;

    default:
      process_generic_report(dev_addr, instance, report, len);
    break;
  }

  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    printf("Error: cannot request to receive report\r\n");
  }
}
