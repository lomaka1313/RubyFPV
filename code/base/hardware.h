#pragma once
#include "config.h"
#include "hardware_radio.h"
#include "hardware_radio_sik.h"
#include "hardware_i2c.h"
#include "hardware_serial.h"


#define WIFI_TYPE_NONE 0
#define WIFI_TYPE_A 1 // per bits
#define WIFI_TYPE_G 2 // per bits
#define WIFI_TYPE_AG 3 // per bits


#define BOARD_TYPE_MASK (u32)0xFF
#define BOARD_SUBTYPE_MASK (u32)0xFF00
#define BOARD_SUBTYPE_SHIFT 8

#define BOARD_TYPE_NONE     0
#define BOARD_TYPE_PIZERO   1
#define BOARD_TYPE_PIZEROW  2
#define BOARD_TYPE_PIZERO2  3
#define BOARD_TYPE_PI2B     5
#define BOARD_TYPE_PI2BV11  6
#define BOARD_TYPE_PI2BV12  7
#define BOARD_TYPE_PI3APLUS 10
#define BOARD_TYPE_PI3B     15
#define BOARD_TYPE_PI3BPLUS 16
#define BOARD_TYPE_PI4B     20

#define BOARD_TYPE_OPENIPC_GOKE200 30
#define BOARD_TYPE_OPENIPC_GOKE210 31
#define BOARD_TYPE_OPENIPC_GOKE300 32

#define BOARD_TYPE_OPENIPC_SIGMASTAR_338Q 40

#define BOARD_SUBTYPE_OPENIPC_UNKNOWN  0
#define BOARD_SUBTYPE_OPENIPC_GENERIC         ((u32)1)
#define BOARD_SUBTYPE_OPENIPC_GENERIC_30KQ    ((u32)2)
#define BOARD_SUBTYPE_OPENIPC_AIO_ULTRASIGHT  ((u32)3)
#define BOARD_SUBTYPE_OPENIPC_AIO_MARIO       ((u32)4)
#define BOARD_SUBTYPE_OPENIPC_AIO_RUNCAM      ((u32)5)
#define BOARD_SUBTYPE_OPENIPC_AIO_EMAX_MINI   ((u32)6)
#define BOARD_SUBTYPE_OPENIPC_AIO_EMAX        ((u32)7)
#define BOARD_SUBTYPE_OPENIPC_AIO_THINKER     ((u32)8)
#define BOARD_SUBTYPE_OPENIPC_AIO_THINKER_E   ((u32)9)
#define BOARD_SUBTYPE_OPENIPC_LAST            ((u32)10)


#define BOARD_TYPE_RADXA_ZERO3 60
#define BOARD_TYPE_RADXA_3C 61


#define CAMERA_TYPE_NONE 0
#define CAMERA_TYPE_CSI   1
#define CAMERA_TYPE_VEYE290  3
#define CAMERA_TYPE_VEYE307  4
#define CAMERA_TYPE_VEYE327  5
#define CAMERA_TYPE_HDMI  7
#define CAMERA_TYPE_IP    8
#define CAMERA_TYPE_OPENIPC_IMX307 20
#define CAMERA_TYPE_OPENIPC_IMX335 21
#define CAMERA_TYPE_OPENIPC_IMX415 22


#define MAX_JOYSTICK_INTERFACE_NAME 128
#define MAX_JOYSTICK_INTERFACES 4
#define MAX_JOYSTICK_AXES 24
#define MAX_JOYSTICK_BUTTONS 24

#ifdef __cplusplus
extern "C" {
#endif 


typedef struct
{
   int deviceIndex;
   char szName[MAX_JOYSTICK_INTERFACE_NAME];
   u32 uId;
   u8 countAxes;
   u8 countButtons;
   int axesValues[MAX_JOYSTICK_AXES];
   int buttonsValues[MAX_JOYSTICK_BUTTONS];
   int axesValuesPrev[MAX_JOYSTICK_AXES];
   int buttonsValuesPrev[MAX_JOYSTICK_BUTTONS];
   int fd;
} hw_joystick_info_t;



int init_hardware();
int init_hardware_only_status_led();
void _hardware_load_system_type();

void hardware_reboot();

void hardware_release();
void hardware_loop();
void hardware_swap_buttons(int swap);

u32 hardware_getOnlyBoardType();
u32 hardware_getBoardType();

int hardware_board_is_raspberry(u32 uBoardType);
int hardware_board_is_radxa(u32 uBoardType);
int hardware_board_is_openipc(u32 uBoardType);
int hardware_board_is_goke(u32 uBoardType);
int hardware_board_is_sigmastar(u32 uBoardType);

void hardware_enum_joystick_interfaces();
int hardware_get_joystick_interfaces_count();
hw_joystick_info_t* hardware_get_joystick_info(int index);
int hardware_open_joystick(int joystickIndex);
void hardware_close_joystick(int joystickIndex);
int hardware_read_joystick(int joystickIndex, int miliSec);
int hardware_is_joystick_opened(int joystickIndex);

u16 hardware_get_flags();

void hardware_setCriticalErrorFlag();
void hardware_setRecoverableErrorFlag();
void hardware_flashGreenLed();

int isKeyMenuPressed();
int isKeyBackPressed();
int isKeyPlusPressed();
int isKeyMinusPressed();
int isKeyQA1Pressed();
int isKeyQA2Pressed();
int isKeyQA3Pressed();

int isKeyMenuLongPressed();
int isKeyMenuLongLongPressed();
int isKeyBackLongPressed();
int isKeyPlusLongPressed();
int isKeyMinusLongPressed();
int isKeyPlusLongLongPressed();
int isKeyMinusLongLongPressed();

void hardware_override_keys(int iKeyMenu, int iKeyBack, int iKeyPlus, int iKeyMinus, int iKeyMenuLong, int iKeyBackLong, int iKeyPlusLong, int iKeyMinusLong);

void hardware_blockCurrentPressedKeys();

int hardware_try_mount_usb();
void hardware_unmount_usb();
char* hardware_get_mounted_usb_name();

int hardware_is_station();
int hardware_is_vehicle();
int hardware_is_running_on_openipc();

void hardware_recording_led_set_off();
void hardware_recording_led_set_on();
void hardware_recording_led_set_blinking();

char* hardware_has_eth();

void hardware_set_default_sigmastar_cpu_freq();
void hardware_set_default_radxa_cpu_freq();
int hardware_get_cpu_speed(); // in Mhz
int hardware_get_gpu_speed(); // in Mhz

void hardware_set_oipc_freq_boost(int iFreqCPUMhz, int iGPUBoost);
void hardware_set_oipc_gpu_boost(int iGPUBoost);
void hardware_balance_interupts();
#ifdef __cplusplus
}  
#endif 