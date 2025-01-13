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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/resource.h>
#include <semaphore.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../radio/fec.h" 

#include "../base/base.h"
#include "../base/config.h"
#include "../base/ctrl_settings.h"
#include "../base/shared_mem.h"
#include "../base/models.h"
#include "../base/models_list.h"
#include "../base/radio_utils.h"
#include "../base/hardware.h"
#include "../base/hw_procs.h"
#include "../common/string_utils.h"
#include "../common/relay_utils.h"
#include "../common/radio_stats.h"
#include "../radio/radiolink.h"
#include "../radio/radiopackets2.h"
#include "../radio/radiopacketsqueue.h"

#include "shared_vars.h"
#include "shared_vars_state.h"
#include "processor_rx_video.h"
#include "rx_video_output.h"
#include "packets_utils.h"
#include "links_utils.h"
#include "video_rx_buffers.h"
#include "timers.h"

typedef struct
{
   unsigned int fec_decode_missing_packets_indexes[MAX_TOTAL_PACKETS_IN_BLOCK];
   unsigned int fec_decode_fec_indexes[MAX_TOTAL_PACKETS_IN_BLOCK];
   u8* fec_decode_data_packets_pointers[MAX_TOTAL_PACKETS_IN_BLOCK];
   u8* fec_decode_fec_packets_pointers[MAX_TOTAL_PACKETS_IN_BLOCK];
   unsigned int missing_packets_count;
}
type_fec_info;


type_fec_info s_FECInfo;

extern t_packet_queue s_QueueRadioPacketsHighPrio;

int ProcessorRxVideo::m_siInstancesCount = 0;
FILE* ProcessorRxVideo::m_fdLogFile = NULL;

void ProcessorRxVideo::oneTimeInit()
{
   m_siInstancesCount = 0;
   m_fdLogFile = NULL;
   for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
      g_pVideoProcessorRxList[i] = NULL;
   log_line("[ProcessorRxVideo] Did one time initialization.");
}

ProcessorRxVideo* ProcessorRxVideo::getVideoProcessorForVehicleId(u32 uVehicleId, u32 uVideoStreamIndex)
{
   if ( (0 == uVehicleId) || (MAX_U32 == uVehicleId) )
      return NULL;

   for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
   {
      if ( NULL != g_pVideoProcessorRxList[i] )
      if ( g_pVideoProcessorRxList[i]->m_uVehicleId == uVehicleId )
      if ( g_pVideoProcessorRxList[i]->m_uVideoStreamIndex == uVideoStreamIndex )
         return g_pVideoProcessorRxList[i];
   }
   return NULL;
}

/*
void ProcessorRxVideo::log(const char* format, ...)
{
   va_list args;
   va_start(args, format);

   log_line(format, args);

   if ( m_fdLogFile <= 0 )
      return;

   char szTime[64];
   sprintf(szTime,"%d:%02d:%02d.%03d", (int)(g_TimeNow/1000/60/60), (int)(g_TimeNow/1000/60)%60, (int)((g_TimeNow/1000)%60), (int)(g_TimeNow%1000));
 
   fprintf(m_fdLogFile, "%s: ", szTime);
   vfprintf(m_fdLogFile, format, args);
   fprintf(m_fdLogFile, "\n");
   fflush(m_fdLogFile);
}
*/

ProcessorRxVideo::ProcessorRxVideo(u32 uVehicleId, u8 uVideoStreamIndex)
:m_bInitialized(false)
{
   m_iInstanceIndex = m_siInstancesCount;
   m_siInstancesCount++;

   log_line("[ProcessorRxVideo] Created new instance (number %d of %d) for VID: %u, stream %u", m_iInstanceIndex+1, m_siInstancesCount, uVehicleId, uVideoStreamIndex);
   m_uVehicleId = uVehicleId;
   m_uVideoStreamIndex = uVideoStreamIndex;
   m_uLastTimeRequestedRetransmission = 0;
   m_uRequestRetransmissionUniqueId = 0;
   m_uTimeLastReceivedNewVideoPacket = 0;
   m_TimeLastHistoryStatsUpdate = 0;
   m_TimeLastRetransmissionsStatsUpdate = 0;
   m_uLatestVideoPacketReceiveTime = 0;

   m_uLastVideoBlockIndexResolutionChange = 0;
   m_uLastVideoBlockPacketIndexResolutionChange = 0;

   m_bPaused = false;

   m_pVideoRxBuffer = new VideoRxPacketsBuffer(uVideoStreamIndex, 0);
   Model* pModel = findModelWithId(uVehicleId, 201);
   if ( NULL == pModel )
      log_softerror_and_alarm("[ProcessorRxVideo] Can't find model for VID %u", uVehicleId);
   else
      m_pVideoRxBuffer->init(pModel);

   // Add it to the video decode stats shared mem list

   m_iIndexVideoDecodeStats = -1;
   for(int i=0; i<MAX_VIDEO_PROCESSORS; i++)
   {
      if ( g_SM_VideoDecodeStats.video_streams[i].uVehicleId == 0 )
      {
         m_iIndexVideoDecodeStats = i;
         break;
      }
      if ( g_SM_VideoDecodeStats.video_streams[i].uVehicleId == uVehicleId )
      {
         m_iIndexVideoDecodeStats = i;
         break;
      }
   }
   if ( -1 != m_iIndexVideoDecodeStats )
   {
      g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].uVehicleId = uVehicleId;
      g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].uVideoStreamIndex = uVideoStreamIndex;
      g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoWidth = 0;
      g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoHeight = 0;
   }
}

ProcessorRxVideo::~ProcessorRxVideo()
{
   
   log_line("[ProcessorRxVideo] Video processor deleted for VID %u, video stream %u", m_uVehicleId, m_uVideoStreamIndex);

   m_siInstancesCount--;

   if ( 0 == m_siInstancesCount )
   {
      if ( m_fdLogFile != NULL )
         fclose(m_fdLogFile);
      m_fdLogFile = NULL;
   }

   // Remove this processor from video decode stats list
   if ( m_iIndexVideoDecodeStats != -1 )
   {
      memset(&(g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats]), 0, sizeof(shared_mem_video_stream_stats));
      for( int i=m_iIndexVideoDecodeStats; i<MAX_VIDEO_PROCESSORS-1; i++ )
         memcpy(&(g_SM_VideoDecodeStats.video_streams[i]), &(g_SM_VideoDecodeStats.video_streams[i+1]), sizeof(shared_mem_video_stream_stats));
      memset(&(g_SM_VideoDecodeStats.video_streams[MAX_VIDEO_PROCESSORS-1]), 0, sizeof(shared_mem_video_stream_stats));
   }
   m_iIndexVideoDecodeStats = -1;
}

bool ProcessorRxVideo::init()
{
   if ( m_bInitialized )
      return true;
   m_bInitialized = true;

   m_fdLogFile = NULL;

   log_line("[ProcessorRxVideo] Initialize video processor Rx instance number %d, for VID %u, video stream index %u", m_iInstanceIndex+1, m_uVehicleId, m_uVideoStreamIndex);

   // To fix video_link_adaptive_init(m_uVehicleId);
   // To fix video_link_keyframe_init(m_uVehicleId);

   m_uRetryRetransmissionAfterTimeoutMiliseconds = g_pControllerSettings->nRetryRetransmissionAfterTimeoutMS;
   log_line("[ProcessorRxVideo] Using timers: Retransmission retry after timeout of %d ms; Request retransmission after video silence (no video packets) timeout of %d ms", m_uRetryRetransmissionAfterTimeoutMiliseconds, g_pControllerSettings->nRequestRetransmissionsOnVideoSilenceMs);
      
   resetReceiveState();
   resetOutputState();
  
   log_line("[ProcessorRxVideo] Initialize video processor complete.");
   log_line("[ProcessorRxVideo] ====================================");
   return true;
}

bool ProcessorRxVideo::uninit()
{
   if ( ! m_bInitialized )
      return true;

   log_line("[ProcessorRxVideo] Uninitialize video processor Rx instance number %d for VID %u, video stream index %d", m_iInstanceIndex+1, m_uVehicleId, m_uVideoStreamIndex);
   
   m_bInitialized = false;
   return true;
}

void ProcessorRxVideo::resetReceiveState()
{
   log_line("[ProcessorRxVideo] Start: Reset video RX state and buffers");
   
   m_InfoLastReceivedVideoPacket.receive_time = 0;
   m_InfoLastReceivedVideoPacket.video_block_index = MAX_U32;
   m_InfoLastReceivedVideoPacket.video_block_packet_index = MAX_U32;
   m_InfoLastReceivedVideoPacket.stream_packet_idx = MAX_U32;

   m_uTimeLastReceivedNewVideoPacket = 0;
   
   m_uLastBlockReceivedAckKeyframeInterval = MAX_U32;
   m_uLastBlockReceivedAdaptiveVideoInterval = MAX_U32;
   m_uLastBlockReceivedSetVideoBitrate = MAX_U32;
   m_uLastBlockReceivedEncodingExtraFlags2 = MAX_U32;

   m_uRetryRetransmissionAfterTimeoutMiliseconds = g_pControllerSettings->nRetryRetransmissionAfterTimeoutMS;
   m_uTimeIntervalMsForRequestingRetransmissions = 15;

   log_line("[ProcessorRxVideo] Using timers: Retransmission retry after timeout of %d ms; Request retransmission after video silence (no video packets) timeout of %d ms", m_uRetryRetransmissionAfterTimeoutMiliseconds, g_pControllerSettings->nRequestRetransmissionsOnVideoSilenceMs);
   
   // Compute how many blocks to buffer

   // To fix ?
   /*

   log_line("[VideoRx] Need buffers to cache %d miliseconds of video for retransmissions", m_iMilisecondsMaxRetransmissionWindow);

   u32 videoBitrate = DEFAULT_VIDEO_BITRATE;
   Model* pModel = findModelWithId(m_uVehicleId, 121);
   if ( NULL != pModel )
      videoBitrate = pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].bitrate_fixed_bps;

   
   float miliPerBlock = (float)((u32)(1000 * 8 * (u32)m_SM_VideoDecodeStats.video_data_length * (u32)m_SM_VideoDecodeStats.data_packets_per_block)) / (float)videoBitrate;

   u32 bytesToBuffer = (((float)videoBitrate / 1000.0) * m_iMilisecondsMaxRetransmissionWindow)/8.0;
   log_line("[VideoRx] Need buffers to cache %u bits of video (%u bytes of video), one video block is %d bytes and lasts %.1f miliseconds",
      bytesToBuffer*8, bytesToBuffer, m_SM_VideoDecodeStats.data_packets_per_block * m_SM_VideoDecodeStats.video_data_length,
      miliPerBlock);
   
   if ( (0 != bytesToBuffer) && (0 != m_SM_VideoDecodeStats.video_data_length) && (0 != m_SM_VideoDecodeStats.data_packets_per_block) )
      m_iRXMaxBlocksToBuffer = 2 + bytesToBuffer/m_SM_VideoDecodeStats.video_data_length/m_SM_VideoDecodeStats.data_packets_per_block;
   else
      m_iRXMaxBlocksToBuffer = 5;

   // Add extra time for last retransmissions (50 milisec)
   if ( miliPerBlock > 0.0001 )
      m_iRXMaxBlocksToBuffer += 50.0/miliPerBlock;

   m_iRXMaxBlocksToBuffer *= 2.0;

   if ( m_iRXMaxBlocksToBuffer >= MAX_RXTX_BLOCKS_BUFFER )
   {
      m_iRXMaxBlocksToBuffer = MAX_RXTX_BLOCKS_BUFFER-1;
      log_line("[VideoRx] Capped max video Rx cache to %d blocks", m_iRXMaxBlocksToBuffer);
   }

   log_line("[VideoRx] Computed result: Will cache a maximum of %d video blocks, for a total of %d miliseconds of video (max retransmission window is %d ms); one block stores %.1f miliseconds of video",
       m_iRXMaxBlocksToBuffer, (int)(miliPerBlock * (float)m_iRXMaxBlocksToBuffer), m_iMilisecondsMaxRetransmissionWindow, miliPerBlock);
   
   resetReceiveBuffers(m_iRXMaxBlocksToBuffer);

   // Reset received packets statistics

   m_SM_VideoDecodeStats.maxBlocksAllowedInBuffers = m_iRXMaxBlocksToBuffer;
   m_SM_VideoDecodeStats.currentPacketsInBuffers = 0;
   m_SM_VideoDecodeStats.maxPacketsInBuffers = 10;
   m_SM_VideoDecodeStats.total_DiscardedSegments = 0;

   m_uLastHardEncodingsChangeVideoBlockIndex = MAX_U32;
   m_uLastVideoResolutionChangeVideoBlockIndex = MAX_U32;
   m_uEncodingsChangeCount = 0;
   m_uLastReceivedVideoLinkProfile = 0;
   */

   m_uTimeLastVideoStreamChanged = g_TimeNow;

   log_line("[ProcessorRxVideo] End: Reset video RX state and buffers");
   log_line("--------------------------------------------------------");
   log_line("");
}

void ProcessorRxVideo::resetOutputState()
{
   log_line("[ProcessorRxVideo] Reset output state.");
   m_uLastOutputVideoBlockTime = 0;
   m_uLastOutputVideoBlockIndex = MAX_U32;
   m_uLastOutputVideoBlockPacketIndex = MAX_U32;
   m_uLastOutputVideoBlockDataPackets = 5555;
}

void ProcessorRxVideo::resetReceiveBuffers(int iToMaxIndex)
{
}

void ProcessorRxVideo::resetReceiveBuffersBlock(int rx_buffer_block_index)
{
 /*
   for( int k=0; k<m_pRXBlocksStack[rx_buffer_block_index]->data_packets + m_pRXBlocksStack[rx_buffer_block_index]->fec_packets; k++ )
   {
      m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[k].uState = RX_PACKET_STATE_EMPTY;
      m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[k].uRetrySentCount = 0;
      m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[k].uTimeFirstRetrySent = 0;
      m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[k].uTimeLastRetrySent = 0;
      m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[k].video_data_length = 0;
      m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[k].packet_length = 0;
   }

   m_pRXBlocksStack[rx_buffer_block_index]->video_block_index = MAX_U32;
   m_pRXBlocksStack[rx_buffer_block_index]->video_data_length = 0;
   m_pRXBlocksStack[rx_buffer_block_index]->data_packets = 0;
   m_pRXBlocksStack[rx_buffer_block_index]->fec_packets = 0;
   m_pRXBlocksStack[rx_buffer_block_index]->received_data_packets = 0;
   m_pRXBlocksStack[rx_buffer_block_index]->received_fec_packets = 0;
   m_pRXBlocksStack[rx_buffer_block_index]->totalPacketsRequested = 0;
   m_pRXBlocksStack[rx_buffer_block_index]->uTimeFirstPacketReceived = MAX_U32;
   m_pRXBlocksStack[rx_buffer_block_index]->uTimeFirstRetrySent = 0;
   m_pRXBlocksStack[rx_buffer_block_index]->uTimeLastRetrySent = 0;
   m_pRXBlocksStack[rx_buffer_block_index]->uTimeLastUpdated = 0;
   */
}

