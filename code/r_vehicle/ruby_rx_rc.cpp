/*
    Ruby Licence
    Copyright (c) 2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
         * Copyright info and developer info must be preserved as is in the user
        interface, additions could be made to that info.
       * Neither the name of the organization nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.
        * Military use is not permited.

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

#include "../base/base.h"
#include "../base/hardware.h"
#ifdef HW_PLATFORM_RASPBERRY
#include "../base/hw_procs.h"
#endif
#include "../base/shared_mem.h"
#include "../radio/radiolink.h"
#include "../radio/radiopackets2.h"
#include "../base/config.h"
#include "../base/models.h"
#include "../base/models_list.h"
#include "../base/ruby_ipc.h"
#include "../common/string_utils.h"
#include "../utils/utils_vehicle.h"
#include "timers.h"
#include "shared_vars.h"

#include <time.h>
#include <sys/resource.h>
#include <semaphore.h>

Model sModelVehicle; 

int s_fIPC_FromRouter = -1;

u8 s_BufferRCFromRouter[MAX_PACKET_TOTAL_SIZE];
u8 s_PipeTmpBufferRCFromRouter[MAX_PACKET_TOTAL_SIZE];
int s_PipeTmpBufferRCFromRouterPos = 0;    

t_packet_header_rc_full_frame_upstream s_LastReceivedRCFrame;

t_packet_header_rc_info_downstream* s_pPHDownstreamInfoRC = NULL; // Info to send back to telemetry process and then (optionally) to ground

int s_LastHistorySlice = 0;
u8 s_LastReceivedRCFrameIndex = 0;
u8 s_QualityRecvCount[2];
u8 s_QualityRecvIndex = 0;


sem_t* s_pSemaphoreStop = NULL;

void process_data_rc_full_frame(u8* pBuffer, int length)
{
   if ( NULL == s_pPHDownstreamInfoRC )
      return;

   t_packet_header_rc_full_frame_upstream* pPHRCF = (t_packet_header_rc_full_frame_upstream*)(pBuffer + sizeof(t_packet_header));
   memcpy(&s_LastReceivedRCFrame, pPHRCF, sizeof(t_packet_header_rc_full_frame_upstream));

   g_TimeLastFrameReceived = g_TimeNow;
   s_pPHDownstreamInfoRC->recv_packets++;
   s_QualityRecvCount[s_QualityRecvIndex]++;

   for( int i=0; i<(int)sModelVehicle.rc_params.channelsCount; i++ )
   {
      if ( i % 2 )
         s_pPHDownstreamInfoRC->rc_channels[i] = (u16)(pPHRCF->ch_lowBits[i]) + (((u16)((pPHRCF->ch_highBits[i/2] & 0xF0)>>4))<<8);
      else
         s_pPHDownstreamInfoRC->rc_channels[i] = (u16)(pPHRCF->ch_lowBits[i]) + (((u16)(pPHRCF->ch_highBits[i/2] & 0x0F))<<8);

      //if ( i == 2 )
      //   log_line("ch: %d", s_pPHDownstreamInfoRC->rc_channels[2] );
   }

   u8 gap = pPHRCF->rc_frame_index - s_LastReceivedRCFrameIndex - 1;
   if ( pPHRCF->rc_frame_index == s_LastReceivedRCFrameIndex )
      gap = 0xFF;
   if ( pPHRCF->rc_frame_index < s_LastReceivedRCFrameIndex )
      gap = 255 - s_LastReceivedRCFrameIndex + pPHRCF->rc_frame_index;

   s_LastReceivedRCFrameIndex = pPHRCF->rc_frame_index;
   s_pPHDownstreamInfoRC->lost_packets += gap;

   //log_line("frame: %d, gap: %d, lost: %d", pPHRCF->rc_frame_index, gap, s_pPHDownstreamInfoRC->lost_packets);

   u8 cReceived = s_pPHDownstreamInfoRC->history[s_LastHistorySlice] & 0x0F;
   u8 cGap = ((s_pPHDownstreamInfoRC->history[s_LastHistorySlice]) >> 4) & 0x0F;
   if ( cReceived < 0x0F )
      cReceived++;
   if ( gap > 0x0F )
      gap = 0x0F;
   if ( gap > cGap )
      cGap = gap;
   s_pPHDownstreamInfoRC->history[s_LastHistorySlice] = (cReceived & 0x0F) | ((cGap & 0x0F) << 4);
}

void on_failsafe_triggered()
{
   log_line("Triggered a RC failsafe due to Rx timeout: %d ms", sModelVehicle.rc_params.rc_failsafe_timeout_ms);
   s_pPHDownstreamInfoRC->is_failsafe = 1;
   s_pPHDownstreamInfoRC->failsafe_count++;
}

void on_failsafe_cleared()
{
   log_line("Cleared RC failsafe.");
   s_pPHDownstreamInfoRC->is_failsafe = 0;
}

int r_start_rx_rc(int argc, char *argv[])
{
   log_init("RX_RC");
   log_arguments(argc, argv);

   if ( strcmp(argv[argc-1], "-debug") == 0 )
      g_bDebug = true;
   if ( g_bDebug )
      log_enable_stdout();

   
   s_fIPC_FromRouter = ruby_open_ipc_channel_read_endpoint(IPC_CHANNEL_TYPE_ROUTER_TO_RC);
   if ( s_fIPC_FromRouter < 0 )
      return -1;

   g_uControllerId = vehicle_utils_getControllerId();

   char szFile[128];
   strcpy(szFile, FOLDER_CONFIG);
   strcat(szFile, FILE_CONFIG_CURRENT_VEHICLE_MODEL);
   if ( ! sModelVehicle.loadFromFile(szFile, true) )
   {
      log_error_and_alarm("Can't load current model vehicle. Exiting.");
      return -1;
   } 
   
   s_pSemaphoreStop = sem_open(SEMAPHORE_STOP_RX_RC, O_CREAT, S_IWUSR | S_IRUSR, 0);
   if ( NULL == s_pSemaphoreStop )
      log_error_and_alarm("Failed to open semaphore: %s", SEMAPHORE_STOP_RX_RC);
   else
      log_line("Opened semaphore for signaling stop.");

   if ( sModelVehicle.uDeveloperFlags & DEVELOPER_FLAGS_BIT_LOG_ONLY_ERRORS )
      log_only_errors();

   #ifdef HW_PLATFORM_RASPBERRY
   hw_set_priority_current_proc(sModelVehicle.processesPriorities.iNiceRC);   
   #endif
   
   s_pPHDownstreamInfoRC = shared_mem_rc_downstream_info_open_write();
   if ( NULL == s_pPHDownstreamInfoRC )
      log_softerror_and_alarm("Failed to open RC Download info shared memory for write.");
   else
      log_line("Opened RC Download info shared memory for write: success.");
   if ( NULL != s_pPHDownstreamInfoRC )
      memset((u8*)s_pPHDownstreamInfoRC, 0, sizeof(t_packet_header_rc_info_downstream));

   g_pProcessStats = shared_mem_process_stats_open_write(SHARED_MEM_WATCHDOG_RC_RX);
   if ( NULL == g_pProcessStats )
      log_softerror_and_alarm("Failed to open shared mem for RC Rx process watchdog for writing: %s", SHARED_MEM_WATCHDOG_RC_RX);
   else
      log_line("Opened shared mem for RC Rx process watchdog for writing.");

   log_line("Started. Running now.");
   log_line("-----------------------------");

   s_QualityRecvCount[0] = 0;
   s_QualityRecvCount[1] = 0;
   s_QualityRecvIndex = 0;

   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastRadioTxTime = g_TimeNow;
      g_pProcessStats->lastRadioRxTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;
   }

   if ( NULL != s_pPHDownstreamInfoRC )
   {
      s_pPHDownstreamInfoRC->rc_rssi = 0;
      s_pPHDownstreamInfoRC->is_failsafe = 1;
   }

   s_LastReceivedRCFrame.rc_frame_index = 0;
   s_LastReceivedRCFrame.flags = 0;

   g_TimeStart = get_current_timestamp_ms();

   int iSleepIntervalMS = 50;

   while (!g_bQuit) 
   {
      hardware_sleep_ms(iSleepIntervalMS);
      if ( iSleepIntervalMS < 50 )
         iSleepIntervalMS += 10;

      int val = 0;
      if ( NULL != s_pSemaphoreStop )
      if ( 0 == sem_getvalue(s_pSemaphoreStop, &val) )
      if ( 0 < val )
      if ( EAGAIN != sem_trywait(s_pSemaphoreStop) )
      {
         log_line("Semaphore to stop is set.");
         g_bQuit = true;
         break;
      }
   
      g_TimeNow = get_current_timestamp_ms();
      u32 tTime0 = g_TimeNow;

      if ( NULL != g_pProcessStats )
      {
         g_pProcessStats->uLoopCounter++;
         g_pProcessStats->lastActiveTime = g_TimeNow;
      }

      if ( NULL == s_pPHDownstreamInfoRC )
      {
         s_pPHDownstreamInfoRC = shared_mem_rc_downstream_info_open_write();
         if ( NULL == s_pPHDownstreamInfoRC )
            log_softerror_and_alarm("Failed to open RC Download info shared memory for write.");
         else
            log_line("Opened RC Download info shared memory for write: success.");
         if ( NULL != s_pPHDownstreamInfoRC )
            memset((u8*)s_pPHDownstreamInfoRC, 0, sizeof(t_packet_header_rc_info_downstream)); 
      }

      if ( NULL != s_pPHDownstreamInfoRC )
      if ( g_TimeNow >= g_TimeLastQualityMeasurement + 500 )
      {
         int quality = 0;
         if ( NULL != s_pPHDownstreamInfoRC )
         if ( 0 != sModelVehicle.rc_params.rc_frames_per_second )
            quality = (100.0 * (s_QualityRecvCount[0] + s_QualityRecvCount[1]))/(float)sModelVehicle.rc_params.rc_frames_per_second;
         if ( quality < 0 ) quality = 0;
         if ( quality > 100 ) quality = 100;

         if ( quality >= s_pPHDownstreamInfoRC->rc_rssi )
            s_pPHDownstreamInfoRC->rc_rssi = quality;
         else
            s_pPHDownstreamInfoRC->rc_rssi = 0.9*quality + 0.1*s_pPHDownstreamInfoRC->rc_rssi;
         if ( s_pPHDownstreamInfoRC->rc_rssi > 100 )
            s_pPHDownstreamInfoRC->rc_rssi = 100;
         //log_line("RC Q: %d, %d %d", s_pPHDownstreamInfoRC->rc_rssi, g_TimeLastQualityMeasurement, g_TimeNow);
         g_TimeLastQualityMeasurement = g_TimeNow;
         s_QualityRecvIndex = 1 - s_QualityRecvIndex;
         s_QualityRecvCount[s_QualityRecvIndex] = 0;
      }

      if ( NULL != s_pPHDownstreamInfoRC )
      {
         int historySlice = (g_TimeNow/50) % RC_INFO_HISTORY_SIZE;
         while ( s_LastHistorySlice != historySlice )
         {
            s_LastHistorySlice++;
            if ( s_LastHistorySlice >= RC_INFO_HISTORY_SIZE )
               s_LastHistorySlice = 0;
            s_pPHDownstreamInfoRC->history[s_LastHistorySlice] = 0;
         }
         s_pPHDownstreamInfoRC->last_history_slice = s_LastHistorySlice;
      }

      if ( NULL != g_pProcessStats )
         g_pProcessStats->lastActiveTime = g_TimeNow;

      int maxMsgToRead = 5;
      while ( (maxMsgToRead > 0) && (NULL != ruby_ipc_try_read_message(s_fIPC_FromRouter, s_PipeTmpBufferRCFromRouter, &s_PipeTmpBufferRCFromRouterPos, s_BufferRCFromRouter)) )
      {
         iSleepIntervalMS = 2;
         maxMsgToRead--;
         t_packet_header* pPH = (t_packet_header*)&s_BufferRCFromRouter[0];
         if ( ! radio_packet_check_crc(s_BufferRCFromRouter, pPH->total_length) )
            continue;
 
         if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_RUBY )
         if ( pPH->packet_type == PACKET_TYPE_RUBY_PAIRING_REQUEST )
         {
            if ( pPH->total_length >= sizeof(t_packet_header) + 2*sizeof(u32) )
            {
               u32 uDeveloperMode = 0;
               memcpy(&uDeveloperMode, &(s_BufferRCFromRouter[sizeof(t_packet_header) + sizeof(u32)]), sizeof(u32));
               g_bDeveloperMode = (bool)uDeveloperMode;
            }
            g_uControllerId = pPH->vehicle_id_src;
            log_line("Received pairing request from router. CID: %u, VID: %u. Developer mode: %s. Updating local model.",
               pPH->vehicle_id_src, pPH->vehicle_id_dest, g_bDeveloperMode?"yes":"no");
            sModelVehicle.uControllerId = pPH->vehicle_id_src;
            if ( sModelVehicle.relay_params.isRelayEnabledOnRadioLinkId >= 0 )
            if ( sModelVehicle.relay_params.uRelayedVehicleId != 0 )
               sModelVehicle.relay_params.uCurrentRelayMode = RELAY_MODE_MAIN | RELAY_MODE_IS_RELAY_NODE;
         }

         if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_LOCAL_CONTROL )
         if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_MODEL_CHANGED )
         {
            u8 changeType = (pPH->vehicle_id_src >> 8 ) & 0xFF;

            if ( (changeType == MODEL_CHANGED_DEBUG_MODE) )
            {
               u8 uExtraParam = (pPH->vehicle_id_src >> 16 ) & 0xFF;
               log_line("Received notification that developer mode changed from %s to %s",
                 g_bDeveloperMode?"yes":"no",
                 uExtraParam?"yes":"no");
               g_bDeveloperMode = (bool)uExtraParam;
            }
            else if ( changeType == MODEL_CHANGED_GENERIC ||
                 changeType == MODEL_CHANGED_SWAPED_RADIO_INTERFACES )
            {
               log_line("Received request from router to reload model.");
               char szFile[128];
               strcpy(szFile, FOLDER_CONFIG);
               strcat(szFile, FILE_CONFIG_CURRENT_VEHICLE_MODEL);
               sModelVehicle.loadFromFile(szFile, true);
               log_line("RC Failsafe timeout: %d ms", sModelVehicle.rc_params.rc_failsafe_timeout_ms);
            }
            else
               log_line("Model change does not affect RX RC. Don't update local model.");
         }

         if ( pPH->vehicle_id_dest != sModelVehicle.uVehicleId )
            continue;

         if ( NULL != g_pProcessStats )
            g_pProcessStats->lastIPCIncomingTime = g_TimeNow;

         #ifdef FEATURE_ENABLE_RC
         if ( pPH->packet_type == PACKET_TYPE_RC_FULL_FRAME )
            process_data_rc_full_frame(s_BufferRCFromRouter, pPH->total_length);
         #endif
      }

      #ifdef FEATURE_ENABLE_RC
      bool bIsFailSafeNow = false;

      if ( sModelVehicle.rc_params.rc_enabled )
      if ( !(s_LastReceivedRCFrame.flags & RC_FULL_FRAME_FLAGS_HAS_INPUT) )
      {
         //log_line("RC No input");
         bIsFailSafeNow = true;
      }
      if ( NULL != s_pPHDownstreamInfoRC )
      if ( sModelVehicle.rc_params.rc_enabled && (0 != g_TimeLastFrameReceived) &&
           (g_TimeLastFrameReceived + sModelVehicle.rc_params.rc_failsafe_timeout_ms <= g_TimeNow ) )
      {
         //log_line("RC timeout failsafe %d ms", sModelVehicle.rc_params.rc_failsafe_timeout_ms);
         bIsFailSafeNow = true;
      }
      if ( bIsFailSafeNow )
      {
         if ( 0 == s_pPHDownstreamInfoRC->is_failsafe )
            on_failsafe_triggered();
         s_pPHDownstreamInfoRC->is_failsafe = 1;
      }

      if ( ! bIsFailSafeNow )
      if ( NULL != s_pPHDownstreamInfoRC )
      if ( sModelVehicle.rc_params.rc_enabled && (0 != g_TimeLastFrameReceived) &&
           (g_TimeLastFrameReceived + sModelVehicle.rc_params.rc_failsafe_timeout_ms > g_TimeNow) )
      {
         if ( 1 == s_pPHDownstreamInfoRC->is_failsafe )
            on_failsafe_cleared();
         s_pPHDownstreamInfoRC->is_failsafe = 0;
      }
      #endif

      u32 tNow = get_current_timestamp_ms();
      if ( NULL != g_pProcessStats )
      {
         if ( g_pProcessStats->uMaxLoopTimeMs < tNow - tTime0 )
            g_pProcessStats->uMaxLoopTimeMs = tNow - tTime0;
         g_pProcessStats->uTotalLoopTime += tNow - tTime0;
         if ( 0 != g_pProcessStats->uLoopCounter )
            g_pProcessStats->uAverageLoopTimeMs = g_pProcessStats->uTotalLoopTime / g_pProcessStats->uLoopCounter;
      }
   }
   log_line("Stopping...");
   
   shared_mem_rc_downstream_info_close(s_pPHDownstreamInfoRC);
   shared_mem_process_stats_close(SHARED_MEM_WATCHDOG_RC_RX, g_pProcessStats);

   ruby_close_ipc_channel(s_fIPC_FromRouter);
   s_fIPC_FromRouter = -1;
 
   if ( NULL != s_pSemaphoreStop )
      sem_close(s_pSemaphoreStop);

   log_line("Stopped.Exit");
   log_line("-----------------------");
   return 0;
} 