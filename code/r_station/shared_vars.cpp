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

#include "shared_vars.h"
#include "timers.h"

bool g_bQuit = false;
bool g_bDebugState = false;
Model* g_pCurrentModel = NULL;

ControllerSettings* g_pControllerSettings = NULL; 
ControllerInterfacesSettings* g_pControllerInterfaces = NULL; 
u32 g_uControllerId = MAX_U32;

bool g_bSearching = false;
u32  g_uSearchFrequency = 0;
u32  g_uAcceptedFirmwareType = MODEL_FIRMWARE_TYPE_RUBY;
bool g_bUpdateInProgress = false;
bool g_bNegociatingRadioLinks = false;

bool g_bDebugIsPacketsHistoryGraphOn = false;
bool g_bDebugIsPacketsHistoryGraphPaused = false;


// Router

ProcessorRxVideo* g_pVideoProcessorRxList[MAX_VIDEO_PROCESSORS];

controller_runtime_info g_SMControllerRTInfo;
controller_runtime_info* g_pSMControllerRTInfo = NULL;
vehicle_runtime_info g_SMVehicleRTInfo;
vehicle_runtime_info* g_pSMVehicleRTInfo = NULL;

shared_mem_radio_stats_rx_hist* g_pSM_HistoryRxStats = NULL;
shared_mem_radio_stats_rx_hist g_SM_HistoryRxStats;
shared_mem_audio_decode_stats* g_pSM_AudioDecodeStats = NULL;

shared_mem_video_frames_stats g_SM_VideoFramesStatsOutput;
shared_mem_video_frames_stats* g_pSM_VideoFramesStatsOutput = NULL;
//shared_mem_video_frames_stats g_SM_VideoInfoStatsRadioIn;
//shared_mem_video_frames_stats* g_pSM_VideoInfoStatsRadioIn = NULL;

shared_mem_router_vehicles_runtime_info g_SM_RouterVehiclesRuntimeInfo;
shared_mem_router_vehicles_runtime_info* g_pSM_RouterVehiclesRuntimeInfo = NULL;

shared_mem_video_stream_stats_rx_processors g_SM_VideoDecodeStats;
shared_mem_video_stream_stats_rx_processors* g_pSM_VideoDecodeStats = NULL;

shared_mem_radio_rx_queue_info* g_pSM_RadioRxQueueInfo = NULL;
shared_mem_radio_rx_queue_info g_SM_RadioRxQueueInfo;

shared_mem_radio_stats g_SM_RadioStats;
shared_mem_radio_stats* g_pSM_RadioStats = NULL;

shared_mem_radio_stats_interfaces_rx_graph g_SM_RadioStatsInterfacesRxGraph;
shared_mem_radio_stats_interfaces_rx_graph* g_pSM_RadioStatsInterfacesRxGraph = NULL;

// To fix
//shared_mem_video_link_stats_and_overwrites* g_pSM_VideoLinkStats = NULL;
shared_mem_video_link_graphs* g_pSM_VideoLinkGraphs = NULL;
shared_mem_router_packets_stats_history* g_pDebug_SM_RouterPacketsStatsHistory = NULL;
shared_mem_process_stats* g_pProcessStats = NULL;

t_packet_data_controller_link_stats g_PD_ControllerLinkStats;

int g_fIPCFromCentral = -1;
int g_fIPCToCentral = -1;
int g_fIPCToTelemetry = -1;
int g_fIPCFromTelemetry = -1;
int g_fIPCToRC = -1;
int g_fIPCFromRC = -1;

t_sik_radio_state g_SiKRadiosState;

bool g_bFirstModelPairingDone = false;

u32 g_uLastInterceptedCommandCounterToSetRadioFlags = MAX_U32;
u32 g_uLastRadioLinkIndexForSentSetRadioLinkFlagsCommand = MAX_U32;
int g_iLastRadioLinkDataRateSentForSetRadioLinkFlagsCommand = 0;

bool g_bIsVehicleLinkToControllerLost = false;
bool g_bIsControllerLinkToVehicleLost = false;

int g_iDebugShowKeyFramesAfterRelaySwitch = 0;
int g_iGetSiKConfigAsyncResult = 0;
int g_iGetSiKConfigAsyncRadioInterfaceIndex = -1;
u8 g_uGetSiKConfigAsyncVehicleLinkIndex = 0;