void ProcessorRxVideo::resetStateOnVehicleRestart()
{
   log_line("[ProcessorRxVideo] VID %d, video stream %u: Reset state, full, due to vehicle restart.", m_uVehicleId, m_uVideoStreamIndex);
   resetReceiveState();
   resetOutputState();
   m_uLastVideoBlockIndexResolutionChange = 0;
   m_uLastVideoBlockPacketIndexResolutionChange = 0;
}

void ProcessorRxVideo::discardRetransmissionsInfo()
{
   checkAndDiscardBlocksTooOld();
   m_uLastTimeRequestedRetransmission = g_TimeNow;
}

void ProcessorRxVideo::onControllerSettingsChanged()
{
   log_line("[ProcessorRxVideo] VID %u, video stream %u: Controller params changed. Reinitializing RX video state...", m_uVehicleId, m_uVideoStreamIndex);

   m_uRetryRetransmissionAfterTimeoutMiliseconds = g_pControllerSettings->nRetryRetransmissionAfterTimeoutMS;
   log_line("[ProcessorRxVideo]: Using timers: Retransmission retry after timeout of %d ms; Request retransmission after video silence (no video packets) timeout of %d ms", m_uRetryRetransmissionAfterTimeoutMiliseconds, g_pControllerSettings->nRequestRetransmissionsOnVideoSilenceMs);
   
   resetReceiveState();
   resetOutputState();
}

void ProcessorRxVideo::pauseProcessing()
{
   m_bPaused = true;
   log_line("[ProcessorRxVideo] VID %u, video stream %u: paused processing.", m_uVehicleId, m_uVideoStreamIndex);
}

void ProcessorRxVideo::resumeProcessing()
{
   m_bPaused = false;
   log_line("[ProcessorRxVideo] VID %u, video stream %u: resumed processing.", m_uVehicleId, m_uVideoStreamIndex);
}

      

bool ProcessorRxVideo::checkAndDiscardBlocksTooOld()
{
   if ( !m_pVideoRxBuffer->hasIncompleteBlocks() )
      return false;

   // Discard blocks that are too old, past retransmission window
   int iCountBlocks = m_pVideoRxBuffer->getBlocksCountInBuffer();
   type_rx_video_block_info* pVideoBlockFirst = m_pVideoRxBuffer->getFirstVideoBlockInBuffer();
   type_rx_video_block_info* pVideoBlockLast = m_pVideoRxBuffer->getVideoBlockInBuffer(iCountBlocks-1);
   if ( (NULL != pVideoBlockFirst) && (NULL != pVideoBlockLast) )
   if ( (int)pVideoBlockLast->uReceivedTime - (int)pVideoBlockFirst->uReceivedTime > m_iMilisecondsMaxRetransmissionWindow )
   {
      m_pVideoRxBuffer->emptyBuffers("No new video past retransmission window");
      //resetReceiveBuffers(m_iRXBlocksStackTopIndex);
      resetOutputState();
      m_uTimeLastReceivedNewVideoPacket = 0;
      return true;
   }
   return false;
}

void ProcessorRxVideo::sendPacketToOutput(int rx_buffer_block_index, int block_packet_index)
{
 /*
   u32 video_block_index = m_pRXBlocksStack[rx_buffer_block_index]->video_block_index;

   if ( MAX_U32 == video_block_index || 0 == m_pRXBlocksStack[rx_buffer_block_index]->data_packets )
      return;

   if ( m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[block_packet_index].uState & RX_PACKET_STATE_OUTPUTED )
      return;

   if ( ! (m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[block_packet_index].uState & RX_PACKET_STATE_RECEIVED) )
   {
      return;
   }

   m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[block_packet_index].uState |= RX_PACKET_STATE_OUTPUTED;

   m_uLastOutputVideoBlockIndex = m_pRXBlocksStack[rx_buffer_block_index]->video_block_index;
   m_uLastOutputVideoBlockPacketIndex = block_packet_index;
   m_uLastOutputVideoBlockDataPackets = m_pRXBlocksStack[rx_buffer_block_index]->data_packets;
*/
   //u8* pBuffer = m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[block_packet_index].pData;
   //int lengthVideo = m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[block_packet_index].video_data_length;
   //int packet_length = m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[block_packet_index].packet_length;

   // To fix?
   //rx_video_output_video_data(m_uVehicleId, (m_SM_VideoDecodeStats.video_stream_and_type >> 4) & 0x0F , m_SM_VideoDecodeStats.width, m_SM_VideoDecodeStats.height, pBuffer, lengthVideo, packet_length);
}

void ProcessorRxVideo::pushIncompleteBlocksOut(int iStackIndexToDiscardTo, bool bTooOld)
{
 /*
   // Discard blocks, do not output them (unless blocks are good and not too old)

   if ( iStackIndexToDiscardTo <= 0 )
      return;

   #ifdef PROFILE_RX
   u32 uTimeStart = get_current_timestamp_ms();
   #endif

   m_uLastOutputVideoBlockTime = g_TimeNow;

   if ( m_pRXBlocksStack[iStackIndexToDiscardTo-1]->video_block_index != MAX_U32 )
      m_uLastOutputVideoBlockIndex = m_pRXBlocksStack[iStackIndexToDiscardTo-1]->video_block_index;
   else if ( m_uLastOutputVideoBlockIndex != MAX_U32 )
      m_uLastOutputVideoBlockIndex += iStackIndexToDiscardTo;

   if ( m_pRXBlocksStack[iStackIndexToDiscardTo-1]->data_packets > 0 )
      m_uLastOutputVideoBlockPacketIndex = m_pRXBlocksStack[iStackIndexToDiscardTo-1]->data_packets-1;

   bool bFullDiscard = false;
   if ( iStackIndexToDiscardTo >= m_iRXBlocksStackTopIndex + 1 )
   {
      iStackIndexToDiscardTo = m_iRXBlocksStackTopIndex+1;
      bFullDiscard = true;
      //if ( bTooOld )
      //   _rx_video_log_line("Discarding full Rx stack [0-%d] (too old, newest data in discarded segment was at %02d:%02d.%03d)", s_RXBlocksStackTopIndex, s_pRXBlocksStack[countToPush-1]->uTimeLastUpdated/1000/60, (s_pRXBlocksStack[countToPush-1]->uTimeLastUpdated/1000)%60, s_pRXBlocksStack[countToPush-1]->uTimeLastUpdated % 1000);
      //else
      //   _rx_video_log_line("Discarding full Rx stack [0-%d] (to make room for newer blocks, discarded blocks indexes [%u-%u], latest received block index: %u", s_RXBlocksStackTopIndex, s_pRXBlocksStack[0]->video_block_index, s_pRXBlocksStack[s_RXBlocksStackTopIndex]->video_block_index, s_LastReceivedVideoPacketInfo.video_block_index);
   }
   else
   {
      //if ( bReasonTooOld )
      //   _rx_video_log_line("Discarding Rx stack segment [0-%d] of total [0-%d] (too old, newest data in discarded segment was at %02d:%02d.%03d)", countToPush-1, s_RXBlocksStackTopIndex, s_pRXBlocksStack[countToPush-1]->uTimeLastUpdated/1000/60, (s_pRXBlocksStack[countToPush-1]->uTimeLastUpdated/1000)%60, s_pRXBlocksStack[countToPush-1]->uTimeLastUpdated % 1000);
      //else
      //   _rx_video_log_line("Discarding Rx stack segment [0-%d] (to make room for newer blocks, discarded blocks indexes [%u-%u], latest received block index: %u", countToPush-1, s_pRXBlocksStack[0]->video_block_index, s_pRXBlocksStack[countToPush-1]->video_block_index, s_LastReceivedVideoPacketInfo.video_block_index);
   }

   for( int i=0; i<iStackIndexToDiscardTo; i++ )
   {

      if ( m_pRXBlocksStack[i]->received_data_packets + m_pRXBlocksStack[i]->received_fec_packets >= m_pRXBlocksStack[i]->data_packets )
      if ( m_SM_VideoDecodeStatsHistory.outputHistoryMaxGoodBlocksPendingPerPeriod[0] > 0 )
         m_SM_VideoDecodeStatsHistory.outputHistoryMaxGoodBlocksPendingPerPeriod[0]--;

      if ( bTooOld )
      {
         resetReceiveBuffersBlock(i);
         continue;
      }

      // Do reconstruction if we have enough data for doing it;
      
      if ( m_pRXBlocksStack[i]->received_data_packets >= m_pRXBlocksStack[i]->data_packets )
      {
         int iIndex = getVehicleRuntimeIndex(m_uVehicleId);
         if ( -1 != iIndex )
            g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].uIntervalsOuputCleanVideoPackets[g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iCurrentIntervalIndex]++;  
      }

      if ( (m_pRXBlocksStack[i]->received_data_packets < m_pRXBlocksStack[i]->data_packets) &&
           (m_pRXBlocksStack[i]->received_data_packets + m_pRXBlocksStack[i]->received_fec_packets >= m_pRXBlocksStack[i]->data_packets) )
      {
         reconstructBlock(i);
         int iIndex = getVehicleRuntimeIndex(m_uVehicleId);
         if ( -1 != iIndex )
            g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].uIntervalsOuputRecontructedVideoPackets[g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iCurrentIntervalIndex]++;  
      }

      if ( m_pRXBlocksStack[i]->received_data_packets >= m_pRXBlocksStack[i]->data_packets )
      {
         for( int k=0; k<m_pRXBlocksStack[i]->data_packets; k++ )
            sendPacketToOutput(i, k);
      }

      resetReceiveBuffersBlock(i);
   }
         
   if ( bFullDiscard )
   {
      resetReceiveBuffersBlock(0);
      m_iRXBlocksStackTopIndex = -1;
   }
   else
   {
      // Rotate the outputed blocks (first countToPush blocks are moved to the end of the circular buffer, the others are sifhted to the begining of the buffer )
      type_received_block_info* tmpBlocksStack[MAX_RXTX_BLOCKS_BUFFER];
      memcpy((u8*)&(tmpBlocksStack[0]), (u8*)&(m_pRXBlocksStack[0]), (m_iRXBlocksStackTopIndex+1)*sizeof(type_received_block_info*));
      
      // Move blocks to the begining
      for( int i=iStackIndexToDiscardTo; i<=m_iRXBlocksStackTopIndex; i++ )
         m_pRXBlocksStack[i-iStackIndexToDiscardTo] = tmpBlocksStack[i];

      // Move the first N to the end of circular buffer
      for( int i=0; i<iStackIndexToDiscardTo; i++ )
         m_pRXBlocksStack[m_iRXBlocksStackTopIndex-iStackIndexToDiscardTo+i+1] = tmpBlocksStack[i];
      
      m_iRXBlocksStackTopIndex -= iStackIndexToDiscardTo;
   }
   
   #ifdef PROFILE_RX
   u32 dTime1 = get_current_timestamp_ms() - uTimeStart;
   if ( dTime1 >= PROFILE_RX_MAX_TIME )
      log_softerror_and_alarm("[Profile-Rx] Pushing incomplete video blocks out (%d blocks)took too long: %u ms.",  iStackIndexToDiscardTo, dTime1);
   #endif
   */
}

void ProcessorRxVideo::pushFirstBlockOut()
{
 /*
   #ifdef PROFILE_RX
   u32 uTimeStart = get_current_timestamp_ms();
   #endif
      
   // If we have all the data packets or
   // If no recontruction is possible, just output valid data

   int countRetransmittedPackets = 0;
   for( int i=0; i<m_pRXBlocksStack[0]->data_packets; i++ )
   {
      if ( m_pRXBlocksStack[0]->packetsInfo[i].uState & RX_PACKET_STATE_RECEIVED )
      if ( m_pRXBlocksStack[0]->packetsInfo[i].uRetrySentCount > 0 )
         countRetransmittedPackets++;
   }
   // Do reconstruction (we have enough data for doing it)

   if ( m_pRXBlocksStack[0]->received_data_packets < m_pRXBlocksStack[0]->data_packets )
   {
      
      if ( m_pRXBlocksStack[0]->received_data_packets + m_pRXBlocksStack[0]->received_fec_packets >= m_pRXBlocksStack[0]->data_packets )
      {
         int iIndex = getVehicleRuntimeIndex(m_uVehicleId);
         if ( -1 != iIndex )
            g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].uIntervalsOuputRecontructedVideoPackets[g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iCurrentIntervalIndex]++;  

         reconstructBlock(0);
         //_log_current_buffer();
      }
      //else
      //   _rx_video_log_line("Can't reconstruct block %u, has only %d packets of minimum %d required.", s_pRXBlocksStack[0]->video_block_index, s_pRXBlocksStack[0]->received_data_packets + s_pRXBlocksStack[0]->received_fec_packets, s_pRXBlocksStack[0]->data_packets);
   }
   else
   {
      if ( g_PD_ControllerLinkStats.tmp_video_streams_blocks_clean[0] < 254 )
         g_PD_ControllerLinkStats.tmp_video_streams_blocks_clean[0]++;
      else
         g_PD_ControllerLinkStats.tmp_video_streams_blocks_clean[0] = 254;

      int iIndex = getVehicleRuntimeIndex(m_uVehicleId);
      if ( -1 != iIndex )
         g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].uIntervalsOuputCleanVideoPackets[g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iCurrentIntervalIndex]++;
   }

   #ifdef PROFILE_RX
   u32 dTime1 = get_current_timestamp_ms() - uTimeStart;
   if ( dTime1 >= PROFILE_RX_MAX_TIME )
      log_softerror_and_alarm("[Profile-Rx] Pushing first video block out (history update and reconstruction) took too long: %u ms.", dTime1);
   #endif

   // Output the block

   if ( m_pRXBlocksStack[0]->received_data_packets + m_pRXBlocksStack[0]->received_fec_packets >= m_pRXBlocksStack[0]->data_packets )
   if ( m_SM_VideoDecodeStatsHistory.outputHistoryMaxGoodBlocksPendingPerPeriod[0] > 0 )
      m_SM_VideoDecodeStatsHistory.outputHistoryMaxGoodBlocksPendingPerPeriod[0]--;

   for( int i=0; i<m_pRXBlocksStack[0]->data_packets; i++ )
      sendPacketToOutput(0, i);

   m_uLastOutputVideoBlockTime = g_TimeNow;
   m_uLastOutputVideoBlockIndex = m_pRXBlocksStack[0]->video_block_index;
   m_uLastOutputVideoBlockPacketIndex = m_pRXBlocksStack[0]->data_packets-1;
   m_uLastOutputVideoBlockDataPackets = m_pRXBlocksStack[0]->data_packets;
   
   resetReceiveBuffersBlock(0);

   // Shift the rx blocks buffers by one block
   if ( m_iRXBlocksStackTopIndex > 0 )
   {
      type_received_block_info* pFirstBlock = m_pRXBlocksStack[0];
      for( int i=0; i<m_iRXBlocksStackTopIndex; i++ )
         m_pRXBlocksStack[i] = m_pRXBlocksStack[i+1];
      m_pRXBlocksStack[m_iRXBlocksStackTopIndex] = pFirstBlock;
   }
   if ( m_iRXBlocksStackTopIndex >= 0 )
      m_iRXBlocksStackTopIndex--;

   #ifdef PROFILE_RX
   u32 dTime2 = get_current_timestamp_ms() - uTimeStart;
   if ( dTime2 >= PROFILE_RX_MAX_TIME )
      log_softerror_and_alarm("[Profile-Rx] Pushing first video block out (to player) took too long: %u ms.", dTime2);
   #endif
   */
}

