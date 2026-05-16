// This is some very ancient (and, if we're being frank, very
// awful) code dedicated to the Ultracade Ushock device, which 
// (one gathers from reading the code) was an early, primitive
// predecessor of HID output controller devices like the LedWiz.
// The Ushock has a single output for a pinball knocker.  This
// code implements bespoke Win32 HID API access to the Ushock
// specifically.  ("PBW" is another name seen a lot here, which
// I believe stands for PinballBallWizard, which as I recall is
// another product from the same vendor.  Or maybe the same one.
// Who knows.)
// 
// The entire concept of this module has long since been
// superseded by DOF (DirectOutput Framework), which is why
// you don't see a bunch of similar modules for LedWiz's,
// PacLed's, and a dozen other devices, thank goodness.
//
// This code was formerly named with the extremely presumptuous
// prefix "hid_" for all of its public functions.  It is most
// certainly not "hid" code generically; it happens to *use*
// HID to do its one extremely narrow job, but naming everything
// here "hid_xxx" makes about as much sense as naming it "c_xxx" 
// because it's written in C.  So, as of 9/2024, it has been
// renamed to more properly indicate its function.  (This wasn't 
// just because its old naming was so piquing, but rather because
// the old naming was creating a collision with the hidapi library,
// which also uses hid_xxx as its naming convention.  Of the two,
// I think hidapi has the far better claim on the name.)

#include "core/stdafx.h"

#include "utils/ushock_output.h"

// This code should be understandable using
// the following URL:
// http://www.edn.com/article/CA243218.html
// 
// [which is, naturally, a dead link; but the code is just basic
// Win32 HID enumeration and access code, and there are gazillions
// of rote examples on stackoverflow that do basically the same
// things]

static HANDLE connectToIthUSBHIDDevice(DWORD deviceIndex)
{
   return nullptr;
}

static HANDLE hid_connect(uint32_t vendorID, uint32_t productID, uint32_t * const versionNumber = nullptr)
{

   return INVALID_HANDLE_VALUE;
}

static HANDLE hnd = hid_connect(0x04b4, 0x6470);

void ushock_output_init()
{
}


static uint32_t sMask = 0;


// This is the main interface to turn output on and off.
// Once set, the value will remain set until another set call is made.
// The output_mask parameter uses any combination of HID_OUTPUT enum.
void ushock_output_set(const uint8_t output_mask, const bool on)
{
   // Check if the outputs are being turned on.
   if (on)
   {
      sMask |= output_mask;
   }
   else
   {
      sMask &= ~output_mask;
   }
}


#define KNOCK_PERIOD_ON  50
#define KNOCK_PERIOD_OFF 500

static int sKnock;
static int sKnockState;
static uint32_t sKnockStamp;


void ushock_output_knock(const int count)
{
   if (count)
   {
      sKnock = count;
      sKnockStamp = g_pplayer->m_time_msec;
      sKnockState = 1;
   }
}


void ushock_output_update(const uint32_t cur_time_msec)
{
}

void ushock_output_shutdown()
{
   if (hnd != INVALID_HANDLE_VALUE)
   {
      hnd = INVALID_HANDLE_VALUE;
   }
}
