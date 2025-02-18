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
#include "shared_vars_state.h"
#include "parse_msp.h"
t_structure_file_upload g_CurrentUploadingFile;
bool g_bHasFileUploadInProgress = false;

int s_StartSequence = 0;
Model* g_pCurrentModel = NULL;
u32 g_uActiveControllerModelVID = 0;

// First vehicle is always the main vehicle, the next ones are relayed vehicles
t_structure_vehicle_info g_SearchVehicleRuntimeInfo;
t_structure_vehicle_info g_UnexpectedVehicleRuntimeInfo;
t_structure_vehicle_info g_VehiclesRuntimeInfo[MAX_CONCURENT_VEHICLES];
int g_iCurrentActiveVehicleRuntimeInfoIndex = 0;

bool g_bSearching = false;
bool g_bSearchFoundVehicle = false;
int g_iSearchFrequency = 0;
int g_iSearchFirmwareType = MODEL_FIRMWARE_TYPE_RUBY;

int g_iSearchSiKAirDataRate = -1;
int g_iSearchSiKECC = -1;
int g_iSearchSiKLBT = -1;
int g_iSearchSiKMCSTR = -1;

bool g_bIsRouterReady = false;

bool g_bFirstModelPairingDone = false;
bool g_bIsFirstConnectionToCurrentVehicle = true;
bool g_bVideoLost = false;

bool g_bDidAnUpdate = false;
bool g_bUpdateInProgress = false;
bool g_bLinkWizardAfterUpdate = false;
int g_nFailedOTAUpdates = 0;
int g_nSucceededOTAUpdates = 0;

bool g_bSyncModelSettingsOnLinkRecover = false;
int g_nTotalControllerCPUSpikes = 0;

bool g_bGotStatsVideoBitrate = false;
bool g_bGotStatsVehicleTx = false;

bool g_bHasVideoDecodeStatsSnapshot = false;

u32 g_uLastControllerAlarmIOFlags = 0;

bool g_bChangedOSDStatsFontSize = false;

bool g_bReconfiguringRadioLinks = false;
bool g_bConfiguringRadioLink = false;
bool g_bSwitchingFavoriteVehicle = false;

void reset_vehicle_runtime_info(t_structure_vehicle_info* pInfo)
{
   if ( NULL == pInfo )
      return;

   int iIndex = -1;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( &(g_VehiclesRuntimeInfo[i]) == pInfo )
      {
         iIndex = i;
         break;
      }
   }
   if ( pInfo == &g_SearchVehicleRuntimeInfo )
      log_line("Reset vehicle runtime info for searching.");
   else if ( pInfo == &g_UnexpectedVehicleRuntimeInfo )
      log_line("Reset vehicle runtime info for unexpected vehicles.");
   else
      log_line("Reset vehicle runtime info index %d, for VID %u", iIndex, pInfo->uVehicleId);

   pInfo->uVehicleId = 0;
   pInfo->pModel = NULL;

   // Reset Ruby telemetry info

   pInfo->bGotRubyTelemetryInfo = false;
   pInfo->bGotRubyTelemetryInfoShort = false;
   pInfo->bGotRubyTelemetryExtraInfo = false;
   pInfo->bGotRubyTelemetryExtraInfoRetransmissions = false;
   pInfo->bGotStatsVehicleRxCards = false;
   
   pInfo->bRubyTelemetryLost = false;
   pInfo->iFrequencyRubyTelemetryFull = 0;
   pInfo->iFrequencyRubyTelemetryShort = 0;
   pInfo->uTimeLastRecvRubyTelemetry = 0;
   pInfo->uTimeLastRecvRubyTelemetryExtended = 0;
   pInfo->uTimeLastRecvRubyTelemetryShort = 0;
   pInfo->uTimeLastRecvAnyRubyTelemetry = 0;
   
   pInfo->uTimeLastRecvVehicleRxStats = 0;

   reset_counters(&(pInfo->vehicleDebugRouterCounters));
   reset_radio_tx_timers(&(pInfo->vehicleDebugRadioTxTimers));

   memset( &(pInfo->headerRubyTelemetryExtended), 0, sizeof(t_packet_header_ruby_telemetry_extended_v4));
   memset( &(pInfo->headerRubyTelemetryExtraInfo), 0, sizeof(t_packet_header_ruby_telemetry_extended_extra_info));
   memset( &(pInfo->headerRubyTelemetryExtraInfoRetransmissions), 0, sizeof(t_packet_header_ruby_telemetry_extended_extra_info_retransmissions));
   memset( &(pInfo->headerRubyTelemetryShort), 0, sizeof(t_packet_header_ruby_telemetry_short));
   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
      memset( &(pInfo->SMVehicleRxStats[i]), 0, sizeof(shared_mem_radio_stats_radio_interface));
   
   // Reset FC telemetry info
   reset_vehicle_telemetry_runtime_info(pInfo);
   
   // Reset other settings

   pInfo->bLinkLost = false;
   pInfo->bRCFailsafeState = false;
   pInfo->bWarningBatteryVoltage = false;
   pInfo->iComputedBatteryCellCount = 1;
   pInfo->bNotificationPairingRequestSent = false;
   pInfo->bPairedConfirmed = false;

   // These are reset by local_stats functions
   //pInfo->bIsArmed = false;
   //pInfo->bHomeSet = false;
   //pInfo->fHomeLat = 0.0;
   //pInfo->fHomeLon = 0.0;
   //pInfo->fHomeLastLat = 0.0;
   //pInfo->fHomeLastLon = 0.0;
   
   // Reset temporary info

   for( int i=0; i<20; i++ )
   {
      pInfo->uSegmentsModelSettingsSize[i] = 0;
      pInfo->uSegmentsModelSettingsIds[i] = 0;
   }
   pInfo->uSegmentsModelSettingsCount = 0;
   pInfo->bWaitingForModelSettings = true;
   pInfo->uTimeLastReceivedModelSettings = MAX_U32;
   pInfo->uTmpLastThrottledFlags = 0;

   pInfo->tmp_iCountRubyTelemetryPacketsFull = 0;
   pInfo->tmp_iCountRubyTelemetryPacketsShort = 0;
   pInfo->tmp_uTimeLastTelemetryFrequencyComputeTime = 0;
}