u32 ProcessorRxVideo::getLastTimeVideoStreamChanged()
{
   return m_uTimeLastVideoStreamChanged;
}

u32 ProcessorRxVideo::getLastestVideoPacketReceiveTime()
{
   return m_uLatestVideoPacketReceiveTime;
}

int ProcessorRxVideo::getCurrentlyReceivedVideoFPS()
{
   return -1;
   // To fix?

   //if ( 0 == m_SM_VideoDecodeStats.width )
   //   return -1;
   //return m_SM_VideoDecodeStats.fps;
}

int ProcessorRxVideo::getCurrentlyReceivedVideoKeyframe()
{
   return -1;
   // To fix?
   //if ( 0 == m_SM_VideoDecodeStats.width )
   //   return -1;
   //return m_SM_VideoDecodeStats.keyframe_ms;
}

int ProcessorRxVideo::getCurrentlyMissingVideoPackets()
{
   int iCount = 0;
   //for( int i=0; i<m_iRXBlocksStackTopIndex; i++ )
   //{
   //   iCount += m_pRXBlocksStack[i]->data_packets + m_pRXBlocksStack[i]->fec_packets - (m_pRXBlocksStack[i]->received_data_packets + m_pRXBlocksStack[i]->received_fec_packets);
   //}
   return iCount;
}

int ProcessorRxVideo::getVideoWidth()
{
   int iVideoWidth = 0;
   if ( m_iIndexVideoDecodeStats != -1 )
   {
      if ( (0 != g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoWidth) && (0 != g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoHeight) )
         iVideoWidth = g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoWidth;
   }
   else
   {
      Model* pModel = findModelWithId(m_uVehicleId, 177);
      if ( NULL != pModel )
         iVideoWidth = pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].width;
   }
   return iVideoWidth;
}

int ProcessorRxVideo::getVideoHeight()
{
   int iVideoHeight = 0;
   if ( m_iIndexVideoDecodeStats != -1 )
   {
      if ( (0 != g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoWidth) && (0 != g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoHeight) )
         iVideoHeight = g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoHeight;
   }
   else
   {
      Model* pModel = findModelWithId(m_uVehicleId, 177);
      if ( NULL != pModel )
         iVideoHeight = pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].height;
   }
   return iVideoHeight;
}

int ProcessorRxVideo::getVideoFPS()
{
   int iFPS = 0;
   if ( -1 != m_iIndexVideoDecodeStats )
      iFPS = g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoFPS;
   if ( 0 == iFPS )
   {
      Model* pModel = findModelWithId(m_uVehicleId, 177);
      if ( NULL != pModel )
         iFPS = pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].fps;
   }
   return iFPS;
}

int ProcessorRxVideo::getVideoType()
{
   int iVideoType = 0;
   if ( (-1 != m_iIndexVideoDecodeStats ) &&
        (0 != ((g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].PHVF.uVideoStreamIndexAndType >> 4) & 0x0F) ) )
      iVideoType = (g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].PHVF.uVideoStreamIndexAndType >> 4) & 0x0F;
   else
   {
      Model* pModel = findModelWithId(m_uVehicleId, 177);
      if ( NULL != pModel )
      {
         iVideoType = VIDEO_TYPE_H264;
         if ( pModel->video_params.uVideoExtraFlags & VIDEO_FLAG_GENERATE_H265 )
            iVideoType = VIDEO_TYPE_H265;
      }
   }
   return iVideoType;
}

int ProcessorRxVideo::periodicLoop(u32 uTimeNow, bool bForceSyncNow)
{
   type_global_state_vehicle_runtime_info* pRuntimeInfo = getVehicleRuntimeInfo(m_uVehicleId);
   Model* pModel = findModelWithId(m_uVehicleId, 155);
   if ( (NULL == pModel) || (NULL == pRuntimeInfo) )
      return -1;
     
   return checkAndRequestMissingPackets(bForceSyncNow);

/*
   if ( 0 != m_uTimeLastReceivedNewVideoPacket )
   if ( m_iMilisecondsMaxRetransmissionWindow > 20 )
   if ( g_TimeNow >= m_uTimeLastReceivedNewVideoPacket + m_iMilisecondsMaxRetransmissionWindow - 20 )
   {
      //if ( m_pRXBlocksStack[m_iRXBlocksStackTopIndex]->uTimeLastUpdated < g_TimeNow - m_iMilisecondsMaxRetransmissionWindow*1.5 )
      log_line("[VideoRx] Discard old blocks due to no new video packet for %d ms (%d blocks in the stack).", m_iMilisecondsMaxRetransmissionWindow, m_iRXBlocksStackTopIndex);
      resetReceiveBuffers(m_iRXBlocksStackTopIndex);
      resetOutputState();
      m_uTimeLastReceivedNewVideoPacket = 0;
      return;
   }
*/
   // TO fix?
   //if ( (m_SM_VideoDecodeStats.uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_ENABLE_RETRANSMISSIONS) )
   //if ( relay_controller_must_display_video_from(g_pCurrentModel, m_uVehicleId) )
   //   bRetr = true;

   // To fix reenable?
   //if ( bRetr )
   //if ( m_InfoLastReceivedVideoPacket.video_block_index != MAX_U32 )
   //   checkAndRequestMissingPackets();

   // Can we output the first few blocks?
   /*
   int maxBlocksToOutputIfAvailable = MAX_BLOCKS_TO_OUTPUT_IF_AVAILABLE;
   do
   {
      if ( m_iRXBlocksStackTopIndex < 0 )
         break;
      if ( m_pRXBlocksStack[0]->data_packets == 0 )
         break;
      if ( m_pRXBlocksStack[0]->received_data_packets + m_pRXBlocksStack[0]->received_fec_packets < m_pRXBlocksStack[0]->data_packets )
         break;

      pushFirstBlockOut();
      maxBlocksToOutputIfAvailable--;
   }
   while ( maxBlocksToOutputIfAvailable > 0 );

   checkAndDiscardBlocksTooOld();

   
   if ( uTimeNow > m_TimeLastRetransmissionsStatsUpdate + m_SM_RetransmissionsStats.uGraphRefreshIntervalMs )
   {
      m_TimeLastRetransmissionsStatsUpdate = uTimeNow;
   }
   */
}

// Returns 1 if a video block has just finished and the flag "Can TX" is set

int ProcessorRxVideo::handleReceivedVideoPacket(int interfaceNb, u8* pBuffer, int length)
{
   if ( m_bPaused )
      return 1;

   t_packet_header* pPH = (t_packet_header*)pBuffer;
   t_packet_header_video_full_98* pPHVF = (t_packet_header_video_full_98*) (pBuffer+sizeof(t_packet_header));    

   #ifdef PROFILE_RX
   //u32 uTimeStart = get_current_timestamp_ms();
   //int iStackIndexStart = m_iRXBlocksStackTopIndex;
   #endif  

   #if defined(RUBY_BUILD_HW_PLATFORM_PI)
   if (((pPHVF->uVideoStreamIndexAndType >> 4) & 0x0F) == VIDEO_TYPE_H265 )
   {
      static u32 s_uTimeLastSendVideoUnsuportedAlarmToCentral = 0;
      if ( g_TimeNow > s_uTimeLastSendVideoUnsuportedAlarmToCentral + 20000 )
      {
         s_uTimeLastSendVideoUnsuportedAlarmToCentral = g_TimeNow;
         send_alarm_to_central(ALARM_ID_UNSUPPORTED_VIDEO_TYPE, pPHVF->uVideoStreamIndexAndType, pPH->vehicle_id_src);
      }
   }
   #endif

   if ( pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED )
   {
      // Discard retransmitted packets that:
      // * Are from before latest video stream resolution change;
      // * Are from before a vehicle restart detected;
      // Retransmitted packets are sent on controller request or automatically (ie on a missing ACK)

      bool bDiscard = false;
      if ( 0 == m_uLastVideoBlockIndexResolutionChange )
         bDiscard = true;
      if ( pPHVF->uCurrentBlockIndex < m_uLastVideoBlockIndexResolutionChange )
         bDiscard = true;
      if ( pPHVF->uCurrentBlockIndex == m_uLastVideoBlockIndexResolutionChange )
      if ( pPHVF->uCurrentBlockPacketIndex < m_uLastVideoBlockPacketIndexResolutionChange )
         bDiscard = true;
   
      if ( bDiscard )
         return 0;
   }


   if ( NULL != m_pVideoRxBuffer )
   {
      bool bNewestOnStream = m_pVideoRxBuffer->checkAddVideoPacket(pBuffer, length);
      if ( bNewestOnStream && (m_iIndexVideoDecodeStats != -1) )
      {
         updateVideoDecodingStats(pBuffer, length);
         m_uLatestVideoPacketReceiveTime = g_TimeNow;
      }

      if ( pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED )
      {
         controller_runtime_info_vehicle* pRTInfo = controller_rt_info_get_vehicle_info(&g_SMControllerRTInfo, pPH->vehicle_id_src);
         if ( NULL != pRTInfo )
         {
            pRTInfo->uCountAckRetransmissions[g_SMControllerRTInfo.iCurrentIndex]++;
            if ( pPHVF->uStreamInfoFlags == VIDEO_STREAM_INFO_FLAG_RETRANSMISSION_ID )
            if ( pPHVF->uStreamInfo == m_uRequestRetransmissionUniqueId )
            {
               u32 uDeltaTime = g_TimeNow - m_uLastTimeRequestedRetransmission;
               controller_rt_info_update_ack_rt_time(&g_SMControllerRTInfo, pPH->vehicle_id_src, g_SM_RadioStats.radio_interfaces[interfaceNb].assignedLocalRadioLinkId, uDeltaTime);
            }
         }
      }

      if ( bNewestOnStream && (!(pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED)) )
         m_uTimeLastReceivedNewVideoPacket = g_TimeNow;
  
      // Output available video packets

      while ( m_pVideoRxBuffer->hasFirstVideoPacketInBuffer() )
      {
         type_rx_video_packet_info* pVideoPacket = m_pVideoRxBuffer->getFirstVideoPacketInBuffer();
         type_rx_video_block_info* pVideoBlock = m_pVideoRxBuffer->getFirstVideoBlockInBuffer();
         if ( (NULL != pVideoBlock) && (NULL != pVideoPacket) && (NULL != pVideoPacket->pRawData) )
         if ( ! pVideoPacket->bEmpty )
         if ( pVideoPacket->pPHVF->uCurrentBlockPacketIndex < pVideoPacket->pPHVF->uCurrentBlockDataPackets )
         if ( NULL != pVideoPacket->pVideoData )
         {
            u8* pVideoSource = pVideoPacket->pVideoData;
            if ( pVideoPacket->pPHVF->uVideoStatusFlags2 & VIDEO_STATUS_FLAGS2_HAS_DEBUG_TIMESTAMPS )
            {
               //t_packet_header_video_full_98_debug_info* pPHVFDebugInfo = (t_packet_header_video_full_98_debug_info*)pVideoSource;
               //log_line("DEBUG output skip debug info for [%u/%u], CRC %u", pVideoPacket->pPHVF->uCurrentBlockIndex, pVideoPacket->pPHVF->uCurrentBlockPacketIndex, pPHVFDebugInfo->uVideoCRC);
               pVideoSource += sizeof(t_packet_header_video_full_98_debug_info);
            }

             u16 uVideoSize = 0;
             memcpy(&uVideoSize, pVideoSource, sizeof(u16));
             //u32 crc = base_compute_crc32(pVideoSource, pVideoPacket->pPHVF->uCurrentBlockPacketSize);
             //log_line("DEBUG output [%u/%u] %d bytes, block size %d, packet length: %d, CRC %u", 
             //   pVideoPacket->pPHVF->uCurrentBlockIndex, pVideoPacket->pPHVF->uCurrentBlockPacketIndex,
             //    uVideoSize, pVideoPacket->pPHVF->uCurrentBlockPacketSize, pVideoPacket->pPH->total_length, crc);
             pVideoSource += sizeof(u16);

             int iVideoWidth = getVideoWidth();
             int iVideoHeight = getVideoHeight();

             rx_video_output_video_data(m_uVehicleId, (pVideoPacket->pPHVF->uVideoStreamIndexAndType >> 4) & 0x0F , iVideoWidth, iVideoHeight, pVideoSource, uVideoSize, pVideoPacket->pPH->total_length);

             g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex]++;
             if ( pVideoPacket->pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED )
                g_SMControllerRTInfo.uOutputedVideoPacketsRetransmitted[g_SMControllerRTInfo.iCurrentIndex]++;
             if ( pVideoBlock->iReconstructedECUsed > 0 )
             {
                if ( pVideoBlock->iReconstructedECUsed > g_SMControllerRTInfo.uOutputedVideoPacketsMaxECUsed[g_SMControllerRTInfo.iCurrentIndex] )
                   g_SMControllerRTInfo.uOutputedVideoPacketsMaxECUsed[g_SMControllerRTInfo.iCurrentIndex] = pVideoBlock->iReconstructedECUsed;
                
                if ( pVideoBlock->iReconstructedECUsed == 1 )
                   g_SMControllerRTInfo.uOutputedVideoPacketsSingleECUsed[g_SMControllerRTInfo.iCurrentIndex]++;
                else if ( pVideoBlock->iReconstructedECUsed == 2 )
                   g_SMControllerRTInfo.uOutputedVideoPacketsTwoECUsed[g_SMControllerRTInfo.iCurrentIndex]++;
                else
                   g_SMControllerRTInfo.uOutputedVideoPacketsMultipleECUsed[g_SMControllerRTInfo.iCurrentIndex]++;
                pVideoBlock->iReconstructedECUsed = 0;
             }
         }
         m_pVideoRxBuffer->advanceStartPosition();
      }

      // If one way link, or retransmissions are off, or spectator mode, or vehicle has lost link to controller,
      // skip blocks, if there are more video blocks with gaps in buffer
      type_rx_video_block_info* pVideoBlock = m_pVideoRxBuffer->getFirstVideoBlockInBuffer();
      if ( NULL != pVideoBlock )
      if ( m_pVideoRxBuffer->getMaxReceivedVideoBlockIndex() > pVideoBlock->uVideoBlockIndex+1 )
      {
         Model* pModel = findModelWithId(m_uVehicleId, 170);
         bool bSkipIncompleteBlocks = false;
         if ( (NULL == pModel) || pModel->isVideoLinkFixedOneWay() )
            bSkipIncompleteBlocks = true;

         if ( g_bSearching || g_bUpdateInProgress || pModel->is_spectator )
            bSkipIncompleteBlocks = true;

         if ( NULL != pModel )
         if ( ! (pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_ENABLE_RETRANSMISSIONS) )
            bSkipIncompleteBlocks = true;

         if ( bSkipIncompleteBlocks )
            m_pVideoRxBuffer->advanceStartPositionToVideoBlock(m_pVideoRxBuffer->getMaxReceivedVideoBlockIndex());
         else
         {
            //checkAndRequestMissingPackets(false);
            checkAndDiscardBlocksTooOld();
         }
      }
   }

