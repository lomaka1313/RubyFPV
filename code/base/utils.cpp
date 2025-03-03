/*
    Ruby Licence
    Copyright (c) 2025 Petru Soroaga
    All rights reserved.

    Redistribution and/or use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions and/or use of the source code (partially or complete) must retain
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
        * Redistributions in binary form (partially or complete) must reproduce
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
        * Copyright info and developer info must be preserved as is in the user
        interface, additions could be made to that info.
        * Neither the name of the organization nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.
        * Military use is not permitted.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE AUTHOR (PETRU SOROAGA) BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "utils.h"
#include <math.h>
#include "../base/config.h"
#include "../base/models.h"
#include "../base/hw_procs.h"
#include "../common/string_utils.h"
#include "../radio/radioflags.h"

const double PIx = 3.141592653589793;
const double RADIUS_EARTH = 6371.0; // Mean radius of Earth in Km

bool ruby_is_first_pairing_done()
{
   char szFile[MAX_FILE_PATH_SIZE];
   strcpy(szFile, FOLDER_CONFIG);
   strcat(szFile, FILE_CONFIG_FIRST_PAIRING_DONE); 
   if ( access(szFile, R_OK) == -1 )
      return false;
   return true;
}

void ruby_set_is_first_pairing_done()
{
   char szFile[MAX_FILE_PATH_SIZE];
   strcpy(szFile, FOLDER_CONFIG);
   strcat(szFile, FILE_CONFIG_FIRST_PAIRING_DONE); 
   FILE* fd = fopen(szFile, "w");
   if ( NULL == fd )
   {
      log_softerror_and_alarm("Failed to create file for marking 'first pairing done' flag.");
      return;
   }
   fprintf(fd, "1");
   fclose(fd);
   log_line("Set 'first pairing done' flag to true.");
}

void reset_sik_state_info(t_sik_radio_state* pState)
{
   if ( NULL == pState )
      return;

   pState->bConfiguringSiKThreadWorking = false;
   pState->iThreadRetryCounter = 0;
   pState->bMustReinitSiKInterfaces = false;
   pState->iMustReconfigureSiKInterfaceIndex = -1;
   pState->uTimeLastSiKReinitCheck = 0;
   pState->uTimeIntervalSiKReinitCheck = 500;
   pState->uSiKInterfaceIndexThatBrokeDown = MAX_U32;

   pState->bConfiguringToolInProgress = false;
   pState->uTimeStartConfiguring = 0;
   
   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
      pState->bInterfacesToReopen[i] = false;
}

int _compute_controller_rc_value_button(Model* pModel, int nChannel, int prevRCValue, hw_joystick_info_t* pJoystick, t_ControllerInputInterface* pCtrlInterface)
{
   u32 dwAssignment = pModel->rc_params.rcChAssignment[nChannel];

   // Check to match description from models.h and config_rc.h bit flags
   // first byte:
          // bit 0: no assignment (0/1);
          // bit 1: 0 - axe, 1 - button;
          // bit 2: 0 - momentary, 1 - sticky button (togle);
          // bit 3: not used
          // bit 4..7 axe/button index (1 index based: first button/axe is 1)
   // each additional 4 bits: positive value (!=0) for each extra button (in increasing order of output RC value)
   // bit 31: toggle flag


   int buttonsIndexes[12];
   int countButtons = 0;

   // Get all assigned buttons to this channel

   for( int shift = 4; shift <= 24; shift += 4 )
   {
      if ( ((dwAssignment >> shift) & 0x0F ) != 0x00 )
      {
         buttonsIndexes[countButtons] = ((dwAssignment >> shift) & 0x0F) - 1;
         countButtons++;
      }
   }

   if ( 0 == countButtons )
      return 0;

   int result = 0;
   int rawValue = 0;

   bool bToggleMode = false;
   if ( dwAssignment & RC_CH_ASSIGNMENT_FLAG_TOGGLE )
      bToggleMode = true;

   //log_line("ch %d, tgl: %d", nChannel, bToggle);

   // Non toggle buttons ?

   if ( ! bToggleMode )
   {
      if ( 1 == countButtons && buttonsIndexes[0] < pJoystick->countButtons )
      {
         rawValue = pJoystick->buttonsValues[buttonsIndexes[0]];

         if ( rawValue == 0 )
         {
            result = pModel->rc_params.rcChMin[nChannel];
            if ( pModel->rc_params.rcChFlags[nChannel] & RC_CH_FLAGS_INVERTED )
               result = pModel->rc_params.rcChMax[nChannel];
         }
         else
         {
            result = pModel->rc_params.rcChMax[nChannel];
            if ( pModel->rc_params.rcChFlags[nChannel] & RC_CH_FLAGS_INVERTED )
               result = pModel->rc_params.rcChMin[nChannel];
         }
         return result;
      }

      result = pModel->rc_params.rcChMin[nChannel];
      if ( pModel->rc_params.rcChFlags[nChannel] & RC_CH_FLAGS_INVERTED )
         result = pModel->rc_params.rcChMax[nChannel];

      for( int i=0; i<countButtons; i++ )
      {
         if ( buttonsIndexes[i] >= pJoystick->countButtons )
            continue;
         rawValue = pJoystick->buttonsValues[buttonsIndexes[i]];
         if ( rawValue != 0 )
         {
            result = pModel->rc_params.rcChMin[nChannel] + (pModel->rc_params.rcChMax[nChannel]-pModel->rc_params.rcChMin[nChannel])*(i+1)/countButtons;
            if ( pModel->rc_params.rcChFlags[nChannel] & RC_CH_FLAGS_INVERTED )
               result = pModel->rc_params.rcChMax[nChannel] - (pModel->rc_params.rcChMax[nChannel]-pModel->rc_params.rcChMin[nChannel])*(i+1)/countButtons;
            return result;
         }
      }
      return result;
   }

   // Toggle buttons

   if ( 1 == countButtons )
   {
      if ( 0 == prevRCValue )
      {
         result = pModel->rc_params.rcChMin[nChannel];
         if ( pModel->rc_params.rcChFlags[nChannel] & RC_CH_FLAGS_INVERTED )
            result = pModel->rc_params.rcChMax[nChannel];
      }
      else
         result = prevRCValue;

      rawValue = pJoystick->buttonsValues[buttonsIndexes[0]];

      if ( rawValue != 0 && 0 == pJoystick->buttonsValuesPrev[buttonsIndexes[0]])
      {
         if ( prevRCValue >= (pModel->rc_params.rcChMax[nChannel]+pModel->rc_params.rcChMin[nChannel])/2 )
            result = pModel->rc_params.rcChMin[nChannel];
         else
            result = pModel->rc_params.rcChMax[nChannel];
      }
      return result;
   }

   // Multiple toggle buttons

   if ( 0 == prevRCValue )
   {
      result = pModel->rc_params.rcChMin[nChannel];
      if ( pModel->rc_params.rcChFlags[nChannel] & RC_CH_FLAGS_INVERTED )
         result = pModel->rc_params.rcChMax[nChannel];
   }
   else
      result = prevRCValue;

   for( int i=0; i<countButtons; i++ )
   {
      if ( buttonsIndexes[i] >= pJoystick->countButtons )
         continue;
      rawValue = pJoystick->buttonsValues[buttonsIndexes[i]];
      if ( rawValue != 0 && 0 == pJoystick->buttonsValuesPrev[buttonsIndexes[i]])
      {
         result = pModel->rc_params.rcChMin[nChannel] + (pModel->rc_params.rcChMax[nChannel]-pModel->rc_params.rcChMin[nChannel])*i/(countButtons-1);
         if ( pModel->rc_params.rcChFlags[nChannel] & RC_CH_FLAGS_INVERTED )
            result = pModel->rc_params.rcChMax[nChannel] - (pModel->rc_params.rcChMax[nChannel]-pModel->rc_params.rcChMin[nChannel])*i/(countButtons-1);
         return result;
      }
   }

   return result;
}

float _compute_controller_rc_ranged_value(Model* pModel, int nChannel, float prevRCValue, float fNormalizedValue, u32 miliSec)
{
   float rcValue = pModel->rc_params.rcChMid[nChannel];

   if ( fNormalizedValue < 0.0 ) fNormalizedValue = 0.0;
   if ( fNormalizedValue > 1.0 ) fNormalizedValue = 1.0;

   if ( pModel->rc_params.rcChFlags[nChannel] & RC_CH_FLAGS_INVERTED )
      fNormalizedValue = 1.0 - fNormalizedValue;

   int nCamSpeed = ((pModel->camera_rc_channels >> 24) & 0x1F);
   if ( nCamSpeed <= 0 )
      nCamSpeed = 1;

   int nCamPitch = (pModel->camera_rc_channels & 0x1F);
   int nCamRoll = ((pModel->camera_rc_channels >> 8) & 0x1F);
   int nCamYaw = ((pModel->camera_rc_channels >> 16) & 0x1F);
   
   bool isRelativeMove = false;

   if ( nCamPitch > 0 && (nChannel == nCamPitch-1) )
      isRelativeMove = true;
   if ( nCamRoll > 0 && (nChannel == nCamRoll-1) )
      isRelativeMove = true;
   if ( nCamYaw > 0 && (nChannel == nCamYaw-1) )
      isRelativeMove = true;

   if ( (((pModel->camera_rc_channels >> 24) & 0xFF) >> 5) & 0x01 )
      isRelativeMove = false;


   if ( fNormalizedValue >= 0.5 )
   {
      fNormalizedValue = (fNormalizedValue-0.5)*2.0;
      fNormalizedValue = powf(fNormalizedValue, 1.0+0.1*(float)pModel->rc_params.rcChExpo[nChannel]) * 0.7 + 0.3 * fNormalizedValue;

      if ( isRelativeMove )
      {
         if ( prevRCValue < 10 )
            prevRCValue = pModel->rc_params.rcChMid[nChannel];
         if ( fNormalizedValue > 0.0001 )
            rcValue = prevRCValue + fNormalizedValue * ((float)nCamSpeed) * ((float)miliSec/20.0);
         else
            rcValue = prevRCValue;
         if ( rcValue > pModel->rc_params.rcChMax[nChannel] )
            rcValue = pModel->rc_params.rcChMax[nChannel];
         if ( rcValue < pModel->rc_params.rcChMin[nChannel] )
            rcValue = pModel->rc_params.rcChMin[nChannel];
         return rcValue;
      }
      rcValue = pModel->rc_params.rcChMid[nChannel] + fNormalizedValue*(float)(pModel->rc_params.rcChMax[nChannel] - pModel->rc_params.rcChMid[nChannel]);
   }
   else
   {
      fNormalizedValue = (0.5-fNormalizedValue)*2.0;
      fNormalizedValue = powf(fNormalizedValue, 1.0+0.1*(float)pModel->rc_params.rcChExpo[nChannel]) * 0.7 + 0.3 * fNormalizedValue;

      if ( isRelativeMove )
      {
         if ( prevRCValue < 10 )
            prevRCValue = pModel->rc_params.rcChMid[nChannel];
         if ( fNormalizedValue > 0.0001 )
            rcValue = prevRCValue - fNormalizedValue * ((float)nCamSpeed) * ((float)miliSec/20.0);
         else
            rcValue = prevRCValue;
         if ( rcValue > pModel->rc_params.rcChMax[nChannel] )
            rcValue = pModel->rc_params.rcChMax[nChannel];
         if ( rcValue < pModel->rc_params.rcChMin[nChannel] )
            rcValue = pModel->rc_params.rcChMin[nChannel];
         return rcValue;
      }

      rcValue = pModel->rc_params.rcChMid[nChannel] - fNormalizedValue*(float)(pModel->rc_params.rcChMid[nChannel] - pModel->rc_params.rcChMin[nChannel]);
   }
   return rcValue;
}

float compute_output_rc_value(Model* pModel, int nChannel, float prevRCValue, float fNormalizedValue, u32 miliSec)
{
   return _compute_controller_rc_ranged_value(pModel, nChannel, prevRCValue, fNormalizedValue, miliSec);
}

float _compute_controller_rc_value_axe(Model* pModel, int nChannel, float prevRCValue, hw_joystick_info_t* pJoystick, t_ControllerInputInterface* pCtrlInterface, u32 miliSec)
{
   // Check to match description from models.h and config_rc.h bit flags
   // first byte:
          // bit 0: no assignment (0/1);
          // bit 1: 0 - axe, 1 - button;
          // bit 2: 0 - momentary, 1 - sticky button (togle);
          // bit 3: not used
          // bit 4..7 axe/button index (1 index based: first button/axe is 1)
   // each additional 4 bits: positive value (!=0) for each extra button (in increasing order of output RC value)
   // bit 31: toggle flag
   
   // 0...1
   float fNormalizedValue = 0.5;

   u32 dwAssignment = pModel->rc_params.rcChAssignment[nChannel];
   int nAxe = ((dwAssignment & 0xFF) >> 4) - 1;
   if ( nAxe >= pJoystick->countAxes )
      return _compute_controller_rc_ranged_value(pModel, nChannel, prevRCValue, fNormalizedValue, miliSec);

   if ( fabs(pCtrlInterface->axesMaxValue[nAxe] - pCtrlInterface->axesMinValue[nAxe]) < 0.01 )
      return _compute_controller_rc_ranged_value(pModel, nChannel, prevRCValue, fNormalizedValue, miliSec);

   int rawValue = pJoystick->axesValues[nAxe];

   float fCenterZonePercent = (float)pCtrlInterface->axesCenterZone[nAxe]/10.0/100.0;
   float rawCenterSize = fCenterZonePercent*fabs(pCtrlInterface->axesMaxValue[nAxe] - pCtrlInterface->axesMinValue[nAxe]);

   float fMidPercent = fabs(pCtrlInterface->axesCenterValue[nAxe]-pCtrlInterface->axesMinValue[nAxe]) / fabs(pCtrlInterface->axesMaxValue[nAxe] - pCtrlInterface->axesMinValue[nAxe]);

   // Throttle with reverse/fwd?

   if ( (pModel->vehicle_type & MODEL_TYPE_MASK) == MODEL_TYPE_CAR || (pModel->vehicle_type & MODEL_TYPE_MASK) == MODEL_TYPE_BOAT || (pModel->vehicle_type & MODEL_TYPE_MASK) == MODEL_TYPE_ROBOT )
   if ( nChannel == 2 && NULL != pModel && (pModel->rc_params.rcChAssignmentThrotleReverse & RC_CH_ASSIGNMENT_FLAG_ASSIGNED) )
   {
      fNormalizedValue = (float)(rawValue - pCtrlInterface->axesMinValue[nAxe])/(float)(pCtrlInterface->axesMaxValue[nAxe] - pCtrlInterface->axesMinValue[nAxe]);
      if ( fNormalizedValue < 0.0 ) fNormalizedValue = 0.0;
      if ( fNormalizedValue > 1.0 ) fNormalizedValue = 1.0;
   
      if ( pModel->rc_params.rcChFlags[nChannel] & RC_CH_FLAGS_INVERTED )
         fNormalizedValue = 1.0 - fNormalizedValue;

      bool bIsInReverse = false;

      dwAssignment = pModel->rc_params.rcChAssignmentThrotleReverse;

      if ( pModel->rc_params.rcChAssignmentThrotleReverse & RC_CH_ASSIGNMENT_FLAG_BUTTON )
      {
         int buttonsIndexes[12];
         int countButtons = 0;

         // Get all assigned buttons to reverse/fwd

         for( int shift = 4; shift <= 24; shift += 4 )
         {
            if ( ((dwAssignment >> shift) & 0x0F ) != 0x00 )
            {
               buttonsIndexes[countButtons] = ((dwAssignment >> shift) & 0x0F) - 1;
               countButtons++;
            }
         }

         if ( 1 == countButtons && buttonsIndexes[0] < pJoystick->countButtons )
         {
            rawValue = pJoystick->buttonsValues[buttonsIndexes[0]];

            if ( rawValue != 0 )
               bIsInReverse = true;
         }
      }
      else
      {
         nAxe = ((dwAssignment & 0xFF) >> 4) - 1;
         if ( nAxe < pJoystick->countAxes )
         {
            rawValue = pJoystick->axesValues[nAxe];
            fNormalizedValue = (float)(rawValue - pCtrlInterface->axesMinValue[nAxe])/(float)(pCtrlInterface->axesMaxValue[nAxe] - pCtrlInterface->axesMinValue[nAxe]);
            if ( fNormalizedValue < 0.0 ) fNormalizedValue = 0.0;
            if ( fNormalizedValue > 1.0 ) fNormalizedValue = 1.0;
            if ( fNormalizedValue > 0.5 )
               bIsInReverse = true;
         }
      }
      float rcValue = pModel->rc_params.rcChMid[nChannel];
      if ( bIsInReverse )
         rcValue = pModel->rc_params.rcChMid[nChannel] - fNormalizedValue*(float)(pModel->rc_params.rcChMid[nChannel] - pModel->rc_params.rcChMin[nChannel]);
      else
         rcValue = pModel->rc_params.rcChMid[nChannel] + fNormalizedValue*(float)(pModel->rc_params.rcChMax[nChannel] - pModel->rc_params.rcChMid[nChannel]);
      return rcValue;
   }

   // Linear axe with no center? (No real middle center)

   if ( fMidPercent < 0.45 || fMidPercent > 0.55 )
   {
      fNormalizedValue = (float)(rawValue - pCtrlInterface->axesMinValue[nAxe])/(float)(pCtrlInterface->axesMaxValue[nAxe] - pCtrlInterface->axesMinValue[nAxe]);
      return _compute_controller_rc_ranged_value(pModel, nChannel, prevRCValue, fNormalizedValue, miliSec);
   }


   // Upper half

   if ( rawValue > pCtrlInterface->axesCenterValue[nAxe] + rawCenterSize )
   {
      if ( rawValue > pCtrlInterface->axesMaxValue[nAxe] )
         rawValue = pCtrlInterface->axesMaxValue[nAxe];

      fNormalizedValue = 0.5 + 0.5 * (float)(rawValue - pCtrlInterface->axesCenterValue[nAxe] - rawCenterSize)/(float)(pCtrlInterface->axesMaxValue[nAxe]-pCtrlInterface->axesCenterValue[nAxe]-rawCenterSize);
      return _compute_controller_rc_ranged_value(pModel, nChannel, prevRCValue, fNormalizedValue, miliSec);
   }

   // Lower half

   if ( rawValue < pCtrlInterface->axesCenterValue[nAxe] - rawCenterSize )
   {
      if ( rawValue < pCtrlInterface->axesMinValue[nAxe] )
         rawValue = pCtrlInterface->axesMinValue[nAxe];

      fNormalizedValue = 0.5 - 0.5*(float)(pCtrlInterface->axesCenterValue[nAxe] - rawValue - rawCenterSize)/(float)(pCtrlInterface->axesCenterValue[nAxe]-pCtrlInterface->axesMinValue[nAxe] - rawCenterSize);
      return _compute_controller_rc_ranged_value(pModel, nChannel, prevRCValue, fNormalizedValue, miliSec);
   }

   return _compute_controller_rc_ranged_value(pModel, nChannel, prevRCValue, fNormalizedValue, miliSec);
}

float compute_controller_rc_value(Model* pModel, int nChannel, float prevRCValue, t_shared_mem_i2c_controller_rc_in* pRCIn, hw_joystick_info_t* pJoystick, t_ControllerInputInterface* pCtrlInterface, u32 miliSec)
{
   if ( NULL == pModel )
      return 0;
   if ( nChannel < 0 || nChannel >= (int) pModel->rc_params.channelsCount )
      return 0;

   if ( pModel->rc_params.inputType == RC_INPUT_TYPE_RC_IN_SBUS_IBUS )
   {
      if ( NULL == pRCIn )
         return get_rc_channel_failsafe_value(pModel, nChannel, prevRCValue);
      if ( ! (pRCIn->uFlags & RC_IN_FLAG_HAS_INPUT) )
         return get_rc_channel_failsafe_value(pModel, nChannel, prevRCValue);

      if ( nChannel >= (int) pRCIn->uChannelsCount )
         return 0;
      if ( pModel->rc_params.iRCTranslationType == RC_TRANSLATION_TYPE_2000 )
         return 1000 + (float)(pRCIn->uChannels[nChannel])/2.0;
      if ( pModel->rc_params.iRCTranslationType == RC_TRANSLATION_TYPE_4000 )
         return 1000 + (float)(pRCIn->uChannels[nChannel])/4.0;
      else
         return (float)(pRCIn->uChannels[nChannel]);
   }

   if ( pModel->rc_params.inputType == RC_INPUT_TYPE_USB )
   {
      if ( NULL == pJoystick || NULL == pCtrlInterface )
         return get_rc_channel_failsafe_value(pModel, nChannel, prevRCValue);

      if ( (pModel->rc_params.rcChAssignment[nChannel] & RC_CH_ASSIGNMENT_FLAG_ASSIGNED) == 0 )
      {
          if ( pModel->rc_params.rcChFlags[nChannel] & RC_CH_FLAGS_INVERTED )
             return pModel->rc_params.rcChMax[nChannel];
          return pModel->rc_params.rcChMin[nChannel];
      }

      if ( (pModel->rc_params.rcChAssignment[nChannel] & RC_CH_ASSIGNMENT_FLAG_BUTTON) == 0 )
         return _compute_controller_rc_value_axe(pModel, nChannel, prevRCValue, pJoystick, pCtrlInterface, miliSec);
      else
         return _compute_controller_rc_value_button(pModel, nChannel, prevRCValue, pJoystick, pCtrlInterface);
   }
   return 0;
}

u32 get_rc_channel_failsafe_type(Model* pModel, int nChannel)
{
   if ( NULL == pModel )
      return RC_FAILSAFE_NOOUTPUT;
   if ( nChannel < 0 || nChannel >= MAX_RC_CHANNELS )
      return RC_FAILSAFE_NOOUTPUT;

   if ( pModel->rc_params.failsafeFlags == RC_FAILSAFE_NOOUTPUT )
      return RC_FAILSAFE_NOOUTPUT;
   if ( pModel->rc_params.failsafeFlags == RC_FAILSAFE_KEEPLAST )
      return RC_FAILSAFE_KEEPLAST;
   if ( pModel->rc_params.failsafeFlags == RC_FAILSAFE_BELOWRANGE )
      return RC_FAILSAFE_BELOWRANGE;
   if ( pModel->rc_params.failsafeFlags == RC_FAILSAFE_VALUE )
      return RC_FAILSAFE_VALUE;

   u32 fsType = (pModel->rc_params.rcChFlags[nChannel]>>1) & 0x07;
   return fsType;
}

int get_rc_channel_failsafe_value(Model* pModel, int nChannel, int prevRCValue)
{
   if ( NULL == pModel )
      return 0;
   if ( nChannel < 0 || nChannel >= MAX_RC_CHANNELS )
      return 0;

   if ( pModel->rc_params.failsafeFlags == RC_FAILSAFE_NOOUTPUT )
      return 0;
   if ( pModel->rc_params.failsafeFlags == RC_FAILSAFE_KEEPLAST )
      return prevRCValue;
   if ( pModel->rc_params.failsafeFlags == RC_FAILSAFE_BELOWRANGE )
      return pModel->rc_params.rcChMin[nChannel]-50;
   if ( pModel->rc_params.failsafeFlags == RC_FAILSAFE_VALUE )
      return (pModel->rc_params.failsafeFlags>>8) & 0xFFFF;

   int fsType = (pModel->rc_params.rcChFlags[nChannel]>>1) & 0x07;

   if ( fsType == RC_FAILSAFE_NOOUTPUT )
      return 0;
   if ( fsType == RC_FAILSAFE_KEEPLAST )
      return prevRCValue;
   if ( fsType == RC_FAILSAFE_BELOWRANGE )
      return pModel->rc_params.rcChMin[nChannel]-50;
   if ( fsType == RC_FAILSAFE_VALUE )
      return pModel->rc_params.rcChFailSafe[nChannel];
   return 0;
}


u32 utils_get_max_radio_datarate_for_profile(Model* pModel, int iProfile)
{
   if ( NULL == pModel )
      return DEFAULT_RADIO_DATARATE_DATA;

   if ( (iProfile < 0) || (iProfile >= MAX_VIDEO_LINK_PROFILES) )
      return DEFAULT_RADIO_DATARATE_DATA;

   int iMinRadioDataRate = 0; 
   u32 uMinRadioDataRateBPS = 0;
   bool bUsesHT40OnAnyLink = false;
   // First get the minimum radio datarate set on radio links that can transport video streams

   for( int i=0; i<pModel->radioLinksParams.links_count; i++ )
   {
      if ( pModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
         continue;
      if ( ! (pModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_HIGH_CAPACITY) )
         continue;
      if ( ! (pModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_VIDEO) )
      {
         log_line("Missing video flag from link %d", i+1);
         continue;
      }
      bool bUsesHT40 = false;
      if ( pModel->radioLinksParams.link_radio_flags[i] & RADIO_FLAG_HT40_VEHICLE )
      {
         bUsesHT40 = true;
         bUsesHT40OnAnyLink = true;
      }

      if ( getRealDataRateFromRadioDataRate(pModel->radioLinksParams.link_datarate_video_bps[i], bUsesHT40) < 5000000)
         continue;

      if ( 0 == uMinRadioDataRateBPS )
      {
         uMinRadioDataRateBPS = getRealDataRateFromRadioDataRate(pModel->radioLinksParams.link_datarate_video_bps[i], bUsesHT40);
         iMinRadioDataRate = pModel->radioLinksParams.link_datarate_video_bps[i];
      }
      else if ( getRealDataRateFromRadioDataRate(pModel->radioLinksParams.link_datarate_video_bps[i], bUsesHT40) < uMinRadioDataRateBPS )
      {
         uMinRadioDataRateBPS = getRealDataRateFromRadioDataRate(pModel->radioLinksParams.link_datarate_video_bps[i], bUsesHT40);
         iMinRadioDataRate = pModel->radioLinksParams.link_datarate_video_bps[i];
      }
   }

   // If the video profile has a set radio datarate, use it

   if ( 0 != pModel->video_link_profiles[iProfile].radio_datarate_video_bps )
   {
      uMinRadioDataRateBPS = getRealDataRateFromRadioDataRate(pModel->video_link_profiles[iProfile].radio_datarate_video_bps, bUsesHT40OnAnyLink);
      iMinRadioDataRate = pModel->video_link_profiles[iProfile].radio_datarate_video_bps;
   }

   //For MQ, LQ profiles, use a lower radio datarate if one was not set for them
   else if ( iProfile == VIDEO_PROFILE_MQ )
   {
      iMinRadioDataRate = utils_get_video_profile_mq_radio_datarate(pModel);
      uMinRadioDataRateBPS = getRealDataRateFromRadioDataRate(iMinRadioDataRate, bUsesHT40OnAnyLink);
   }
   else if ( iProfile == VIDEO_PROFILE_LQ )
   {
      iMinRadioDataRate = utils_get_video_profile_lq_radio_datarate(pModel);
      uMinRadioDataRateBPS = getRealDataRateFromRadioDataRate(iMinRadioDataRate, bUsesHT40OnAnyLink);
   } 

   // If the user did set a radio datarate for the video link, and is lower than current profile data rate, use it

   if ( 0 != pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps )
   if ( getRealDataRateFromRadioDataRate(pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps, bUsesHT40OnAnyLink) < uMinRadioDataRateBPS )
   {
      uMinRadioDataRateBPS = getRealDataRateFromRadioDataRate(pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps, bUsesHT40OnAnyLink);
      iMinRadioDataRate = pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps;
   }

   if ( 0 == uMinRadioDataRateBPS )
      uMinRadioDataRateBPS = DEFAULT_RADIO_DATARATE_DATA;
   
   return uMinRadioDataRateBPS;
}

u32 utils_get_max_allowed_video_bitrate_for_profile(Model* pModel, int iProfile)
{
   if ( NULL == pModel )
      return DEFAULT_VIDEO_BITRATE;

   if ( (iProfile < 0) || (iProfile >= MAX_VIDEO_LINK_PROFILES) )
      return DEFAULT_VIDEO_BITRATE;

   u32 uMaxRadioDataRateBPS = utils_get_max_radio_datarate_for_profile(pModel, iProfile);

   // Compute now max actual video bitrate that is set for profile and that does not go above the radio maxim

   u32 uMaxVideoBitrateBPSForRadioRate = (uMaxRadioDataRateBPS/100) * DEFAULT_VIDEO_LINK_LOAD_PERCENT;

   int iProfileDataPackets = 0;
   int iProfileECPackets = 0;
   pModel->get_video_profile_ec_scheme(iProfile, &iProfileDataPackets, &iProfileECPackets);
   
   uMaxVideoBitrateBPSForRadioRate = ( uMaxVideoBitrateBPSForRadioRate  / (u32)(iProfileDataPackets + iProfileECPackets) ) * (u32)iProfileDataPackets;

   return uMaxVideoBitrateBPSForRadioRate;
}


// Returns the video bitrate for a video profile
u32 utils_get_max_allowed_video_bitrate_for_profile_or_user_video_bitrate(Model* pModel, int iProfile)
{
   u32 uMaxVideoBitrateBPSForRadioRate = utils_get_max_allowed_video_bitrate_for_profile(pModel, iProfile);

   if ( 0 != pModel->video_link_profiles[iProfile].bitrate_fixed_bps )
   if ( pModel->video_link_profiles[iProfile].bitrate_fixed_bps < uMaxVideoBitrateBPSForRadioRate )
      uMaxVideoBitrateBPSForRadioRate = pModel->video_link_profiles[iProfile].bitrate_fixed_bps;
   return uMaxVideoBitrateBPSForRadioRate;
}

u32 utils_get_max_allowed_video_bitrate_for_profile_and_level(Model* pModel, int iProfile, int iLevel)
{
   if ( NULL == pModel )
      return DEFAULT_VIDEO_BITRATE;

   if ( iProfile < 0 || iProfile >= MAX_VIDEO_LINK_PROFILES )
      return DEFAULT_VIDEO_BITRATE;

   u32 uMaxVideoBitrateForProfile = utils_get_max_allowed_video_bitrate_for_profile_or_user_video_bitrate(pModel, iProfile);

   int iMaxLevels = pModel->get_video_profile_total_levels(iProfile);
   if ( iMaxLevels <= 1 )
      return uMaxVideoBitrateForProfile;
   if ( iLevel >= iMaxLevels )
      iLevel = iMaxLevels-1;

   int iProfileDataPackets = 0;
   int iProfileECPackets = 0;
   pModel->get_video_profile_ec_scheme(iProfile, &iProfileDataPackets, &iProfileECPackets);

   u32 uTotalBitrateUsedForProfile = ( uMaxVideoBitrateForProfile / (u32)iProfileDataPackets ) * (u32)(iProfileDataPackets + iProfileECPackets);
   
   u32 uBottomVideoBitrate = uTotalBitrateUsedForProfile / 2; // fec is equal to data packets on the lowest level
   
   if ( iProfile != VIDEO_PROFILE_USER )
   if ( iProfile != VIDEO_PROFILE_BEST_PERF )
   if ( iProfile != VIDEO_PROFILE_HIGH_QUALITY )
   if ( uBottomVideoBitrate < pModel->video_params.lowestAllowedAdaptiveVideoBitrate )
      uBottomVideoBitrate = pModel->video_params.lowestAllowedAdaptiveVideoBitrate;

   // Minimum for MQ profile, for 12 Mb datarate is at least 2Mb, do not go below that
   if ( iProfile == VIDEO_PROFILE_MQ )
   if ( pModel->video_link_profiles[VIDEO_PROFILE_MQ].radio_datarate_video_bps == 0 )
   if ( getRealDataRateFromRadioDataRate(pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps, 0) >= 12000000 )
   if ( uBottomVideoBitrate < 6000000 )
      uBottomVideoBitrate = 6000000;

   if ( uBottomVideoBitrate < 250000 )
      uBottomVideoBitrate = 250000;

   u32 uVideoBitrateChangePerLevel = (uMaxVideoBitrateForProfile - uBottomVideoBitrate) / (iMaxLevels-1);

   u32 uFinalVideoBitrate  = uMaxVideoBitrateForProfile - uVideoBitrateChangePerLevel * iLevel;
   
   return uFinalVideoBitrate;
}

int utils_get_video_profile_mq_radio_datarate(Model* pModel)
{
   if ( NULL == pModel )
      return 9000000;

   int iMaxRadioDataRate = 0; 
   u32 uMaxRadioDataRateBPS = 0;
   bool bUsesHT40OnAnyLink = false;

   // First get the maximum radio datarate set on radio links

   for( int i=0; i<pModel->radioLinksParams.links_count; i++ )
   {
      bool bUsesHT40 = false;
      if ( pModel->radioLinksParams.link_radio_flags[i] & RADIO_FLAG_HT40_VEHICLE )
      {
         bUsesHT40 = true;
         bUsesHT40OnAnyLink = true;
      }
      if ( ! (pModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_HIGH_CAPACITY) )
      if ( getRealDataRateFromRadioDataRate(pModel->radioLinksParams.link_datarate_video_bps[i], bUsesHT40) < 5000000)
         continue;
      if ( 0 == uMaxRadioDataRateBPS )
         uMaxRadioDataRateBPS = getRealDataRateFromRadioDataRate(pModel->radioLinksParams.link_datarate_video_bps[i], bUsesHT40);

      if ( 0 == iMaxRadioDataRate )
         iMaxRadioDataRate = pModel->radioLinksParams.link_datarate_video_bps[i];
   }

   // If the user's selected video profile has a set radio datarate, use it

   if ( 0 != pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps )
   {
      uMaxRadioDataRateBPS = getRealDataRateFromRadioDataRate(pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps, bUsesHT40OnAnyLink);
      iMaxRadioDataRate = pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps;
   }

   // If the MQ video profile has a fixed datarate instead of auto, return it.
   int iDataRate = pModel->video_link_profiles[VIDEO_PROFILE_MQ].radio_datarate_video_bps;
   if ( iDataRate != 0 )
   {
      if ( getRealDataRateFromRadioDataRate(iDataRate, bUsesHT40OnAnyLink) > getRealDataRateFromRadioDataRate(iMaxRadioDataRate, bUsesHT40OnAnyLink) )
         iDataRate = iMaxRadioDataRate;
      return iDataRate;
   }

   // For legacy radio data rates, return a lower legacy radio data rate
   if ( iMaxRadioDataRate > 0 )
   {
      if ( getRealDataRateFromRadioDataRate(iMaxRadioDataRate, bUsesHT40OnAnyLink) > 12000000 )
         return 12000000;
      if ( getRealDataRateFromRadioDataRate(iMaxRadioDataRate, bUsesHT40OnAnyLink) > 9000000 )
         return 9000000;
      return 6000000;
   }

   // For MCS data rates, return a lower MCS data rate
   //iDataRate = iMaxRadioDataRate;
   //if ( iDataRate < -1 )
   //   iDataRate++;

   // MCS-1
   iDataRate = iMaxRadioDataRate;
   if ( iDataRate <= -2 )
      iDataRate = -2;
   return iDataRate;
}

int utils_get_video_profile_lq_radio_datarate(Model* pModel)
{
   if ( NULL == pModel )
      return 6000000;

   int iMaxRadioDataRate = 0; 
   u32 uMaxRadioDataRateBPS = 0;
   bool bUsesHT40OnAnyLink = false;

   // First get the maximum radio datarate set on radio links

   for( int i=0; i<pModel->radioLinksParams.links_count; i++ )
   {
      bool bUsesHT40 = false;
      if ( pModel->radioLinksParams.link_radio_flags[i] & RADIO_FLAG_HT40_VEHICLE )
      {
         bUsesHT40 = true;
         bUsesHT40OnAnyLink = true;
      }
      if ( ! (pModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_HIGH_CAPACITY) )
      if ( getRealDataRateFromRadioDataRate(pModel->radioLinksParams.link_datarate_video_bps[i], bUsesHT40) < 5000000)
         continue;
      if ( 0 == uMaxRadioDataRateBPS )
         uMaxRadioDataRateBPS = getRealDataRateFromRadioDataRate(pModel->radioLinksParams.link_datarate_video_bps[i], bUsesHT40);

      if ( 0 == iMaxRadioDataRate )
         iMaxRadioDataRate = pModel->radioLinksParams.link_datarate_video_bps[i];
   }

   // If the user's selected video profile has a set radio datarate, use it

   if ( 0 != pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps )
   {
      uMaxRadioDataRateBPS = getRealDataRateFromRadioDataRate(pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps, bUsesHT40OnAnyLink);
      iMaxRadioDataRate = pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].radio_datarate_video_bps;
   }

   // If the LQ video profile has a fixed datarate instead of auto, return it.
   int iDataRate = pModel->video_link_profiles[VIDEO_PROFILE_LQ].radio_datarate_video_bps;
   if ( iDataRate != 0 )
   {
      if ( getRealDataRateFromRadioDataRate(iDataRate, bUsesHT40OnAnyLink) > getRealDataRateFromRadioDataRate(iMaxRadioDataRate, bUsesHT40OnAnyLink) )
         iDataRate = iMaxRadioDataRate;
      return iDataRate;
   }

   // For legacy radio data rates, return the lowest legacy radio data rate
   if ( iMaxRadioDataRate > 0 )
      return 6000000;

   // For MCS data rates, return the lowest MCS data rate (MCS-0 = -1)
   return -1;
}

void log_current_full_radio_configuration(Model* pModel)
{
   log_line("=====================================================================================");

   if ( NULL == pModel )
   {
      log_line("Current vehicle radio configuration:");
      log_error_and_alarm("INVALID MODEL parameter");
      log_line("=====================================================================================");
      return;
   }

   if ( pModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
      log_line("Current vehicle radio configuration: %d radio links, of which one is a relay link:", pModel->radioLinksParams.links_count);
   else
      log_line("Current vehicle radio configuration: %d radio links:", pModel->radioLinksParams.links_count);
   log_line("");
   for( int i=0; i<pModel->radioLinksParams.links_count; i++ )
   {
      char szPrefix[32];
      char szBuff[256];
      char szBuff2[256];
      char szBuff3[256];
      char szBuff4[256];
      char szBuff5[1200];
      szBuff[0] = 0;
      szPrefix[0] = 0;
      for( int k=0; k<pModel->radioInterfacesParams.interfaces_count; k++ )
      {
         if ( pModel->radioInterfacesParams.interface_link_id[k] == i )
         {
            char szInfo[32];
            if ( 0 != szBuff[0] )
               sprintf(szInfo, ", %d", k+1);
            else
               sprintf(szInfo, "%d", k+1);
            strcat(szBuff, szInfo);
         }
      }
      if ( pModel->relay_params.isRelayEnabledOnRadioLinkId == i )
      {
         strcpy(szPrefix, "Relay ");
         log_line("* %sRadio Link %d Info:  %s, radio interface(s) assigned to this link: [%s]", szPrefix, i+1, str_format_frequency(pModel->relay_params.uRelayFrequencyKhz), szBuff);
      }
      else
         log_line("* %sRadio Link %d Info:  %s, radio interface(s) assigned to this link: [%s]", szPrefix, i+1, str_format_frequency(pModel->radioLinksParams.link_frequency_khz[i]), szBuff);
      
      szBuff[0] = 0;

      str_get_radio_capabilities_description(pModel->radioLinksParams.link_capabilities_flags[i], szBuff);
      str_get_radio_frame_flags_description(pModel->radioLinksParams.link_radio_flags[i], szBuff2); 
      log_line("* %sRadio Link %d Capab: %s, Radio flags: %s", szPrefix, i+1, szBuff, szBuff2);
      str_getDataRateDescription(pModel->radioLinksParams.link_datarate_video_bps[i], 0, szBuff);
      str_getDataRateDescription(pModel->radioLinksParams.link_datarate_data_bps[i], 0, szBuff2);
      str_getDataRateDescription(pModel->radioLinksParams.uplink_datarate_video_bps[i], 0, szBuff3);
      str_getDataRateDescription(pModel->radioLinksParams.uplink_datarate_data_bps[i], 0, szBuff4);
      sprintf(szBuff5, "video: %s, data: %s, uplink video: %s, data: %s;", szBuff, szBuff2, szBuff3, szBuff4);
      log_line("* %sRadio Link %d Datarates: %s", szPrefix, i+1, szBuff5);
      log_line("");
   }

   log_line("=====================================================================================");
   log_line("Physical Radio Interfaces (%d configured, %d detected):", pModel->radioInterfacesParams.interfaces_count, hardware_get_radio_interfaces_count());
   log_line("");
   int count = pModel->radioInterfacesParams.interfaces_count;
   if ( count != hardware_get_radio_interfaces_count() )
   {
      log_softerror_and_alarm("Count of detected radio interfaces is not the same as the count of configured ones for this vehicle!");
      if ( count > hardware_get_radio_interfaces_count() )
         count = hardware_get_radio_interfaces_count();
   }
   for( int i=0; i<count; i++ )
   {
      char szPrefix[32];
      char szBuff[256];
      char szBuff2[256];
      szPrefix[0] = 0;
      radio_hw_info_t* pRadioInfo = hardware_get_radio_info(i);
      str_get_radio_capabilities_description(pModel->radioInterfacesParams.interface_capabilities_flags[i], szBuff);
      str_get_radio_frame_flags_description(pModel->radioInterfacesParams.interface_current_radio_flags[i], szBuff2); 
      if ( pModel->radioInterfacesParams.interface_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
         strcpy(szPrefix, "Relay ");
      log_line("* %sRadio int %d: %s [%s] %s, current frequency: %s, assigned to radio link %d", szPrefix, i+1, pRadioInfo->szUSBPort, str_get_radio_card_model_string(pModel->radioInterfacesParams.interface_card_model[i]), pRadioInfo->szDriver, str_format_frequency(pRadioInfo->uCurrentFrequencyKhz), pModel->radioInterfacesParams.interface_link_id[i]+1);
      log_line("* %sRadio int %d Capab: %s, Radio flags: %s", szPrefix, i+1, szBuff, szBuff2);
      log_line("");
   }
   log_line("=====================================================================================");
}


bool radio_utils_set_interface_frequency(Model* pModel, int iRadioIndex, int iAssignedModelRadioLink, u32 uFrequencyKhz, shared_mem_process_stats* pProcessStats, u32 uDelayMs)
{
   if ( uFrequencyKhz == 0 )
   {
      log_softerror_and_alarm("Skipping setting card (%d) due to invalid uFrequencyKhz 0.", iRadioIndex+1);
      return false;
   }

   u32 uFreqWifi = uFrequencyKhz/1000;

   char szInfo[64];
   u32 delayMs = DEFAULT_DELAY_WIFI_CHANGE;
   if ( hardware_is_station() && (uDelayMs > 0) )
   {
      delayMs = uDelayMs;
      if ( delayMs > 200 )
         delayMs = DEFAULT_DELAY_WIFI_CHANGE;
   }
   else if ( NULL != pModel )
      delayMs = (pModel->uDeveloperFlags >> 8) & 0xFF; 

   int iStartIndex = 0;
   int iEndIndex = hardware_get_radio_interfaces_count()-1;

   if ( -1 == iRadioIndex )
   {
      log_line("Setting all radio interfaces to frequency %s (guard interval: %d ms) for model radio link %d", str_format_frequency(uFrequencyKhz), (int)delayMs, iAssignedModelRadioLink+1);
      strcpy(szInfo, "all radio interfaces");
   }
   else
   {
      radio_hw_info_t* pRadioInfo2 = hardware_get_radio_info(iRadioIndex);
      log_line("Setting radio interface %d (%s, %s) to frequency %s (freq for wifi: %u) (guard interval: %d ms) for model radio link %d", iRadioIndex+1, pRadioInfo2->szName, str_get_radio_driver_description(pRadioInfo2->iRadioDriver), str_format_frequency(uFrequencyKhz), uFreqWifi, (int)delayMs, iAssignedModelRadioLink+1);
      sprintf(szInfo, "radio interface %d (%s, %s)", iRadioIndex+1, pRadioInfo2->szName, str_get_radio_driver_description(pRadioInfo2->iRadioDriver));
      iStartIndex = iRadioIndex;
      iEndIndex = iRadioIndex;
   }

   char cmd[128];
   char szOutput[512];
   bool failed = false;
   bool anySucceeded = false;

   for( int i=iStartIndex; i<=iEndIndex; i++ )
   {
      if ( NULL != pProcessStats )
         pProcessStats->lastActiveTime = get_current_timestamp_ms();
      
      radio_hw_info_t* pRadioInfo = hardware_get_radio_info(i);
      if ( (NULL == pRadioInfo) || (0 == hardware_radioindex_supports_frequency(i, uFrequencyKhz)) )
      {
         if ( NULL == pRadioInfo )
            log_line("Radio interface %d is NULL", i+1);
         else
         {
            log_line("Radio interface %d (%s, %s) does not support %s. Skipping it.", i+1, pRadioInfo->szName, str_get_radio_driver_description(pRadioInfo->iRadioDriver), str_format_frequency(uFrequencyKhz));
            pRadioInfo->lastFrequencySetFailed = 1;
            pRadioInfo->uFailedFrequencyKhz = uFrequencyKhz;
         }
         failed = true;
         continue;
      }

      if ( ! pRadioInfo->isConfigurable )
      {
         log_line("Radio interface %d (%s, %s) is not configurable. Skipping it.", i+1, pRadioInfo->szName, str_get_radio_driver_description(pRadioInfo->iRadioDriver));
         continue;
      }

      if ( hardware_radio_is_sik_radio(pRadioInfo) )
      {
         if ( ! hardware_radio_sik_set_frequency(pRadioInfo, uFrequencyKhz, pProcessStats) )
         {
            log_softerror_and_alarm("Failed to switch SiK radio interface %d to frequency %s", i+1, str_format_frequency(uFrequencyKhz));
            if ( NULL != pProcessStats )
               pProcessStats->lastActiveTime = get_current_timestamp_ms();
            continue;
         }
      }
      else if ( hardware_radio_is_wifi_radio(pRadioInfo) )
      {
         bool bTryHT40 = false;
         bool bUsedHT40 = false;
         szOutput[0] = 0;

         if ( (NULL != pModel) && (iAssignedModelRadioLink >= 0) && (iAssignedModelRadioLink < MAX_RADIO_INTERFACES) )
         {
            if ( hardware_is_station() )
            if ( pModel->radioLinksParams.link_radio_flags[iAssignedModelRadioLink] & RADIO_FLAG_HT40_CONTROLLER )
                  bTryHT40 = true;
            if ( hardware_is_vehicle() )
            if ( pModel->radioLinksParams.link_radio_flags[iAssignedModelRadioLink] & RADIO_FLAG_HT40_VEHICLE )
                  bTryHT40 = true;
         }

         if ( bTryHT40 )
         {
            #if defined(HW_PLATFORM_RASPBERRY)
            if ( pRadioInfo->iRadioType == RADIO_TYPE_ATHEROS )
            {
               sprintf(cmd, "iw dev %s set freq %u HT40+ 2>&1", pRadioInfo->szName, uFreqWifi);
               bUsedHT40 = true;
            }
            else
            {
               sprintf(cmd, "iw dev %s set freq %u HT40+ 2>&1", pRadioInfo->szName, uFreqWifi);
               bUsedHT40 = true;
            }
            #else
               sprintf(cmd, "iwconfig %s freq %u000 2>&1", pRadioInfo->szName, uFrequencyKhz);            
            #endif
         }
         else if ( pRadioInfo->isHighCapacityInterface )
         {
            #if defined(HW_PLATFORM_RASPBERRY)
            sprintf(cmd, "iw dev %s set freq %u 2>&1", pRadioInfo->szName, uFreqWifi);
            #else
            sprintf(cmd, "iwconfig %s freq %u000 2>&1", pRadioInfo->szName, uFrequencyKhz);            
            #endif
         }
         hw_execute_bash_command_raw(cmd, szOutput);

         if ( 5 < strlen(szOutput) )
            log_softerror_and_alarm("Received a response from set freq command: [%s]", szOutput);
           
         if ( NULL != strstr( szOutput, "Invalid argument" ) )
         if ( bUsedHT40 )
         if ( pRadioInfo->isHighCapacityInterface )
         {
            int len = strlen(szOutput);
            for( int k=0; k<len; k++ )
            {
               if ( szOutput[k] == 10 || szOutput[k] == 13 )
                  szOutput[k] = '.';
            }
            log_softerror_and_alarm("Failed to switch radio interface %d (%s, %s) to frequency %s in HT40 mode, returned error: [%s]. Retry operation.", i+1, pRadioInfo->szName, str_get_radio_driver_description(pRadioInfo->iRadioDriver), str_format_frequency(uFrequencyKhz), szOutput);
            hardware_sleep_ms(delayMs);
            szOutput[0] = 0;
            #if defined(HW_PLATFORM_RASPBERRY)
            sprintf(cmd, "iw dev %s set freq %u 2>&1", pRadioInfo->szName, uFreqWifi);
            #else
            sprintf(cmd, "iwconfig %s freq %u000 2>&1", pRadioInfo->szName, uFrequencyKhz);
            #endif
            hw_execute_bash_command_raw(cmd, szOutput);
         }

         if ( (NULL != strstr(szOutput, "busy")) || (NULL != strstr(szOutput, "such device")) )
         {
             hardware_initialize_radio_interface(i, delayMs);
             hardware_sleep_ms(delayMs);
             hw_execute_bash_command_raw(cmd, szOutput);
         }
         if ( NULL != strstr(szOutput, "failed") )
         {
            pRadioInfo->lastFrequencySetFailed = 1;
            pRadioInfo->uFailedFrequencyKhz = uFrequencyKhz;
            pRadioInfo->uCurrentFrequencyKhz = 0;
            failed = true;
            int len = strlen(szOutput);
            for( int k=0; k<len; k++ )
            {
               if ( szOutput[k] == 10 || szOutput[k] == 13 )
                  szOutput[k] = '.';
            }
            log_softerror_and_alarm("Failed to switch radio interface %d (%s, %s) to frequency %s, returned error: [%s]", i+1, pRadioInfo->szName, str_get_radio_driver_description(pRadioInfo->iRadioDriver), str_format_frequency(uFrequencyKhz), szOutput);
            hardware_sleep_ms(delayMs);
            continue;
         }
      }
      
      else
      {
         log_softerror_and_alarm("Detected unknown radio interface type.");
         continue;
      }
      log_line("Setting radio interface %d (%s, %s) to frequency %s succeeded.", i+1, pRadioInfo->szName, str_get_radio_driver_description(pRadioInfo->iRadioDriver), str_format_frequency(uFrequencyKhz));
      pRadioInfo->uCurrentFrequencyKhz = uFrequencyKhz;
      pRadioInfo->lastFrequencySetFailed = 0;
      pRadioInfo->uFailedFrequencyKhz = 0;
      anySucceeded = true;

      if ( NULL != pProcessStats )
         pProcessStats->lastActiveTime = get_current_timestamp_ms();
      if ( iRadioIndex != -1 )
         hardware_sleep_ms(delayMs/2+1);
      else
         hardware_sleep_ms(delayMs);
   }

   if ( -1 == iRadioIndex )
      log_line("Setting %s to frequency %s result: %s, at least one radio interface succeeded: ", szInfo, str_format_frequency(uFrequencyKhz), (failed?"failed":"succeeded"), (anySucceeded?"yes":"no"));

   return anySucceeded;
}

bool radio_utils_set_datarate_atheros(Model* pModel, int iCard, int dataRate_bps, u32 uDelayMs)
{
   u32 delayMs = DEFAULT_DELAY_WIFI_CHANGE;
   if ( hardware_is_station() && (uDelayMs > 0) )
   {
      delayMs = uDelayMs;
      if ( delayMs > 200 )
         delayMs = DEFAULT_DELAY_WIFI_CHANGE;
   }
   else if ( NULL != pModel )
      delayMs = (pModel->uDeveloperFlags >> 8) & 0xFF; 

   delayMs += 20;
   log_line("Setting global datarate for Atheros/RaLink radio interface %d to: %d bps (guard interval: %d ms)", iCard+1, dataRate_bps, (int)delayMs);
   
   radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(iCard);
   if ( NULL == pRadioHWInfo )
   {
      log_softerror_and_alarm("Can't get info for radio interface %d", iCard+1);
      return false;
   }

   if ( pRadioHWInfo->iCurrentDataRateBPS == dataRate_bps )
   {
      log_line("Atheros/RaLink radio interface %d already on datarate: %d bps. Done.", iCard+1, dataRate_bps);
      return true;
   }

   char cmd[1024];

   //sprintf(cmd, "ifconfig %s down", pRadioHWInfo->szName );
   sprintf(cmd, "ip link set dev %s down", pRadioHWInfo->szName);
   hw_execute_bash_command(cmd, NULL);
   hardware_sleep_ms(delayMs);

   sprintf(cmd, "iw dev %s set type managed", pRadioHWInfo->szName );
   hw_execute_bash_command(cmd, NULL);
   hardware_sleep_ms(delayMs);

   //sprintf(cmd, "ifconfig %s up", pRadioHWInfo->szName );
   sprintf(cmd, "ip link set dev %s up", pRadioHWInfo->szName);
   hw_execute_bash_command(cmd, NULL);
   hardware_sleep_ms(delayMs);

   if ( dataRate_bps > 0 )
      sprintf(cmd, "iw dev %s set bitrates legacy-2.4 %d", pRadioHWInfo->szName, dataRate_bps/1000/1000 );
   else
      sprintf(cmd, "iw dev %s set bitrates ht-mcs-2.4 %d", pRadioHWInfo->szName, -dataRate_bps-1 );
   hw_execute_bash_command(cmd, NULL);
   hardware_sleep_ms(delayMs);

   //sprintf(cmd, "ifconfig %s down", pRadioHWInfo->szName );
   sprintf(cmd, "ip link set dev %s down", pRadioHWInfo->szName);
   hw_execute_bash_command(cmd, NULL);
   hardware_sleep_ms(delayMs);

   sprintf(cmd, "iw dev %s set monitor none", pRadioHWInfo->szName );
   hw_execute_bash_command(cmd, NULL);
   hardware_sleep_ms(delayMs);

   sprintf(cmd, "iw dev %s set monitor fcsfail", pRadioHWInfo->szName );
   hw_execute_bash_command(cmd, NULL);
   hardware_sleep_ms(delayMs);

   //sprintf(cmd, "ifconfig %s up", pRadioHWInfo->szName );
   sprintf(cmd, "ip link set dev %s up", pRadioHWInfo->szName);
   hw_execute_bash_command(cmd, NULL);
   hardware_sleep_ms(delayMs);

   pRadioHWInfo->iCurrentDataRateBPS = dataRate_bps;
   hardware_save_radio_info();
   log_line("Setting datarate on Atheros/RaLink radio interface %d to: %d bps completed.", iCard+1, dataRate_bps);
   return true;
}


void log_camera_profiles_differences(camera_profile_parameters_t* pProfile1, camera_profile_parameters_t* pProfile2)
{
 
}

int check_write_filesystem()
{
   char szFile[MAX_FILE_PATH_SIZE];
   char szComm[256];

   sprintf(szComm, "rm -rf %stestwrite.txt 2>&1 1>/dev/null", FOLDER_CONFIG);
   hw_execute_bash_command(szComm, NULL);

   strcpy(szFile, FOLDER_CONFIG);
   strcat(szFile, "testwrite.txt");
   FILE* fd = fopen(szFile, "wb");
   if ( NULL == fd )
   {
      log_softerror_and_alarm("Check write file system failed: error -1");
      return -1;
   }
   fprintf(fd, "test1234\n");
   fclose(fd);

   fd = fopen(szFile, "rb");
   if ( NULL == fd )
   {
      log_softerror_and_alarm("Check write file system failed: error -2");
      return -2;
   }
   if ( 1 != fscanf(fd, "%s", szComm) )
   {
      log_softerror_and_alarm("Check write file system failed: error -3");
      fclose(fd);
      return -3;
   }

   if ( 0 != strcmp(szComm, "test1234") )
   {
      log_softerror_and_alarm("Check write file system failed: error -4");
      fclose(fd);
      return -4;
   }
   fclose(fd);

   sprintf(szComm, "rm -rf %stestwrite.txt 2>&1 1>/dev/null", FOLDER_CONFIG);
   hw_execute_bash_command(szComm, NULL);
   return 0;
}


double convertToRadians(double val)
{
   return val * PIx / 180.0;
}

long metersBetweenPlaces(double lat1, double lon1, double lat2, double lon2)
{
   double dlon = convertToRadians(lon2 - lon1);
   double dlat = convertToRadians(lat2 - lat1);

   double a = ( pow(sin(dlat / 2), 2) + cos(convertToRadians(lat1))) * cos(convertToRadians(lat2)) * pow(sin(dlon / 2), 2);
   double angle = 2 * asin(sqrt(a));
   //double angle = 2 * atan2(sqrt(a), sqrt(1.0-a));
   return (angle * RADIUS_EARTH * 1000.0);
}

long distance_meters_between(double lat1, double lon1, double lat2, double lon2)
{
    double delta = (lon1 - lon2) * 0.017453292519;
    double sdlong = sin(delta);
    double cdlong = cos(delta);

    lat1 = (lat1) * 0.017453292519;
    lat2 = (lat2) * 0.017453292519;

    double slat1 = sin(lat1);
    double clat1 = cos(lat1);
    double slat2 = sin(lat2);
    double clat2 = cos(lat2);

    delta = (clat1 * slat2) - (slat1 * clat2 * cdlong);
    delta = delta * delta;
    delta += (clat2 * sdlong) * (clat2 * sdlong);
    delta = sqrt(delta);

    float denom = (slat1 * slat2) + (clat1 * clat2 * cdlong);
    delta = atan2(delta, denom);

    return (delta * 6372795.0);
}