void reset_vehicle_telemetry_runtime_info(t_structure_vehicle_info* pInfo)
{
   if ( NULL == pInfo )
      return;

   pInfo->bGotRubyTelemetryInfo = false;
   pInfo->bGotRubyTelemetryInfoShort = false;

   pInfo->bGotFCTelemetry = false;
   pInfo->bGotFCTelemetryShort = false;
   pInfo->bGotFCTelemetryExtra = false;
   pInfo->bFCTelemetrySourcePresent = false;
   pInfo->iFrequencyFCTelemetryFull = 0;
   pInfo->iFrequencyFCTelemetryShort = 0;
   pInfo->uTimeLastRecvFCTelemetry = 0;
   pInfo->uTimeLastRecvFCTelemetryFull = 0;
   pInfo->uTimeLastRecvFCTelemetryShort = 0;
   
   pInfo->uLastFCFlightMode = 0;
   pInfo->uLastFCFlags = 0;

   memset( &(pInfo->headerFCTelemetry), 0, sizeof(t_packet_header_fc_telemetry));
   memset( &(pInfo->headerFCTelemetryExtra), 0, sizeof(t_packet_header_fc_extra));
   memset( &(pInfo->headerFCTelemetryRCChannels), 0, sizeof(t_packet_header_fc_rc_channels));

   pInfo->headerFCTelemetry.flags |= FC_TELE_FLAGS_NO_FC_TELEMETRY;

   pInfo->uTimeLastMessageFromFC = 0;
   pInfo->szLastMessageFromFC[0] = 0;
   
   parse_msp_reset_state(&pInfo->mspState);

   pInfo->tmp_iCountFCTelemetryPacketsFull = 0;
   pInfo->tmp_iCountFCTelemetryPacketsShort = 0;
}

void shared_vars_state_reset_all_vehicles_runtime_info()
{
   log_line("Reset all vehicles runtime info...");

   reset_vehicle_runtime_info(&g_SearchVehicleRuntimeInfo);
   reset_vehicle_runtime_info(&g_UnexpectedVehicleRuntimeInfo);
   
   g_iCurrentActiveVehicleRuntimeInfoIndex = 0;
   
   // First vehicle is always the main vehicle, the next ones are relayed vehicles

   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
      reset_vehicle_runtime_info(&(g_VehiclesRuntimeInfo[i]));

   log_line("Done reseting all vehicles runtime info.");
}

void reset_model_settings_download_buffers(u32 uVehicleId)
{
   if ( (0 == uVehicleId) || (MAX_U32 == uVehicleId) )
      return;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( g_VehiclesRuntimeInfo[i].uVehicleId == uVehicleId )
      {
         for( int k=0; k<20; k++ )
         {
            g_VehiclesRuntimeInfo[i].uSegmentsModelSettingsSize[k] = 0;
            g_VehiclesRuntimeInfo[i].uSegmentsModelSettingsIds[k] = 0;
         }
         g_VehiclesRuntimeInfo[i].uSegmentsModelSettingsCount = 0;
         g_VehiclesRuntimeInfo[i].bWaitingForModelSettings = true;
         g_VehiclesRuntimeInfo[i].uTimeLastReceivedModelSettings = MAX_U32;
         log_line("[Commands]: Did reset model settings download buffers and state for vehicle id %u.", uVehicleId);
      }
   }
}