// To fix
   /*
   t_packet_header* pPH = (t_packet_header*) pBuffer;
   t_packet_header_video_full_77* pPHVF = (t_packet_header_video_full_77*) (pBuffer+sizeof(t_packet_header));    
   u32 video_block_index = pPHVF->video_block_index;
   u8 video_block_packet_index = pPHVF->video_block_packet_index;

   
   checkAndDiscardBlocksTooOld();
   
   Model* pModel = findModelWithId(pPH->vehicle_id_src, 122);
   if ( NULL == pModel )
   {
      static u32 s_uLastAlarmVideoUnkownTime = 0;
      static u32 s_uTimeLastAlarmVideoUnkownVehicleId = 0;
      if ( (s_uLastAlarmVideoUnkownTime == 0 ) || (pPH->vehicle_id_src != s_uTimeLastAlarmVideoUnkownVehicleId) )
      if ( (s_uLastAlarmVideoUnkownTime == 0 ) || (g_TimeNow > s_uLastAlarmVideoUnkownTime+10000) )
      {
         s_uLastAlarmVideoUnkownTime = g_TimeNow;
         s_uTimeLastAlarmVideoUnkownVehicleId = pPH->vehicle_id_src;
         send_alarm_to_central(ALARM_ID_GENERIC, ALARM_ID_GENERIC_TYPE_UNKNOWN_VIDEO, pPH->vehicle_id_src);
      }
      return 0;
   }   

   #ifdef PROFILE_RX
   u32 dTime1 = get_current_timestamp_ms() - uTimeStart;
   if ( dTime1 >= PROFILE_RX_MAX_TIME )
      log_softerror_and_alarm("[Profile-Rx] Video processing video packet [%u/%u], interface: %d, len: %d bytes: Discarding old blocks took too long: %u ms. Stack top before/after: %d/%d", video_block_index , video_block_packet_index, interfaceNb, length, dTime1, iStackIndexStart, m_iRXBlocksStackTopIndex);
   #endif

   int packetsGap = preProcessReceivedVideoPacket(interfaceNb, pBuffer, length);

   #ifdef PROFILE_RX
   u32 dTime2 = get_current_timestamp_ms() - uTimeStart;
   if ( dTime2 >= PROFILE_RX_MAX_TIME )
      log_softerror_and_alarm("[Profile-Rx] Video processing video packet [%u/%u], interface: %d, len: %d bytes: Preprocessing packet took too long: %u ms. Stack top before/after: %d/%d", video_block_index , video_block_packet_index, interfaceNb, length, dTime2, iStackIndexStart, m_iRXBlocksStackTopIndex);
   #endif

   if ( packetsGap < 0 )
   {
      return 0;
   }

   int added_to_rx_buffer_index = -1;

   if ( pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED )
      added_to_rx_buffer_index = processRetransmittedVideoPacket(pBuffer, length);
   else
      added_to_rx_buffer_index = processReceivedVideoPacket(pBuffer, length);

   #ifdef PROFILE_RX
   u32 dTime3 = get_current_timestamp_ms() - uTimeStart;
   if ( dTime3 >= PROFILE_RX_MAX_TIME )
      log_softerror_and_alarm("[Profile-Rx] Video processing video packet [%u/%u], interface: %d, len: %d bytes: Adding packet to buffers took too long: %u ms. Stack top before/after: %d/%d", video_block_index , video_block_packet_index, interfaceNb, length, dTime3, iStackIndexStart, m_iRXBlocksStackTopIndex);
   #endif

   if ( added_to_rx_buffer_index < 0 )
   {
      return 0;
   }
   int nReturnCanTx = onNewReceivedValidVideoPacket(pModel, pBuffer, length, added_to_rx_buffer_index);

   return nReturnCanTx;
   */
   return 1;
}


void ProcessorRxVideo::updateVideoDecodingStats(u8* pRadioPacket, int iPacketLength)
{
   if ( (m_iIndexVideoDecodeStats < 0) || (m_iIndexVideoDecodeStats >= MAX_VIDEO_PROCESSORS) )
      return;
   t_packet_header_video_full_98* pPHVF = (t_packet_header_video_full_98*) (pRadioPacket+sizeof(t_packet_header));    

   if ( g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].PHVF.uCurrentVideoLinkProfile != pPHVF->uCurrentVideoLinkProfile )
   {
      // Video profile changed on the received stream
      if ( pPHVF->uCurrentVideoLinkProfile == VIDEO_PROFILE_MQ ||
           pPHVF->uCurrentVideoLinkProfile == VIDEO_PROFILE_LQ ||
           g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].PHVF.uCurrentVideoLinkProfile == VIDEO_PROFILE_MQ ||
           g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].PHVF.uCurrentVideoLinkProfile == VIDEO_PROFILE_LQ )
      {
         if ( pPHVF->uCurrentVideoLinkProfile > g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].PHVF.uCurrentVideoLinkProfile )
            g_SMControllerRTInfo.uFlagsAdaptiveVideo[g_SMControllerRTInfo.iCurrentIndex] |= CTRL_RT_INFO_FLAG_VIDEO_PROF_SWITCHED_LOWER;
         else
            g_SMControllerRTInfo.uFlagsAdaptiveVideo[g_SMControllerRTInfo.iCurrentIndex] |= CTRL_RT_INFO_FLAG_VIDEO_PROF_SWITCHED_HIGHER;
      }
      else
         g_SMControllerRTInfo.uFlagsAdaptiveVideo[g_SMControllerRTInfo.iCurrentIndex] |= CTRL_RT_INFO_FLAG_VIDEO_PROF_SWITCHED_USER_SELECTABLE;
   }

   memcpy( &(g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].PHVF), pPHVF, sizeof(t_packet_header_video_full_98));

   if ( pPHVF->uStreamInfoFlags == VIDEO_STREAM_INFO_FLAG_SIZE )
   if ( 0 != pPHVF->uCurrentBlockIndex )
   {
      if ( (g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoWidth != (pPHVF->uStreamInfo & 0xFFFF)) ||
           (g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoHeight != ((pPHVF->uStreamInfo >> 16) & 0xFFFF)) )
      {
          m_uLastVideoBlockIndexResolutionChange = pPHVF->uCurrentBlockIndex;
          m_uLastVideoBlockPacketIndexResolutionChange = pPHVF->uCurrentBlockPacketIndex;
      }
      g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoWidth = pPHVF->uStreamInfo & 0xFFFF;
      g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoHeight = (pPHVF->uStreamInfo >> 16) & 0xFFFF;
   }
   if ( pPHVF->uStreamInfoFlags == VIDEO_STREAM_INFO_FLAG_FPS )
   {
      g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].iCurrentVideoFPS = pPHVF->uStreamInfo;
   }
   if ( pPHVF->uStreamInfoFlags == VIDEO_STREAM_INFO_FLAG_FEC_TIME )
   {
      g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].uCurrentFECTimeMicros = pPHVF->uStreamInfo;
   }
   if ( pPHVF->uStreamInfoFlags == VIDEO_STREAM_INFO_FLAG_VIDEO_PROFILE_FLAGS )
   {
      g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].uCurrentVideoProfileEncodingFlags = pPHVF->uStreamInfo;
   }
}


void ProcessorRxVideo::reconstructBlock(int rx_buffer_block_index)
{
 /*

   if ( g_PD_ControllerLinkStats.tmp_video_streams_blocks_reconstructed[0] < 254 )
      g_PD_ControllerLinkStats.tmp_video_streams_blocks_reconstructed[0]++;
   else
      g_PD_ControllerLinkStats.tmp_video_streams_blocks_reconstructed[0] = 254;

   // Add existing data packets, mark and count the ones that are missing

   s_FECInfo.missing_packets_count = 0;
   for( int i=0; i<m_pRXBlocksStack[rx_buffer_block_index]->data_packets; i++ )
   {
      s_FECInfo.fec_decode_data_packets_pointers[i] = m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[i].pData;
      if ( ! (m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[i].uState & RX_PACKET_STATE_RECEIVED) )
      {
         s_FECInfo.fec_decode_missing_packets_indexes[s_FECInfo.missing_packets_count] = i;
         s_FECInfo.missing_packets_count++;
         //s_VDStatsCache.total_BadOrLostPackets++;
      }
   }

   if ( s_FECInfo.missing_packets_count > g_PD_ControllerLinkStats.tmp_video_streams_blocks_max_ec_packets_used[0] )
   {
      // missing packets in a block can't be larger than 8 bits (config values for max EC/Data pachets)
      g_PD_ControllerLinkStats.tmp_video_streams_blocks_max_ec_packets_used[0] = s_FECInfo.missing_packets_count;
   }

   // Add the needed FEC packets to the list
   unsigned int pos = 0;
   for( int i=0; i<m_pRXBlocksStack[rx_buffer_block_index]->fec_packets; i++ )
   {
      if ( m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[i+m_pRXBlocksStack[rx_buffer_block_index]->data_packets].uState & RX_PACKET_STATE_RECEIVED)
      {
         s_FECInfo.fec_decode_fec_packets_pointers[pos] = m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[i+m_pRXBlocksStack[rx_buffer_block_index]->data_packets].pData;
         s_FECInfo.fec_decode_fec_indexes[pos] = i;
         pos++;
         if ( pos == s_FECInfo.missing_packets_count )
            break;
      }
   }

   fec_decode(m_pRXBlocksStack[rx_buffer_block_index]->video_data_length, s_FECInfo.fec_decode_data_packets_pointers, m_pRXBlocksStack[rx_buffer_block_index]->data_packets, s_FECInfo.fec_decode_fec_packets_pointers, s_FECInfo.fec_decode_fec_indexes, s_FECInfo.fec_decode_missing_packets_indexes, s_FECInfo.missing_packets_count );
         
   // Mark all data packets reconstructed as received, set the right data in them
   for( u32 i=0; i<s_FECInfo.missing_packets_count; i++ )
   {
      m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[s_FECInfo.fec_decode_missing_packets_indexes[i]].uState |= RX_PACKET_STATE_RECEIVED;
      m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[s_FECInfo.fec_decode_missing_packets_indexes[i]].video_data_length = m_pRXBlocksStack[rx_buffer_block_index]->video_data_length;
      m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[s_FECInfo.fec_decode_missing_packets_indexes[i]].packet_length = m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[s_FECInfo.fec_decode_missing_packets_indexes[i]].video_data_length;
      m_pRXBlocksStack[rx_buffer_block_index]->received_data_packets++;
   }
  //_rx_video_log_line("Reconstructed block %u, had %d missing packets", s_pRXBlocksStack[rx_buffer_block_index]->video_block_index, s_FECInfo.missing_packets_count);
*/
}


