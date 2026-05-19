#pragma once

#include <hidsdi.h>
#include <hidpi.h>

enum VjdStat
{
	VJD_STAT_OWN,
	VJD_STAT_FREE,
	VJD_STAT_BUSY,
	VJD_STAT_MISS,
	VJD_STAT_UNKN
};

// Standard vJoy structure missing from some SDK versions
typedef struct _JOYSTICK_POSITION_V2
{
    BYTE    bDevice;
    LONG    wThrottle;
    LONG    wRudder;
    LONG    wAileron;
    LONG    wAxisX;
    LONG    wAxisY;
    LONG    wAxisZ;
    LONG    wAxisXRot;
    LONG    wAxisYRot;
    LONG    wAxisZRot;
    LONG    wSlider;
    LONG    wDial;
    LONG    wWheel;
    LONG    wAxisVX;
    LONG    wAxisVY;
    LONG    wAxisVZ;
    LONG    wAxisVBRX;
    LONG    wAxisVBRY;
    LONG    wAxisVBRZ;
    LONG    lButtons;
    DWORD   bHats;
    DWORD   bHatsEx1;
    DWORD   bHatsEx2;
    DWORD   bHatsEx3;
    LONG    lButtonsEx1;
    LONG    lButtonsEx2;
    LONG    lButtonsEx3;
} JOYSTICK_POSITION_V2, *PJOYSTICK_POSITION_V2;

// Missing HID ID definitions for FFB
#ifndef HID_ID_EFFREP
#define HID_ID_EFFREP 0x01
#define HID_ID_ENVREP 0x02
#define HID_ID_CONDREP 0x03
#define HID_ID_PRIDREP 0x04
#define HID_ID_CONSTREP 0x05
#define HID_ID_RAMPREP 0x06
#define HID_ID_CSTMREP 0x07
#define HID_ID_SMPLREP 0x08
#define HID_ID_EFOPREP 0x09
#define HID_ID_BLKFRREP 0x0A
#define HID_ID_CTRLREP 0x0B
#define HID_ID_GAINREP 0x0C
#define HID_ID_SETCREP 0x0D
#define HID_ID_NEWEFREP 0x0E
#define HID_ID_BLKLDREP 0x0F
#define HID_ID_POOLREP 0x10
#endif

class vJoyFeeder {
public:
    vJoyFeeder(UINT deviceId);
    ~vJoyFeeder();
    bool Initialize();
    void UpdateThrottleAxis(float value);
    bool UpdateAxis(long value, UINT usage);
    bool SetButton(bool pressed, UCHAR buttonId);

private:
    UINT m_deviceId;
    bool m_initialized;
};