t_structure_vehicle_info* get_vehicle_runtime_info_for_vehicle_id(u32 uVehicleId)
{
   if ( (0 == uVehicleId) || (MAX_U32 == uVehicleId) )
      return NULL;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( g_VehiclesRuntimeInfo[i].uVehicleId == uVehicleId )
         return &(g_VehiclesRuntimeInfo[i]);
   }
   return NULL;
}

t_packet_header_ruby_telemetry_extended_v4* get_received_relayed_vehicle_telemetry_info()
{
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( NULL != g_pCurrentModel )
      if ( g_VehiclesRuntimeInfo[i].uVehicleId == g_pCurrentModel->relay_params.uRelayedVehicleId )
         return &(g_VehiclesRuntimeInfo[i].headerRubyTelemetryExtended);
   }
   return NULL;
}


void log_current_runtime_vehicles_info()
{
   int iCount = 0;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( (g_VehiclesRuntimeInfo[i].uVehicleId != 0) && (g_VehiclesRuntimeInfo[i].uVehicleId != MAX_U32) )
         iCount++;
   }
   log_line("Current vehicles runtime info : count vehicles found in list: %d", iCount);

   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( (g_VehiclesRuntimeInfo[i].uVehicleId == 0) || (g_VehiclesRuntimeInfo[i].uVehicleId == MAX_U32) )
         continue;

      char szTmp[128];
      strcpy(szTmp, "NULL model");
      if ( NULL != g_VehiclesRuntimeInfo[i].pModel )
         strcpy(szTmp, g_VehiclesRuntimeInfo[i].pModel->is_spectator?"spectator mode":"control mode");

      log_line("Current vehicles runtime info: vehicle[%d]: %u %s, %s, Model: %p, model VID: %u, %s",
         i, g_VehiclesRuntimeInfo[i].uVehicleId, 
         (g_VehiclesRuntimeInfo[i].bGotRubyTelemetryInfo)?"RT":"no RT",
         (g_VehiclesRuntimeInfo[i].bGotFCTelemetry)?"FCT":"no FCT",
         g_VehiclesRuntimeInfo[i].pModel,
         (g_VehiclesRuntimeInfo[i].pModel != NULL)?(g_VehiclesRuntimeInfo[i].pModel->uVehicleId):0,
         szTmp);

      if ( NULL == g_VehiclesRuntimeInfo[i].pModel )
         log_softerror_and_alarm("Current vehicles runtime info: runtime info index %d has a valid VID: %d and a NULL model", i, g_VehiclesRuntimeInfo[i].uVehicleId);
   }
   log_line("Current vehicles runtime info: active vehicle runtime index: %d", g_iCurrentActiveVehicleRuntimeInfoIndex);
   if ( NULL != g_pCurrentModel )
      log_line("Current model (g_pCurrentModel) VID: %u, mode: %s", g_pCurrentModel->uVehicleId, g_pCurrentModel->is_spectator?"spectator mode":"control mode");
   else
      log_line("Current model (g_pCurrentModel): NULL");
}

bool vehicle_runtime_has_received_fc_telemetry(u32 uVehicleId)
{
   if ( (0 == uVehicleId) || (MAX_U32 == uVehicleId) )
      return false;

   t_structure_vehicle_info* pRuntimeInfo = get_vehicle_runtime_info_for_vehicle_id(uVehicleId);
   if ( (NULL == pRuntimeInfo) || (NULL == pRuntimeInfo->pModel) )
      return false;

   bool bNoTelemetryFromFC = false;

   if ( (pRuntimeInfo->pModel->telemetry_params.fc_telemetry_type == TELEMETRY_TYPE_MAVLINK) ||
       (pRuntimeInfo->pModel->telemetry_params.fc_telemetry_type == TELEMETRY_TYPE_LTM) )
   if ( pRuntimeInfo->bGotFCTelemetry )
   if ( pRuntimeInfo->headerFCTelemetry.flags & FC_TELE_FLAGS_NO_FC_TELEMETRY )
      bNoTelemetryFromFC = true;

   if ( pRuntimeInfo->bGotRubyTelemetryInfo )
   if ( ! (pRuntimeInfo->headerRubyTelemetryExtended.flags & FLAG_RUBY_TELEMETRY_HAS_VEHICLE_TELEMETRY_DATA) )
      bNoTelemetryFromFC = true;

   if ( pRuntimeInfo->bGotRubyTelemetryInfoShort )
   if ( ! (pRuntimeInfo->headerRubyTelemetryShort.uFlags & FLAG_RUBY_TELEMETRY_HAS_VEHICLE_TELEMETRY_DATA) )
      bNoTelemetryFromFC = true;

   return ! bNoTelemetryFromFC;
}