int ProcessorRxVideo::checkAndRequestMissingPackets(bool bForceSyncNow)
{
   type_global_state_vehicle_runtime_info* pRuntimeInfo = getVehicleRuntimeInfo(m_uVehicleId);
   Model* pModel = findModelWithId(m_uVehicleId, 179);
   if ( (NULL == pModel) || (NULL == pRuntimeInfo) )
      return -1;

   int iVideoProfileNow = g_SM_VideoDecodeStats.video_streams[m_iIndexVideoDecodeStats].PHVF.uCurrentVideoLinkProfile;
   m_iMilisecondsMaxRetransmissionWindow = ((pModel->video_link_profiles[iVideoProfileNow].uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_MAX_RETRANSMISSION_WINDOW_MASK) >> 8) * 5;

   checkAndDiscardBlocksTooOld();

   if ( g_bSearching || g_bUpdateInProgress || m_bPaused || pModel->is_spectator || (! pRuntimeInfo->bIsPairingDone) )
      return -1;
   if ( ! m_pVideoRxBuffer->hasIncompleteBlocks() )
      return -1;
   
   if ( pModel->isVideoLinkFixedOneWay() || (!(pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_ENABLE_RETRANSMISSIONS)) )
      return -1;

   // Do not request from models 9.7 or older
   if ( get_sw_version_build(pModel) < 242 )
      return -1;

   // If we haven't received any video yet, don't try retransmissions
   if ( (0 == m_uTimeLastReceivedNewVideoPacket) || (-1 == m_iIndexVideoDecodeStats) )
      return -1;


   // If too much time since we last received a new video packet, then discard the entire rx buffer
   if ( 0 != m_uTimeLastReceivedNewVideoPacket )
   if ( m_iMilisecondsMaxRetransmissionWindow > 10 )
   if ( g_TimeNow >= m_uTimeLastReceivedNewVideoPacket + m_iMilisecondsMaxRetransmissionWindow - 10 )
   {
      log_line("[ProcessorRxVideo] Discard old blocks due to no new video packet for %d ms", m_iMilisecondsMaxRetransmissionWindow);
      m_pVideoRxBuffer->emptyBuffers("No new video past retransmission window");
      //resetReceiveBuffers(m_iRXBlocksStackTopIndex);
      resetOutputState();
      m_uTimeLastReceivedNewVideoPacket = 0;
      return -1;
   }


   //if ( m_uLastTimeRequestedRetransmission < g_TimeNow-200 )
   //   m_uTimeIntervalMsForRequestingRetransmissions = 10;
   //if ( m_uTimeIntervalMsForRequestingRetransmissions < 20 )
   //   m_uTimeIntervalMsForRequestingRetransmissions++;

   if ( (!bForceSyncNow) && (g_TimeNow < m_uLastTimeRequestedRetransmission + m_uTimeIntervalMsForRequestingRetransmissions) )
      return -1;


   // If link is lost, do not request retransmissions

   if ( (!bForceSyncNow) && (0 != g_pControllerSettings->iDisableRetransmissionsAfterControllerLinkLostMiliseconds) )
   {
      u32 uDelta = (u32)g_pControllerSettings->iDisableRetransmissionsAfterControllerLinkLostMiliseconds;
      if ( g_TimeNow > pRuntimeInfo->uLastTimeReceivedAckFromVehicle + uDelta )
         return -1;
   }


   // Request all missing packets except current block

   //#define PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS 20
   // params after header:
   //   u32: retransmission request id
   //   u8: video stream index
   //   u8: number of video packets requested
   //   (u32+u8)*n = each (video block index + video packet index) requested 

   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_VIDEO, PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS, STREAM_ID_DATA);
   PH.vehicle_id_src = g_uControllerId;
   PH.vehicle_id_dest = m_uVehicleId;
   if ( m_uVehicleId == 0 || m_uVehicleId == MAX_U32 )
   {
      PH.vehicle_id_dest = pModel->uVehicleId;
      log_softerror_and_alarm("[ProcessorRxVideo] Tried to request retransmissions before having received a video packet.");
   }

   #ifdef FEATURE_VEHICLE_COMPUTES_ADAPTIVE_VIDEO  
   //if ( pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_ENABLE_ADAPTIVE_VIDEO_LINK )
   //if ( pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_ADAPTIVE_VIDEO_LINK_USE_CONTROLLER_INFO_TOO )
   //if ( g_TimeNow > g_TimeLastControllerLinkStatsSent + CONTROLLER_LINK_STATS_HISTORY_SLICE_INTERVAL_MS/2 )
   //   PH.total_length += get_controller_radio_link_stats_size();
   #endif

   char szDebug[1024];
   szDebug[0] = 0;

   u8 packet[MAX_PACKET_TOTAL_SIZE];
   m_uRequestRetransmissionUniqueId++;
   memcpy(packet + sizeof(t_packet_header), (u8*)&m_uRequestRetransmissionUniqueId, sizeof(u32));
   memcpy(packet + sizeof(t_packet_header) + sizeof(u32), (u8*)&m_uVideoStreamIndex, sizeof(u8));
   u8* pDataInfo = packet + sizeof(t_packet_header) + sizeof(u32) + 2*sizeof(u8);
   int iCountPacketsRequested = 0;
   int iCountBlocks = m_pVideoRxBuffer->getBlocksCountInBuffer();
   for( int i=0; i<iCountBlocks-1; i++ )
   {
      type_rx_video_block_info* pVideoBlock = m_pVideoRxBuffer->getVideoBlockInBuffer(i);
      if ( NULL == pVideoBlock )
         continue;
      int iCountToRequestFromBlock = pVideoBlock->iBlockDataPackets - pVideoBlock->iRecvDataPackets - pVideoBlock->iRecvECPackets;
      if ( iCountToRequestFromBlock > 0 )
      {
         for( int k=0; k<pVideoBlock->iBlockDataPackets; k++ )
         {
            if ( NULL == pVideoBlock->packets[k].pRawData )
               continue;
            if ( ! pVideoBlock->packets[k].bEmpty )
               continue;

            memcpy(pDataInfo, &pVideoBlock->uVideoBlockIndex, sizeof(u32));
            pDataInfo += sizeof(u32);
            u8 uPacketIndex = k;
            memcpy(pDataInfo, &uPacketIndex, sizeof(u8));
            pDataInfo += sizeof(u8);

            char szTmp[32];
            sprintf(szTmp, "[%u/%d] ", pVideoBlock->uVideoBlockIndex, k);
            strcat(szDebug, szTmp);

            iCountToRequestFromBlock--;
            iCountPacketsRequested++;
            if ( iCountToRequestFromBlock == 0 )
               break;
            if ( iCountPacketsRequested > 20 )
              break;
         }
      }
      if ( iCountPacketsRequested > 20 )
        break;
   }

   if ( iCountPacketsRequested == 0 )
      return 0;

   u8 uCount = iCountPacketsRequested;
   memcpy(packet + sizeof(t_packet_header) + sizeof(u32) + sizeof(u8), (u8*)&uCount, sizeof(u8));
   PH.total_length = sizeof(t_packet_header) + sizeof(u32) + 2*sizeof(u8);
   PH.total_length += iCountPacketsRequested*(sizeof(u32) + sizeof(u8)); 
   memcpy(packet, (u8*)&PH, sizeof(t_packet_header));

   m_uLastTimeRequestedRetransmission = g_TimeNow;

   //type_rx_video_block_info* pVideoBlockFirst = m_pVideoRxBuffer->getFirstVideoBlockInBuffer();
   //log_line("DEBUG sent retr %u, %d (%s) (video min/max: %u - %u)",
   //    m_uRequestRetransmissionUniqueId, iCountPacketsRequested, szDebug, pVideoBlockFirst->uVideoBlockIndex, m_pVideoRxBuffer->getMaxReceivedVideoBlockIndex());

   controller_runtime_info_vehicle* pRTInfo = controller_rt_info_get_vehicle_info(&g_SMControllerRTInfo, m_uVehicleId);
   if ( NULL != pRTInfo )
      pRTInfo->uCountReqRetransmissions[g_SMControllerRTInfo.iCurrentIndex]++;

   packets_queue_add_packet(&s_QueueRadioPacketsHighPrio, packet);
   return iCountPacketsRequested;
   
/*
   
      ////////////
      int countToRequestForBlock = 0;

      // For last block, request only missing packets up untill the last received data packet in the block
      if ( i == m_iRXBlocksStackTopIndex )
      {
         bool bCheckAndRequestFromTopBlock = false;

         if ( m_InfoLastReceivedVideoPacket.receive_time + 20 < g_TimeNow )
         if ( (m_pRXBlocksStack[i]->received_data_packets > 0) || (m_pRXBlocksStack[i]->received_fec_packets > 0) )
            bCheckAndRequestFromTopBlock = true;

         if ( 0 != g_pControllerSettings->nRequestRetransmissionsOnVideoSilenceMs )
         if ( 0 != m_pRXBlocksStack[i]->uTimeLastUpdated )
         if ( m_pRXBlocksStack[i]->uTimeLastUpdated + g_pControllerSettings->nRequestRetransmissionsOnVideoSilenceMs < g_TimeNow )
            bCheckAndRequestFromTopBlock = true;

         if ( bCheckAndRequestFromTopBlock )
         {
            // Request missing packets from the start of the block

            int iMaxBlockPacketIndexReceived = -1;
            for( int k=m_pRXBlocksStack[i]->data_packets + m_pRXBlocksStack[i]->fec_packets-1; k>=0; k-- )
            {
               if ( m_pRXBlocksStack[i]->packetsInfo[k].uState & RX_PACKET_STATE_RECEIVED )
               {
                  iMaxBlockPacketIndexReceived = k;
                  break;
               }
            }

            if ( iMaxBlockPacketIndexReceived >= m_pRXBlocksStack[i]->fec_packets )
            if ( m_pRXBlocksStack[i]->received_data_packets + m_pRXBlocksStack[i]->received_fec_packets < iMaxBlockPacketIndexReceived - m_pRXBlocksStack[i]->fec_packets )
            {
               countToRequestForBlock = m_pRXBlocksStack[i]->data_packets - m_pRXBlocksStack[i]->received_data_packets - m_pRXBlocksStack[i]->received_fec_packets;
               if ( countToRequestForBlock > iMaxBlockPacketIndexReceived )
                 countToRequestForBlock = iMaxBlockPacketIndexReceived;
            }
         }
      }
      else
         countToRequestForBlock = m_pRXBlocksStack[i]->data_packets - m_pRXBlocksStack[i]->received_data_packets - m_pRXBlocksStack[i]->received_fec_packets;

      if ( countToRequestForBlock <= 0 )
         continue;

      countToRequestForBlock -= m_pRXBlocksStack[i]->totalPacketsRequested;

      // First, re-request the packets we already requested once.
      // Then, request additional packets if needed (not enough requested for possible reconstruction)
      // Then, request some EC packets (half the original EC rate) proportional to missing packets count
      
      if ( m_pRXBlocksStack[i]->totalPacketsRequested > 0 )
      {
         for( int k=0; k<m_pRXBlocksStack[i]->data_packets; k++ )
         {
            if ( m_pRXBlocksStack[i]->packetsInfo[k].uState & RX_PACKET_STATE_RECEIVED )
               continue;
            if ( m_pRXBlocksStack[i]->packetsInfo[k].uRetrySentCount == 0 )
               continue;
            if ( m_pRXBlocksStack[i]->packetsInfo[k].uTimeLastRetrySent + m_uRetryRetransmissionAfterTimeoutMiliseconds >= g_TimeNow )
               continue;

            m_pRXBlocksStack[i]->packetsInfo[k].uRetrySentCount++;
            m_pRXBlocksStack[i]->uTimeLastRetrySent = g_TimeNow;
            m_pRXBlocksStack[i]->packetsInfo[k].uTimeLastRetrySent = g_TimeNow;

            // Decrease interval of future retransmissions requests for this packet
            u32 dt = 5 * m_pRXBlocksStack[i]->packetsInfo[k].uRetrySentCount;
            if ( dt > m_uRetryRetransmissionAfterTimeoutMiliseconds-10 )
               dt = m_uRetryRetransmissionAfterTimeoutMiliseconds-10;
            m_pRXBlocksStack[i]->packetsInfo[k].uTimeLastRetrySent -= dt;

            memcpy(pBuffer, &(m_pRXBlocksStack[i]->video_block_index), sizeof(u32));
            pBuffer += sizeof(u32);
            *pBuffer = (u8)k;
            pBuffer++;
            *pBuffer = (u8)(m_pRXBlocksStack[i]->packetsInfo[k].uRetrySentCount);
            pBuffer++;
            totalCountRequested++;
            totalCountReRequested++;

            if ( totalCountRequested >= MAX_RETRANSMISSION_PACKETS_IN_REQUEST-2 )
               break;
         }
      }

      if ( totalCountRequested >= MAX_RETRANSMISSION_PACKETS_IN_REQUEST-2 )
         break;

      // Request additional packets from the block if not enough for possible reconstruction

      if ( m_pRXBlocksStack[i]->data_packets - m_pRXBlocksStack[i]->received_data_packets - m_pRXBlocksStack[i]->received_fec_packets - m_pRXBlocksStack[i]->totalPacketsRequested > 0 )
      {
         for( int k=0; k<m_pRXBlocksStack[i]->data_packets; k++ )
         {
            if ( m_pRXBlocksStack[i]->packetsInfo[k].uState & RX_PACKET_STATE_RECEIVED )
               continue;
            if ( m_pRXBlocksStack[i]->packetsInfo[k].uRetrySentCount != 0 )
               continue;

            if ( 0 == m_pRXBlocksStack[i]->packetsInfo[k].uTimeFirstRetrySent )
               m_pRXBlocksStack[i]->packetsInfo[k].uTimeFirstRetrySent = g_TimeNow;
            m_pRXBlocksStack[i]->packetsInfo[k].uTimeLastRetrySent = g_TimeNow;
            m_pRXBlocksStack[i]->packetsInfo[k].uRetrySentCount = 1;
            totalCountRequestedNew++;
            m_pRXBlocksStack[i]->totalPacketsRequested++;

            if ( 0 == m_pRXBlocksStack[i]->uTimeFirstRetrySent )
               m_pRXBlocksStack[i]->uTimeFirstRetrySent = g_TimeNow;
            m_pRXBlocksStack[i]->uTimeLastRetrySent = g_TimeNow;

            memcpy(pBuffer, &(m_pRXBlocksStack[i]->video_block_index), sizeof(u32));
            pBuffer += sizeof(u32);
            *pBuffer = (u8)k;
            pBuffer++;
            *pBuffer = (u8)(m_pRXBlocksStack[i]->packetsInfo[k].uRetrySentCount);
            pBuffer++;

            totalCountRequested++;
            countToRequestForBlock--;
            if ( countToRequestForBlock <= 0 )
               break;

            if ( totalCountRequested >= MAX_RETRANSMISSION_PACKETS_IN_REQUEST-2 )
               break;
         }
      }

      if ( totalCountRequested >= MAX_RETRANSMISSION_PACKETS_IN_REQUEST-2 )
         break;
   }

   // No new video packets for a long time? Request next one

   if ( 0 != g_pControllerSettings->nRequestRetransmissionsOnVideoSilenceMs )
   if ( m_InfoLastReceivedVideoPacket.video_block_index != MAX_U32 )
   if ( m_InfoLastReceivedVideoPacket.receive_time != 0 )
   if ( m_InfoLastReceivedVideoPacket.receive_time + g_pControllerSettings->nRequestRetransmissionsOnVideoSilenceMs <= g_TimeNow )
   {
      u32 videoBlock = m_InfoLastReceivedVideoPacket.video_block_index;
      u32 videoPacket = m_InfoLastReceivedVideoPacket.video_block_packet_index;
      videoPacket++;
      // To fix?
      //if ( videoPacket >= (u32)m_SM_VideoDecodeStats.data_packets_per_block )
      {
         videoPacket = 0;
         videoBlock++;
      }
      memcpy(pBuffer, &videoBlock, sizeof(u32));
      pBuffer += sizeof(u32);
      *pBuffer = (u8)videoPacket;
      pBuffer++;
      *pBuffer = 1;
      pBuffer++;

      totalCountRequested++;
      totalCountRequestedNew++;
   }

   if ( 0 == totalCountRequested )      
      return;

   m_uLastTimeRequestedRetransmission = g_TimeNow;

   if ( bUseNewVersion )
   {
      m_uRequestRetransmissionUniqueId++;
      memcpy((u8*)&(buffer[0]), (u8*)&m_uRequestRetransmissionUniqueId, sizeof(u32));
      buffer[4] = 0; // video stream id
      buffer[5] = (u8) totalCountRequested;
   }
   else
   {
      buffer[0] = 0; // video stream id
      buffer[1] = (u8) totalCountRequested;
   }
   
   
   int iIndex = getVehicleRuntimeIndex(m_uVehicleId);
   if ( -1 != iIndex )
   {
      g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].uIntervalsRequestedRetransmissions[g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iCurrentIntervalIndex] += totalCountRequestedNew;
      g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].uIntervalsRetriedRetransmissions[g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iCurrentIntervalIndex] += totalCountReRequested;
   }

   // Store info about this retransmission request

  

   if ( g_PD_ControllerLinkStats.tmp_video_streams_requested_retransmission_packets[0] + totalCountRequested < 254 )
      g_PD_ControllerLinkStats.tmp_video_streams_requested_retransmission_packets[0] += totalCountRequested;
   else
      g_PD_ControllerLinkStats.tmp_video_streams_requested_retransmission_packets[0] = 254;

   int bufferLength = 0;

   if ( bUseNewVersion )
      bufferLength = sizeof(u32) + 2 + totalCountRequested*(sizeof(u32)+sizeof(u8)+sizeof(u8));
   else
      bufferLength = 2 + totalCountRequested*(sizeof(u32)+sizeof(u8)+sizeof(u8));
   
   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_VIDEO, PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS, STREAM_ID_DATA);
   if ( bUseNewVersion )
      PH.packet_type = PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS2;
   PH.vehicle_id_src = g_uControllerId;
   PH.vehicle_id_dest = m_uVehicleId;
   if ( m_uVehicleId == 0 || m_uVehicleId == MAX_U32 )
   {
      PH.vehicle_id_dest = pModel->uVehicleId;
      log_softerror_and_alarm("[VideoRx] Tried to request retransmissions before having received a video packet.");
   }
   PH.total_length = sizeof(t_packet_header) + bufferLength;

   #ifdef FEATURE_VEHICLE_COMPUTES_ADAPTIVE_VIDEO  
   if ( NULL != pModel )
   if ( pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_ENABLE_ADAPTIVE_VIDEO_LINK )
   if ( pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_ADAPTIVE_VIDEO_LINK_USE_CONTROLLER_INFO_TOO )
   if ( g_TimeNow > g_TimeLastControllerLinkStatsSent + CONTROLLER_LINK_STATS_HISTORY_SLICE_INTERVAL_MS/2 )
      PH.total_length += get_controller_radio_link_stats_size();
   #endif

   u8 packet[MAX_PACKET_TOTAL_SIZE];
   memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
   memcpy(packet + sizeof(t_packet_header), buffer, bufferLength);

   #ifdef FEATURE_VEHICLE_COMPUTES_ADAPTIVE_VIDEO
   if ( NULL != pModel && (pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_ENABLE_ADAPTIVE_VIDEO_LINK) )
   if ( g_TimeNow > g_TimeLastControllerLinkStatsSent + CONTROLLER_LINK_STATS_HISTORY_SLICE_INTERVAL_MS/2 )
   {
      add_controller_radio_link_stats_to_buffer(packet + sizeof(t_packet_header)+bufferLength);
      g_TimeLastControllerLinkStatsSent = g_TimeNow;
   }
   #endif
   
   packets_queue_add_packet(&s_QueueRadioPackets, packet);

   // If there are retried retransmissions, send the request twice
   if ( totalCountReRequested > 5 )
      packets_queue_add_packet(&s_QueueRadioPackets, packet);
      */
}

// To fix
/*
void ProcessorRxVideo::addPacketToReceivedBlocksBuffers(u8* pBuffer, int length, int rx_buffer_block_index, bool bWasRetransmitted)
{
   t_packet_header* pPH = (t_packet_header*)pBuffer;

   Model* pModel = findModelWithId(pPH->vehicle_id_src, 124);
   if ( NULL == pModel )
      return;

   if ( rx_buffer_block_index > m_iRXBlocksStackTopIndex )
      m_iRXBlocksStackTopIndex = rx_buffer_block_index;

   u32 video_block_index = 0;
   u8 video_block_packet_index = 0;
   int iLastAckKeyframeInterval = 0;
   int iLastAckVideoLevelShift = 0;
   u32 uLastSetVideoBitrate = 0;
   u32 uVideoStatusFlags2 = 0;
   
   
   t_packet_header_video_full_77* pPHVF = (t_packet_header_video_full_77*) (pBuffer+sizeof(t_packet_header));    
   video_block_index = pPHVF->video_block_index;
   video_block_packet_index = pPHVF->video_block_packet_index;

   iLastAckKeyframeInterval = pPHVF->uLastAckKeyframeInterval;
   iLastAckVideoLevelShift = pPHVF->uLastAckLevelShift;
   uLastSetVideoBitrate = pPHVF->uLastSetVideoBitrate;
   uVideoStatusFlags2 = pPHVF->uVideoStatusFlags2;


   if ( m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[video_block_packet_index].uState & RX_PACKET_STATE_RECEIVED )
      return;

   m_uTimeLastReceivedNewVideoPacket = g_TimeNow;

   // Begin - Check for last acknowledged values

   if ( ! bWasRetransmitted )
   if ( (m_uLastBlockReceivedEncodingExtraFlags2 == MAX_U32) ||
        (video_block_index > m_uLastBlockReceivedEncodingExtraFlags2) )
   {
      m_uLastBlockReceivedEncodingExtraFlags2 = video_block_index;
      m_SM_VideoDecodeStats.uVideoStatusFlags2 = uVideoStatusFlags2;
   }

   if ( ! bWasRetransmitted )
   if ( (m_uLastBlockReceivedSetVideoBitrate == MAX_U32) ||
        (video_block_index > m_uLastBlockReceivedSetVideoBitrate) )
   {
      m_uLastBlockReceivedSetVideoBitrate = video_block_index;
      m_SM_VideoDecodeStats.uLastSetVideoBitrate = uLastSetVideoBitrate;
      int iIndex = getVehicleRuntimeIndex(m_uVehicleId);
      if ( -1 != iIndex )
      if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].uLastSetVideoBitrate != uLastSetVideoBitrate )
      {
         g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].uLastSetVideoBitrate = uLastSetVideoBitrate;
         //log_line("Received video info (video block index: %u) from VID %u (%u) that video bitrate was set to %u bps", video_block_index, pPH->vehicle_id_src, m_uVehicleId, (uLastSetVideoBitrate & 0x7FFFFFFF));
      }
   }

   if ( ! bWasRetransmitted )
   if ( (m_uLastBlockReceivedAckKeyframeInterval == MAX_U32) ||
        (video_block_index > m_uLastBlockReceivedAckKeyframeInterval) )
   {
      m_uLastBlockReceivedAckKeyframeInterval = video_block_index;
      m_SM_VideoDecodeStats.iLastAckKeyframeInterval = iLastAckKeyframeInterval;

      int iIndex = getVehicleRuntimeIndex(m_uVehicleId);
      if ( -1 != iIndex )
      if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iLastAcknowledgedKeyFrameMs != iLastAckKeyframeInterval )
      {
         g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iLastAcknowledgedKeyFrameMs = iLastAckKeyframeInterval;
         g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iLastRequestedKeyFrameMsRetryCount = 0;
         log_line("Received video ACK (video block index: %u) from VID %u (%u) for setting keyframe to %d ms", video_block_index, pPH->vehicle_id_src, m_uVehicleId, iLastAckKeyframeInterval);
      }
   }

   if ( ! bWasRetransmitted )
   if ( (m_uLastBlockReceivedAdaptiveVideoInterval == MAX_U32) ||
        (video_block_index > m_uLastBlockReceivedAdaptiveVideoInterval) )
   {
      m_uLastBlockReceivedAdaptiveVideoInterval = video_block_index;
      m_SM_VideoDecodeStats.iLastAckVideoLevelShift = iLastAckVideoLevelShift;

      int iIndex = getVehicleRuntimeIndex(m_uVehicleId);
      if ( -1 != iIndex )
      if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iLastAcknowledgedLevelShift != iLastAckVideoLevelShift )
      {
         g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iLastAcknowledgedLevelShift = iLastAckVideoLevelShift;
         g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].uTimeLastAckLevelShift = g_TimeNow;
         g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iLastRequestedLevelShiftRetryCount = 0;
         log_line("Received video ACK (video block index: %u) from VID %u (%u) for setting video level shift to %d", video_block_index, pPH->vehicle_id_src, m_uVehicleId, iLastAckVideoLevelShift);
      }
   }

   // End - Check for last acknowledged values


   if ( m_pRXBlocksStack[rx_buffer_block_index]->uTimeFirstPacketReceived == MAX_U32 )
      m_pRXBlocksStack[rx_buffer_block_index]->uTimeFirstPacketReceived = g_TimeNow;


   m_pRXBlocksStack[rx_buffer_block_index]->video_block_index = pPHVF->video_block_index;
   m_pRXBlocksStack[rx_buffer_block_index]->video_data_length = pPHVF->video_data_length;
   m_pRXBlocksStack[rx_buffer_block_index]->data_packets = pPHVF->block_packets;
   m_pRXBlocksStack[rx_buffer_block_index]->fec_packets = pPHVF->block_fecs;
   m_pRXBlocksStack[rx_buffer_block_index]->uTimeLastUpdated = g_TimeNow;
   m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[pPHVF->video_block_packet_index].uState |= RX_PACKET_STATE_RECEIVED;
   m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[pPHVF->video_block_packet_index].video_data_length = pPHVF->video_data_length;
   m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[pPHVF->video_block_packet_index].packet_length = length;

   if ( length < 100 || length > MAX_PACKET_TOTAL_SIZE )
      log_softerror_and_alarm("Invalid video data size to copy (%d bytes)", length);
   else
      memcpy(m_pRXBlocksStack[rx_buffer_block_index]->packetsInfo[video_block_packet_index].pData, pBuffer+sizeof(t_packet_header)+sizeof(t_packet_header_video_full_77), length - sizeof(t_packet_header) - sizeof(t_packet_header_video_full_77));

   if ( video_block_packet_index < m_pRXBlocksStack[rx_buffer_block_index]->data_packets )
      m_pRXBlocksStack[rx_buffer_block_index]->received_data_packets++;
   else
      m_pRXBlocksStack[rx_buffer_block_index]->received_fec_packets++;


   m_SM_VideoDecodeStats.currentPacketsInBuffers++;
   if ( m_SM_VideoDecodeStats.currentPacketsInBuffers > m_SM_VideoDecodeStats.maxPacketsInBuffers )
      m_SM_VideoDecodeStats.maxPacketsInBuffers = m_SM_VideoDecodeStats.currentPacketsInBuffers;
}
*/

// Returns index in the receive stack for the block where we added the packet, or -1 if it was discarded

int ProcessorRxVideo::processRetransmittedVideoPacket(u8* pBuffer, int length)
{
   t_packet_header* pPH = (t_packet_header*)pBuffer;
   Model* pModel = findModelWithId(pPH->vehicle_id_src, 125);
   if ( NULL == pModel )
      return -1;

   //if ( -1 == m_iRXBlocksStackTopIndex )
   //   return -1;

   //u32 video_block_index = 0;
   //u8 video_block_packet_index = 0;
   
   // To fix
   /*
   t_packet_header_video_full_77* pPHVF = (t_packet_header_video_full_77*) (pBuffer+sizeof(t_packet_header));    
   video_block_index = pPHVF->video_block_index;
   video_block_packet_index = pPHVF->video_block_packet_index;

   if ( video_block_index < m_pRXBlocksStack[0]->video_block_index )
      return -1;
   
   if ( video_block_index > m_pRXBlocksStack[m_iRXBlocksStackTopIndex]->video_block_index )
      return -1;

   int dest_stack_index = (video_block_index-m_pRXBlocksStack[0]->video_block_index);
   if ( (dest_stack_index < 0) || (dest_stack_index >= m_iRXMaxBlocksToBuffer) )
      return -1;

   if ( m_pRXBlocksStack[dest_stack_index]->packetsInfo[video_block_packet_index].uState & RX_PACKET_STATE_RECEIVED )
      return -1;

   addPacketToReceivedBlocksBuffers(pBuffer, length, dest_stack_index, true);

   return dest_stack_index;
   */
   return 0;
}

// Returns index in the receive stack for the block where we added the packet, or -1 if it was discarded

int ProcessorRxVideo::processReceivedVideoPacket(u8* pBuffer, int length)
{
   t_packet_header* pPH = (t_packet_header*)pBuffer;
   Model* pModel = findModelWithId(pPH->vehicle_id_src, 126);
   if ( NULL == pModel )
      return -1;
   u32 video_block_index = 0;
   u8 video_block_packet_index = 0;
   
   u8  block_packets = 0;
   u8  block_fecs = 0;

// To fix
   /*
   t_packet_header_video_full_77* pPHVF = (t_packet_header_video_full_77*) (pBuffer+sizeof(t_packet_header));    
   video_block_index = pPHVF->video_block_index;
   video_block_packet_index = pPHVF->video_block_packet_index;
   block_packets = pPHVF->block_packets;
   block_fecs = pPHVF->block_fecs;

   // Find a position in blocks buffer where to put it

   // Empty buffers?

   if ( m_iRXBlocksStackTopIndex == -1 )
   {
      // Never used?
      if ( m_uLastOutputVideoBlockIndex == MAX_U32 )
      {
         // If we are past recoverable point in a block just discard it and wait for a new block
         if ( video_block_packet_index > block_fecs )
         {
            return -1;
         }
         log("[VideoRx] Started new buffers at[%u/%d]", video_block_index, video_block_packet_index);
         m_iRXBlocksStackTopIndex = 0;
         addPacketToReceivedBlocksBuffers(pBuffer, length, 0, false);
         return 0;
      }
   }

   // Discard received packets from blocks prior to the first block in stack or prior to the last output block

   if ( m_uLastOutputVideoBlockIndex != MAX_U32 )
   {
      if ( video_block_index < m_uLastOutputVideoBlockIndex )
         return -1;
      if ( video_block_index == m_uLastOutputVideoBlockIndex )
      if ( video_block_packet_index <= m_uLastOutputVideoBlockPacketIndex )
         return -1;
   }

   if ( m_iRXBlocksStackTopIndex >= 0 )
   if ( video_block_index < m_pRXBlocksStack[0]->video_block_index )
      return -1;


   // Find position for this block in the receive stack

   u32 stackIndex = 0;
   if ( (m_iRXBlocksStackTopIndex >= 0) && (video_block_index >= m_pRXBlocksStack[0]->video_block_index) )
      stackIndex = video_block_index - m_pRXBlocksStack[0]->video_block_index;
   //else if ( m_uLastOutputVideoBlockIndex != MAX_U32 )
   //   stackIndex = video_block_index - m_uLastOutputVideoBlockIndex-1;
   
   if ( stackIndex >= (u32)m_iRXMaxBlocksToBuffer )
   {
      int overflow = stackIndex - m_iRXMaxBlocksToBuffer+1;
      if ( (overflow > m_iRXMaxBlocksToBuffer*2/3) || ((stackIndex - (u32)m_iRXBlocksStackTopIndex) > (u32)m_iRXMaxBlocksToBuffer*2/3) )
      {
         log_line("[VideoRx] Discard some rx video blocks as stack is full. %d blocks pending in the stack, oveflow by %d blocks.", m_iRXMaxBlocksToBuffer, overflow );
         resetReceiveBuffers(m_iRXMaxBlocksToBuffer);
         resetOutputState();
      }
      else
      {
         // Discard few more blocks if they also have missing packets and can't be reconstructed right now; to avoid multiple consecutive discards events
         int iLookAhead = 2 + m_iRXMaxBlocksToBuffer/10;
         while ( (overflow < m_iRXBlocksStackTopIndex) && (iLookAhead > 0) )
         {
            if ( m_pRXBlocksStack[overflow]->received_data_packets + m_pRXBlocksStack[overflow]->received_fec_packets >= m_pRXBlocksStack[overflow]->data_packets )
               break;
            overflow++;
            iLookAhead--;
         }
        
         pushIncompleteBlocksOut(overflow, false);
      }

      // Did we discarded everything?
      if ( -1 == m_iRXBlocksStackTopIndex )
      {
         // If we are past recoverable point in a block just discard it and wait for a new block
         if ( video_block_packet_index > block_fecs )
            return -1;

         m_iRXBlocksStackTopIndex = 0;
         addPacketToReceivedBlocksBuffers(pBuffer, length, 0, false);
         return 0;
      }
      stackIndex -= overflow;
   }

   // Add the packet to the buffer
   addPacketToReceivedBlocksBuffers(pBuffer, length, stackIndex, false);
   
   // Add info about any missing blocks in the stack: video block indexes, data scheme, last update time for any skipped blocks
   for( u32 i=0; i<stackIndex; i++ )
      if ( 0 == m_pRXBlocksStack[i]->uTimeLastUpdated )
      {
         m_pRXBlocksStack[i]->uTimeLastUpdated = g_TimeNow;
         m_pRXBlocksStack[i]->data_packets = block_packets;
         m_pRXBlocksStack[i]->fec_packets = block_fecs;
         m_pRXBlocksStack[i]->video_block_index = video_block_index - stackIndex + i;
      }
   return stackIndex;
   */
   return 0;
}

// Return -1 if discarded

int ProcessorRxVideo::preProcessRetransmittedVideoPacket(int interfaceNb, u8* pBuffer, int length)
{
   t_packet_header* pPH = (t_packet_header*) pBuffer;
   
   Model* pModel = findModelWithId(pPH->vehicle_id_src, 127);
   if ( NULL == pModel )
      return -1;
   
   u32 video_block_index = 0;
   u8 video_block_packet_index = 0;
   u32 uIdRetransmissionRequest = 0;

   // To fix
   /*
   t_packet_header_video_full_77* pPHVF = (t_packet_header_video_full_77*) (pBuffer+sizeof(t_packet_header));    
   video_block_index = pPHVF->video_block_index;
   video_block_packet_index = pPHVF->video_block_packet_index;
   uIdRetransmissionRequest = pPHVF->uLastRecvVideoRetransmissionId;

   int iRetransmissionIndex = -1;

   // Compute retransmission roundtrip for this packet

   u32 uSinglePacketRetransmissionTime = 0;
   if ( video_block_index >= m_pRXBlocksStack[0]->video_block_index )
   if ( (m_iRXBlocksStackTopIndex >=0) && (video_block_index < m_pRXBlocksStack[m_iRXBlocksStackTopIndex]->video_block_index) )
   {
      int dest_stack_index = (video_block_index-m_pRXBlocksStack[0]->video_block_index);
      if ( (dest_stack_index >= 0) && (dest_stack_index < m_iRXMaxBlocksToBuffer) )
      if ( ! (m_pRXBlocksStack[dest_stack_index]->packetsInfo[video_block_packet_index].uState & RX_PACKET_STATE_RECEIVED) )
      if ( m_pRXBlocksStack[dest_stack_index]->packetsInfo[video_block_packet_index].uTimeFirstRetrySent != 0 )
      {
         uSinglePacketRetransmissionTime = g_TimeNow - m_pRXBlocksStack[dest_stack_index]->packetsInfo[video_block_packet_index].uTimeFirstRetrySent;
      
         m_SM_RetransmissionsStats.history[0].uAvgRetransmissionRoundtripTimeSinglePacket += uSinglePacketRetransmissionTime;
         m_SM_RetransmissionsStats.history[0].uCountReceivedSingleUniqueRetransmittedPackets++;

         if ( 0 == m_SM_RetransmissionsStats.history[0].uMinRetransmissionRoundtripTimeSinglePacket )
            m_SM_RetransmissionsStats.history[0].uMinRetransmissionRoundtripTimeSinglePacket = uSinglePacketRetransmissionTime;
         if ( 0 == m_SM_RetransmissionsStats.history[0].uMaxRetransmissionRoundtripTimeSinglePacket )
            m_SM_RetransmissionsStats.history[0].uMaxRetransmissionRoundtripTimeSinglePacket = uSinglePacketRetransmissionTime;
         
         if ( uSinglePacketRetransmissionTime < m_SM_RetransmissionsStats.history[0].uMinRetransmissionRoundtripTimeSinglePacket )
            m_SM_RetransmissionsStats.history[0].uMinRetransmissionRoundtripTimeSinglePacket = uSinglePacketRetransmissionTime;
         if ( (uSinglePacketRetransmissionTime < 255) && (uSinglePacketRetransmissionTime > m_SM_RetransmissionsStats.history[0].uMaxRetransmissionRoundtripTimeSinglePacket) )
            m_SM_RetransmissionsStats.history[0].uMaxRetransmissionRoundtripTimeSinglePacket = uSinglePacketRetransmissionTime;
         if ( uSinglePacketRetransmissionTime >= 255 )
            m_SM_RetransmissionsStats.history[0].uMaxRetransmissionRoundtripTimeSinglePacket = 255;
      }
   }

   // Found the retransmission info in the list
   if ( iRetransmissionIndex != -1 )
   {
       u32 dTime = (g_TimeNow - pRetransmissionInfo->uRequestTime);
       if ( (dTime < pRetransmissionInfo->uMinResponseTime) || pRetransmissionInfo->uMinResponseTime == 0 )
          pRetransmissionInfo->uMinResponseTime = dTime;
       if ( dTime > pRetransmissionInfo->uMaxResponseTime )
          pRetransmissionInfo->uMaxResponseTime = dTime;

       if ( dTime < m_SM_RetransmissionsStats.history[0].uMinRetransmissionRoundtripTime ||
            m_SM_RetransmissionsStats.history[0].uMinRetransmissionRoundtripTime == 0 )
          m_SM_RetransmissionsStats.history[0].uMinRetransmissionRoundtripTime = dTime;
       if ( (dTime < 255) && dTime > m_SM_RetransmissionsStats.history[0].uMaxRetransmissionRoundtripTime )
          m_SM_RetransmissionsStats.history[0].uMaxRetransmissionRoundtripTime = dTime;

       // Find the video packet info in the list for this retransmission

       for( int i=0; i<pRetransmissionInfo->uRequestedPackets; i++ )
       {
          if ( pRetransmissionInfo->uRequestedVideoBlockIndex[i] == video_block_index )
          if ( pRetransmissionInfo->uRequestedVideoBlockPacketIndex[i] == video_block_packet_index )
          {
             // Received the first ever response for this retransmission 
             if ( 0 == pRetransmissionInfo->uReceivedPackets )
             {
                m_SM_RetransmissionsStats.history[0].uCountAcknowledgedRetransmissions++;
                m_SM_RetransmissionsStats.totalReceivedRetransmissions++;
             }

             // Received this segment for the first time
             if ( 0 == pRetransmissionInfo->uReceivedPacketCount[i] )
             {
                m_SM_RetransmissionsStats.listActiveRetransmissions[iRetransmissionIndex].uReceivedPacketTime[i] = g_TimeNow;
                m_SM_RetransmissionsStats.history[0].uAverageRetransmissionRoundtripTime += dTime;
                m_SM_RetransmissionsStats.history[0].uCountReceivedRetransmissionPackets++;
                m_SM_RetransmissionsStats.totalReceivedVideoPackets++;
                pRetransmissionInfo->uReceivedPackets++;
             }
             else
                m_SM_RetransmissionsStats.history[0].uCountReceivedRetransmissionPacketsDuplicate++;
                
             pRetransmissionInfo->uReceivedPacketCount[i]++;
             break;
          }
       }

       if ( pRetransmissionInfo->uRequestedPackets == pRetransmissionInfo->uReceivedPackets )
       {
          m_SM_RetransmissionsStats.history[0].uCountCompletedRetransmissions++;
          // Remove it from the retransmissions list
          m_SM_RetransmissionsStats.iCountActiveRetransmissions--;
       }
   }
   
   if ( -1 == m_iRXBlocksStackTopIndex )
      return -1;

   if ( m_uLastOutputVideoBlockIndex != MAX_U32 )
   {
      if ( video_block_index < m_uLastOutputVideoBlockIndex )
         return -1;
      if ( video_block_index == m_uLastOutputVideoBlockIndex )
      if ( video_block_packet_index <= m_uLastOutputVideoBlockPacketIndex )
         return -1;
   }

   int stackIndex = video_block_index - m_pRXBlocksStack[0]->video_block_index;
   if ( (stackIndex < 0) || (stackIndex >= m_iRXMaxBlocksToBuffer) )
      return -1;

   if ( m_pRXBlocksStack[stackIndex]->packetsInfo[video_block_packet_index].uState & RX_PACKET_STATE_RECEIVED )
      return -1;
*/
   return 0;
}


// Returns the gap from the last received packet, or -1 if discarded
// The gap is 0 (return value) for retransmitted packets that are not ignored

int ProcessorRxVideo::preProcessReceivedVideoPacket(int interfaceNb, u8* pBuffer, int length)
{
   t_packet_header* pPH = (t_packet_header*) pBuffer;
   
   Model* pModel = findModelWithId(pPH->vehicle_id_src, 128);
   if ( NULL == pModel )
      return -1;

   if ( pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED )
   {
      return preProcessRetransmittedVideoPacket(interfaceNb, pBuffer, length );
   }

   u32 video_block_index = 0;
   u8 video_block_packet_index = 0;
   u8  block_packets = 0;
   u8  block_fecs = 0;

   // To fix
   /*
   t_packet_header_video_full_77* pPHVF = (t_packet_header_video_full_77*) (pBuffer+sizeof(t_packet_header));    
   video_block_index = pPHVF->video_block_index;
   video_block_packet_index = pPHVF->video_block_packet_index;
   u16 video_data_length = pPHVF->video_data_length;
   block_packets = pPHVF->block_packets;
   block_fecs = pPHVF->block_fecs;

   u32 prevRecvVideoBlockIndex = m_InfoLastReceivedVideoPacket.video_block_index;
   u32 prevRecvVideoBlockPacketIndex = m_InfoLastReceivedVideoPacket.video_block_packet_index;

   m_InfoLastReceivedVideoPacket.receive_time = g_TimeNow;
   m_InfoLastReceivedVideoPacket.stream_packet_idx = (pPH->stream_packet_idx & PACKET_FLAGS_MASK_STREAM_PACKET_IDX);
   m_InfoLastReceivedVideoPacket.video_block_index = video_block_index;
   m_InfoLastReceivedVideoPacket.video_block_packet_index = video_block_packet_index;

   if ( m_uLastOutputVideoBlockIndex != MAX_U32 )
   {
      if ( video_block_index < m_uLastOutputVideoBlockIndex )
         return -1;

      if ( video_block_index == m_uLastOutputVideoBlockIndex )
      {
         if ( video_block_packet_index <= m_uLastOutputVideoBlockPacketIndex )
            return -1;
         if ( video_block_packet_index >= block_packets )
         if ( m_uLastOutputVideoBlockPacketIndex == (u32)(block_packets-1) )
            return -1;
      }
   }

   if ( -1 != m_iRXBlocksStackTopIndex )
   {
      if ( video_block_index < m_pRXBlocksStack[0]->video_block_index )
         return -1;
      
      int stackIndex = video_block_index - m_pRXBlocksStack[0]->video_block_index;
      
      if ( (stackIndex >= 0) && (stackIndex < m_iRXMaxBlocksToBuffer) )
      if ( m_pRXBlocksStack[stackIndex]->packetsInfo[video_block_packet_index].uState & RX_PACKET_STATE_RECEIVED )
         return -1;
   }
   
   // Regular video packets that will be processed further

   // Compute gap (in packets) between this and last video packet received;
   // Ignore EC packets as they can be out of order;
   // Used only for history reporting purposes

   int gap = 0;
   if ( video_block_packet_index < block_packets )
   if ( prevRecvVideoBlockPacketIndex < block_packets )
   {
      if ( video_block_index == prevRecvVideoBlockIndex )
      if ( video_block_packet_index > prevRecvVideoBlockPacketIndex )
         gap = video_block_packet_index - prevRecvVideoBlockPacketIndex-1;

      if ( video_block_index > prevRecvVideoBlockIndex )
      {
          gap = block_packets - prevRecvVideoBlockPacketIndex - 1;
          gap += video_block_packet_index;

          gap += (block_packets) * (video_block_index - prevRecvVideoBlockIndex - 1);
      }
      if ( gap > m_SM_VideoDecodeStatsHistory.outputHistoryBlocksMaxPacketsGapPerPeriod[0] )
         m_SM_VideoDecodeStatsHistory.outputHistoryBlocksMaxPacketsGapPerPeriod[0] = (gap>255) ? 255:gap;
   }
   // Check video resolution change

   int width = 0;
   int height = 0;
   u8 video_link_profile = 0;
   u32 uProfileEncodingFlags = 0;
   int keyframe_ms = 0;
   u8 video_stream_and_type = 0;
   u8 video_fps = 0;
   u32 fec_time = 0;

   width = pPHVF->video_width;
   height = pPHVF->video_height;
   video_link_profile = pPHVF->video_link_profile;
   uProfileEncodingFlags = pPHVF->uProfileEncodingFlags;
   keyframe_ms = pPHVF->video_keyframe_interval_ms;
   video_stream_and_type = pPHVF->video_stream_and_type;
   video_fps = pPHVF->video_fps;
   fec_time = pPHVF->fec_time;

   if ( m_SM_VideoDecodeStats.width != 0 && (m_SM_VideoDecodeStats.width != width || m_SM_VideoDecodeStats.height != height) )
   if ( (m_uLastVideoResolutionChangeVideoBlockIndex == MAX_U32) || (video_block_index > m_uLastVideoResolutionChangeVideoBlockIndex) )
   {
      m_uTimeLastVideoStreamChanged = g_TimeNow;
      m_uLastVideoResolutionChangeVideoBlockIndex = video_block_index;
      log("[VideoRx] Video resolution changed at video block index %u (From %d x %d to %d x %d). Signal reload of the video player.",
         video_block_index, m_SM_VideoDecodeStats.width, m_SM_VideoDecodeStats.height, width, height); 
   }

   // Check if encodings changed (ignore flags related to tx only or irelevant: retransmission window, duplication percent)

   if ( (m_SM_VideoDecodeStats.video_link_profile & 0x0F) != (video_link_profile & 0x0F) ||
        (m_SM_VideoDecodeStats.uProfileEncodingFlags & (~(VIDEO_PROFILE_ENCODING_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT | 0x00FF0000))) != (uProfileEncodingFlags & (~(VIDEO_PROFILE_ENCODING_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT | 0x00FF0000))) ||
        m_SM_VideoDecodeStats.data_packets_per_block != block_packets ||
        m_SM_VideoDecodeStats.fec_packets_per_block != block_fecs ||
        m_SM_VideoDecodeStats.video_data_length != video_data_length
      )
   if ( (m_uLastHardEncodingsChangeVideoBlockIndex == MAX_U32) || (video_block_index > m_uLastHardEncodingsChangeVideoBlockIndex) )
   {
      m_uEncodingsChangeCount++;
      m_uLastHardEncodingsChangeVideoBlockIndex = video_block_index;

      
      bool bOnlyECschemeChanged = false;
      if ( (block_packets != m_SM_VideoDecodeStats.data_packets_per_block) || (block_fecs != m_SM_VideoDecodeStats.fec_packets_per_block) )
      if ( (m_SM_VideoDecodeStats.video_link_profile & 0x0F) == (video_link_profile & 0x0F) &&
           (m_SM_VideoDecodeStats.uProfileEncodingFlags & (~(VIDEO_PROFILE_ENCODING_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT | 0x00FF0000))) == (uProfileEncodingFlags & (~(VIDEO_PROFILE_ENCODING_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT | 0x00FF0000))) &&
           (m_SM_VideoDecodeStats.video_data_length == video_data_length)
         )
         bOnlyECschemeChanged = true;

      static u32 s_TimeLastEncodingsChangedLog = 0;
      if ( bOnlyECschemeChanged )
      {
          log("[VideoRx] Detected (only) error correction scheme change (%u times) on video at block idx %u. Data/EC Old: %d/%d, New: %d/%d", m_uEncodingsChangeCount, video_block_index, m_SM_VideoDecodeStats.data_packets_per_block, m_SM_VideoDecodeStats.fec_packets_per_block, block_packets, block_fecs);
      }
      else if ( g_TimeNow > s_TimeLastEncodingsChangedLog + 5000 )
      {
         s_TimeLastEncodingsChangedLog = g_TimeNow;
         m_uTimeLastVideoStreamChanged = g_TimeNow;
         log_line("[VideoRx] Detected video encodings change (%u times) on the received stream (at video block index %u):", m_uEncodingsChangeCount, video_block_index);
         if ( ( m_SM_VideoDecodeStats.video_link_profile & 0x0F) != (video_link_profile & 0x0F) )
            log_line("Video link profile (from [user/current] to [user/current]: [%s/%s] -> [%s/%s]", str_get_video_profile_name((m_SM_VideoDecodeStats.video_link_profile >> 4) & 0x0F), str_get_video_profile_name(m_SM_VideoDecodeStats.video_link_profile & 0x0F), str_get_video_profile_name((video_link_profile >> 4) & 0x0F), str_get_video_profile_name(video_link_profile & 0x0F));
         if ( m_SM_VideoDecodeStats.data_packets_per_block != block_packets )
            log_line("data packets (from/to): %d -> %d", m_SM_VideoDecodeStats.data_packets_per_block, block_packets);
         if ( m_SM_VideoDecodeStats.fec_packets_per_block != block_fecs )
            log_line("ec packets (from/to): %d -> %d", m_SM_VideoDecodeStats.fec_packets_per_block, block_fecs);
         if ( m_SM_VideoDecodeStats.video_data_length != video_data_length )
            log_line("video data length (from/to): %d -> %d", m_SM_VideoDecodeStats.video_data_length, video_data_length);
         if ( (m_SM_VideoDecodeStats.uProfileEncodingFlags & (~(VIDEO_PROFILE_ENCODING_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT | 0x00FF0000))) != (uProfileEncodingFlags & (~(VIDEO_PROFILE_ENCODING_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT | 0x00FF0000))) )
            log_line("Encoding flags (from/to): %u -> %u", m_SM_VideoDecodeStats.uProfileEncodingFlags, uProfileEncodingFlags);
         log_line("keyframe (from/to): %d ms -> %d ms", m_SM_VideoDecodeStats.keyframe_ms, keyframe_ms);
      }
      else
      {
         //log_line("New encodings: [%u/0], %d/%d/%d", pPHVF->video_block_index, pPHVF->block_packets, pPHVF->block_fecs, pPHVF->video_data_length);
      }
      // Max retransmission window changed on user selected profile?

      bool bReinitDueToWindowChange = false;
      if ( (video_link_profile & 0x0F) == pModel->video_params.user_selected_video_link_profile )
      if ( m_uLastReceivedVideoLinkProfile == pModel->video_params.user_selected_video_link_profile )
      if ( (uProfileEncodingFlags & 0xFF00) != (m_SM_VideoDecodeStats.uProfileEncodingFlags & 0xFF00) )
         bReinitDueToWindowChange = true;

      if ( (m_SM_VideoDecodeStats.uProfileEncodingFlags & (~(VIDEO_PROFILE_ENCODING_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT | 0x00FF0000))) != (uProfileEncodingFlags & (~(VIDEO_PROFILE_ENCODING_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT | 0x00FF0000))) )
      {
         log_line("[VideoRx] Encoding flags changed from/to: %u -> %u.", m_SM_VideoDecodeStats.uProfileEncodingFlags, uProfileEncodingFlags);
         log_line("[VideoRx] Encoding flags old: [%s]", str_format_video_encoding_flags(m_SM_VideoDecodeStats.uProfileEncodingFlags));
         log_line("[VideoRx] Encoding flags new: [%s]", str_format_video_encoding_flags(uProfileEncodingFlags));
      }   
      m_SM_VideoDecodeStats.video_link_profile = video_link_profile;
      m_SM_VideoDecodeStats.uProfileEncodingFlags = uProfileEncodingFlags;
      m_SM_VideoDecodeStats.data_packets_per_block = block_packets;
      m_SM_VideoDecodeStats.fec_packets_per_block = block_fecs;
      m_SM_VideoDecodeStats.video_data_length = video_data_length;

      if ( bReinitDueToWindowChange )
      {
         log("[VideoRx] Retransmission window (milisec) has changed (to %d ms). Reinitializing rx state...", ((uProfileEncodingFlags & 0xFF00)>>8)*5);
         resetReceiveState();
         resetOutputState();
         gap = 0;
         m_uLastHardEncodingsChangeVideoBlockIndex = video_block_index;
         m_uLastVideoResolutionChangeVideoBlockIndex = video_block_index;      
      }
   }

   #if defined(RUBY_BUILD_HW_PLATFORM_PI)
   if (((pPHVF->video_stream_and_type >> 4) & 0x0F) == VIDEO_TYPE_H265 )
   {
      static u32 s_uTimeLastSendVideoUnsuportedAlarmToCentral = 0;
      if ( g_TimeNow > s_uTimeLastSendVideoUnsuportedAlarmToCentral + 20000 )
      {
         s_uTimeLastSendVideoUnsuportedAlarmToCentral = g_TimeNow;
         send_alarm_to_central(ALARM_ID_UNSUPPORTED_VIDEO_TYPE, pPHVF->video_stream_and_type, pPH->vehicle_id_src);
      }
   }
   #endif
   
   m_uLastReceivedVideoLinkProfile = (video_link_profile & 0x0F);

   if ( (m_SM_VideoDecodeStats.uProfileEncodingFlags & (~(VIDEO_PROFILE_ENCODING_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT | 0x00FF0000))) != (uProfileEncodingFlags & (~(VIDEO_PROFILE_ENCODING_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT | 0x00FF0000))) )
   {
      log_line("[VideoRx] Received video encoding flags changed from/to: %u -> %u.", m_SM_VideoDecodeStats.uProfileEncodingFlags, uProfileEncodingFlags);
      log_line("[VideoRx] Video encoding flags old: [%s]", str_format_video_encoding_flags(m_SM_VideoDecodeStats.uProfileEncodingFlags));
      log_line("[VideoRx] Video encoding flags new: [%s]", str_format_video_encoding_flags(uProfileEncodingFlags));
   } 

   m_SM_VideoDecodeStats.uProfileEncodingFlags = uProfileEncodingFlags;
   m_SM_VideoDecodeStats.video_stream_and_type = video_stream_and_type;
   m_SM_VideoDecodeStats.keyframe_ms = keyframe_ms;
   m_SM_VideoDecodeStats.fps = video_fps;
   m_SM_VideoDecodeStats.width = width;
   m_SM_VideoDecodeStats.height = height;
   m_SM_VideoDecodeStats.fec_time = fec_time;
   
   // Set initial stream level shift

   int iIndex = getVehicleRuntimeIndex(m_uVehicleId);
   if ( -1 != iIndex )
   {
      if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iCurrentTargetLevelShift == -1 )
         video_link_adaptive_set_intial_video_adjustment_level(m_uVehicleId, m_uLastReceivedVideoLinkProfile, block_packets, block_fecs);

      if ( m_SM_VideoDecodeStats.keyframe_ms > 0 )
      if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iLastRequestedKeyFrameMs <= 0 )
         video_link_keyframe_set_intial_received_level(m_uVehicleId, m_SM_VideoDecodeStats.keyframe_ms);
   }

   return gap;
   */
   return 0;
}


int ProcessorRxVideo::onNewReceivedValidVideoPacket(Model* pModel, u8* pBuffer, int length, int iAddedToStackIndex)
{
  // To fix
 /*
   t_packet_header* pPH = (t_packet_header*) pBuffer;
   t_packet_header_video_full_77* pPHVF = (t_packet_header_video_full_77*) (pBuffer+sizeof(t_packet_header));    

   u32 video_block_index = pPHVF->video_block_index;
   u8 video_block_packet_index = pPHVF->video_block_packet_index;
   u8 block_packets = pPHVF->block_packets;
   u8 block_fecs = pPHVF->block_fecs;

   int nReturnCanTx = 0;
   if ( pPH->packet_flags & PACKET_FLAGS_BIT_CAN_START_TX )
   if ( !( pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED) )
   if ( video_block_packet_index >= block_packets + block_fecs - 1 )
      nReturnCanTx = 1;

   if ( pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED )
   {
      if ( m_SM_VideoDecodeStatsHistory.outputHistoryReceivedVideoRetransmittedPackets[0] < 254 )
         m_SM_VideoDecodeStatsHistory.outputHistoryReceivedVideoRetransmittedPackets[0]++;
   }
   else
   {
      if ( m_SM_VideoDecodeStatsHistory.outputHistoryReceivedVideoPackets[0] < 254 )
         m_SM_VideoDecodeStatsHistory.outputHistoryReceivedVideoPackets[0]++;
   }
   
   bool bCanSendPacketNow = false;
   
   if ( video_block_packet_index == 0 )
   if ( video_block_index == m_uLastOutputVideoBlockIndex+1 )
   if ( m_uLastOutputVideoBlockPacketIndex == (m_uLastOutputVideoBlockDataPackets-1) )
      bCanSendPacketNow = true;

   if ( video_block_packet_index < block_packets )
   if ( video_block_index == m_uLastOutputVideoBlockIndex )
   if ( video_block_packet_index == m_uLastOutputVideoBlockPacketIndex+1 )
      bCanSendPacketNow = true;

   if ( bCanSendPacketNow )
   {
      sendPacketToOutput(iAddedToStackIndex, video_block_packet_index);
   }

   // Can we output the first few blocks?
   int maxBlocksToOutputIfAvailable = MAX_BLOCKS_TO_OUTPUT_IF_AVAILABLE;
   do
   {
      if ( (m_iRXBlocksStackTopIndex < 0) || (m_pRXBlocksStack[0]->data_packets == 0) )
         break;
      if ( m_pRXBlocksStack[0]->received_data_packets + m_pRXBlocksStack[0]->received_fec_packets < m_pRXBlocksStack[0]->data_packets )
         break;

      pushFirstBlockOut();
      nReturnCanTx = 1;
      maxBlocksToOutputIfAvailable--;
   }
   while ( maxBlocksToOutputIfAvailable > 0 );


   // Output packets from first video block (if we still have one) if we did reconstructed and outputed older blocks
   if ( m_iRXBlocksStackTopIndex >= 0 )
   if ( maxBlocksToOutputIfAvailable != MAX_BLOCKS_TO_OUTPUT_IF_AVAILABLE )

   {
      for( int i=0; i<m_pRXBlocksStack[0]->data_packets; i++ )
      {
         if ( ! (m_pRXBlocksStack[0]->packetsInfo[i].uState & RX_PACKET_STATE_RECEIVED) )
            break;

         bCanSendPacketNow = false;
   
        if ( i == 0 )
        if ( m_pRXBlocksStack[0]->video_block_index == m_uLastOutputVideoBlockIndex+1 )
        if ( m_uLastOutputVideoBlockPacketIndex == (m_uLastOutputVideoBlockDataPackets-1) )
           bCanSendPacketNow = true;

        if ( i == (int)m_uLastOutputVideoBlockPacketIndex+1 )
           bCanSendPacketNow = true;

        if ( ! bCanSendPacketNow )
           break;

        sendPacketToOutput(0, i);
      }
   }

   // Output it anyway if not using bidirectional video or not using retransmissions and we are past the first block
   // if EC scheme is new, we need to wait [ECSpread] block
   // if EC scheme is old, we need to wait only one block

   bool bOutputBocksAsIs = false;

   if ( pModel->isVideoLinkFixedOneWay() )
      bOutputBocksAsIs = true;
   if ( !( m_SM_VideoDecodeStats.uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_ENABLE_RETRANSMISSIONS ) )
      bOutputBocksAsIs = true;
   if ( g_pControllerSettings->iDisableRetransmissionsAfterControllerLinkLostMiliseconds != 0 )
   if ( g_TimeNow > g_uTimeLastReceivedResponseToAMessage + g_pControllerSettings->iDisableRetransmissionsAfterControllerLinkLostMiliseconds )
      bOutputBocksAsIs = true;

   if ( bOutputBocksAsIs )
   {
      int iWaitBlocks = 0;
      u32 uECSpreadHigh = (pPHVF->uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_EC_SCHEME_SPREAD_FACTOR_HIGHBIT)?1:0;
      u32 uECSpreadLow = (pPHVF->uProfileEncodingFlags & VIDEO_PROFILE_ENCODING_FLAG_EC_SCHEME_SPREAD_FACTOR_LOWBIT)?1:0;
      iWaitBlocks = uECSpreadLow + (uECSpreadHigh*2);
      if ( m_iRXBlocksStackTopIndex > iWaitBlocks )
      {
         pushFirstBlockOut();
         nReturnCanTx = 1;
         return nReturnCanTx;
      }
   }

   return nReturnCanTx;
   */
 return 1;
}