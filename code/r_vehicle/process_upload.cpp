/*
    Ruby Licence
    Copyright (c) 2025 Petru Soroaga petrusoroaga@yahoo.com
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

#include "../base/base.h"
#include "../base/config.h"
#include "../base/commands.h"
#include "../base/hardware.h"
#include "../base/hardware_files.h"
#include "../base/hardware_radio.h"
#include "../base/hw_procs.h"
#include "../base/ruby_ipc.h"

#include <pthread.h>
#include "launchers_vehicle.h"
#include "process_upload.h"
#include "ruby_rx_commands.h"
#include "video_source_csi.h"
#include "shared_vars.h"
#include "timers.h"


extern int s_fIPCToRouter;


FILE* s_pFileSoftware = NULL;

u32 s_uLastTimeReceivedAnySoftwareBlock = 0;
u32 s_uLastReceivedSoftwareBlockIndex = 0xFFFFFFFF;
u32 s_uLastReceivedSoftwareTotalSize = 0;
u32 s_uCurrentReceivedSoftwareSize = 0;
bool s_bSoftwareUpdateStoppedVideoPipeline = false;
bool s_bUpdateAppliedRebooting = false;

char s_szUpdateArchiveFile[MAX_FILE_PATH_SIZE];

u8** s_pSWPackets = NULL;
u8*  s_pSWPacketsReceived = NULL;
u32* s_pSWPacketsSize = NULL;
u32 s_uSWPacketsCount = 0;
u32 s_uSWPacketsMaxSize = 0;

pthread_t s_pThreadProcessUpload;
bool s_bUpdateInProgress = false;
bool s_bProcessUploadInProgress = false;
pthread_t s_pThreadProcessArchive;
bool s_bThreadProcessArchiveFinished = true;
char s_szProcessUploadArchiveCommand[256];

void _sw_update_close_remove_temp_files()
{
   if ( NULL != s_pFileSoftware )
       fclose(s_pFileSoftware);
   s_pFileSoftware = NULL;

   if ( 0 != s_szUpdateArchiveFile[0] )
   {
      char szComm[512];
      sprintf(szComm, "rm -rf %s", s_szUpdateArchiveFile);
      hw_execute_bash_command_silent(szComm, NULL);
      s_szUpdateArchiveFile[0] = 0;
   }

   s_uLastReceivedSoftwareBlockIndex = 0xFFFFFFFF;
   s_uLastReceivedSoftwareTotalSize = 0;
   s_uCurrentReceivedSoftwareSize = 0;

   if ( NULL != s_pSWPackets )
   {
      for( u32 i=0; i<s_uSWPacketsCount; i++ )
         free ((u8*)s_pSWPackets[i]);
      free ((u8*)s_pSWPackets);
   }

   if ( NULL != s_pSWPacketsReceived )
      free((u8*)s_pSWPacketsReceived);

   if ( NULL != s_pSWPacketsSize )
      free((u8*)s_pSWPacketsSize);

   s_pSWPackets = NULL;
   s_pSWPacketsReceived = NULL;
   s_pSWPacketsSize = NULL;
   s_uSWPacketsCount = 0;
   s_uSWPacketsMaxSize = 0;

   char szComm[256];
   sprintf(szComm, "rm -rf %s%s", FOLDER_RUBY_TEMP, FILE_TEMP_UPDATE_IN_PROGRESS);
   hw_execute_bash_command(szComm, NULL);

   sprintf(szComm, "rm -rf %s%s", FOLDER_RUBY_TEMP, FILE_TEMP_UPDATE_IN_PROGRESS_APPLY);
   hw_execute_bash_command_silent(szComm, NULL);

   s_bProcessUploadInProgress = false;
   if ( s_bSoftwareUpdateStoppedVideoPipeline )
   if ( ! s_bUpdateAppliedRebooting )
   {
      sendControlMessage(PACKET_TYPE_LOCAL_CONTROL_RESUME_VIDEO, 0);
      s_bSoftwareUpdateStoppedVideoPipeline = false;
   }
}

void process_sw_upload_init()
{
   s_pFileSoftware = NULL;
   s_szUpdateArchiveFile[0] = 0;
   s_bSoftwareUpdateStoppedVideoPipeline = false;

   s_pSWPackets = NULL;
   s_pSWPacketsReceived = NULL;
   s_pSWPacketsSize = NULL;
   s_uSWPacketsCount = 0;
   s_uSWPacketsMaxSize = 0;
}

void _process_upload_send_status_to_controller(u8 uStatus, int iRepeatCount)
{
   static u32 uStatusCounterProcessUpload = 0;
   uStatusCounterProcessUpload++;

   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_RUBY, PACKET_TYPE_OTA_UPDATE_STATUS, STREAM_ID_DATA);
   PH.vehicle_id_src = g_pCurrentModel->uVehicleId;
   PH.vehicle_id_dest = g_pCurrentModel->uControllerId;
   PH.total_length = sizeof(t_packet_header)+sizeof(u8)+sizeof(u32);
   
   u8 buffer[MAX_PACKET_TOTAL_SIZE];
   memcpy(buffer, (u8*)&PH, sizeof(t_packet_header));
   memcpy(buffer+sizeof(t_packet_header), (u8*)&uStatus, sizeof(u8));
   memcpy(buffer+sizeof(t_packet_header) + sizeof(u8), (u8*)&uStatusCounterProcessUpload, sizeof(u32));
   radio_packet_compute_crc(buffer, PH.total_length);

   for( int i=0; i<iRepeatCount; i++ )
   {
      ruby_ipc_channel_send_message(s_fIPCToRouter, buffer, PH.total_length);
      if ( NULL != g_pProcessStats )
         g_pProcessStats->lastActiveTime = g_TimeNow;
      hardware_sleep_ms(50);
   }
   log_line("ProcessUpload: Send OTA status %d (counter %u) to controller CID: %u", uStatus, uStatusCounterProcessUpload, g_pCurrentModel->uControllerId);
}

static void * _thread_process_archive(void *argument)
{
   s_bThreadProcessArchiveFinished = false;
   log_line("[ProcessUploadThArch] Started archive thread...");
   hw_execute_bash_command_raw(s_szProcessUploadArchiveCommand, NULL);
   log_line("[ProcessUploadThArch] Finished archive thread.");
   s_bThreadProcessArchiveFinished = true;
   return NULL;
}

static void * _thread_process_upload(void *argument)
{
   log_line("[ProcessUploadTh] Started update thread...");
   s_bProcessUploadInProgress = true;
   
   char szFile[MAX_FILE_PATH_SIZE];
   char szComm[512];

   sprintf(szComm, "touch %s%s", FOLDER_RUBY_TEMP, FILE_TEMP_UPDATE_IN_PROGRESS_APPLY);
   hw_execute_bash_command(szComm, NULL);

   strcpy(szFile, FOLDER_CONFIG);
   strcat(szFile, FILE_CONFIG_CONTROLLER_ID);
   u32 uControllerId = 0;
   FILE* fd = fopen(szFile, "r");
   if ( NULL != fd )
   {
      fscanf(fd, "%u", &uControllerId);
      fclose(fd);
   }

   log_line("[ProcessUpload] Controller ID: from file: %u, from model: %u", uControllerId, g_pCurrentModel->uControllerId);
   if ( 0 == g_pCurrentModel->uControllerId )
      g_pCurrentModel->uControllerId = uControllerId;

   _process_upload_send_status_to_controller(OTA_UPDATE_STATUS_START_PROCESSING, 5);
   
   #if defined(HW_PLATFORM_RASPBERRY)
   log_line("Save received update archive for backup...");
   sprintf(szComm, "rm -rf %slast_update_received.tar 2>&1", FOLDER_UPDATES);
   hw_execute_bash_command(szComm, NULL);
   sprintf(szComm, "cp -rf %s %slast_update_received.tar", s_szUpdateArchiveFile, FOLDER_UPDATES);
   hw_execute_bash_command(szComm, NULL);
   sprintf(szComm, "chmod 777 %slast_update_received.tar 2>&1", FOLDER_UPDATES);
   hw_execute_bash_command(szComm, NULL);
   #endif

   vehicle_stop_rx_rc();

   log_line("Binaries versions before update:");
   char szOutput[2048];
   hw_execute_ruby_process_wait(NULL, "ruby_start", "-ver", szOutput, 1);
   log_line("ruby_start: [%s]", szOutput);
   hw_execute_ruby_process_wait(NULL, "ruby_rt_vehicle", "-ver", szOutput, 1);
   log_line("ruby_rt_vehicle: [%s]", szOutput);
   hw_execute_ruby_process_wait(NULL, "ruby_tx_telemetry", "-ver", szOutput, 1);
   log_line("ruby_tx_telemetry: [%s]", szOutput);
   
   sprintf(szComm, "chmod 777 %s", FOLDER_BINARIES);
   hw_execute_bash_command(szComm, NULL);
   sprintf(szComm, "chmod 777 %s*", FOLDER_BINARIES);
   hw_execute_bash_command(szComm, NULL);

   #ifdef HW_PLATFORM_RASPBERRY
   log_line("Running on Raspberry hardware");
   sprintf(szComm, "nice -n 19 ionice -c 3 tar -C %s -zxf %s 2>&1 1>/dev/null", FOLDER_BINARIES, s_szUpdateArchiveFile);
   #endif
   #ifdef HW_PLATFORM_OPENIPC_CAMERA
   log_line("Running on OpenIPC hardware");
   sprintf(szComm, "tar -C %s -xf %s 2>&1 1>/dev/null", FOLDER_BINARIES, s_szUpdateArchiveFile);
   #endif
   
   hardware_sleep_ms(500);
   _process_upload_send_status_to_controller(OTA_UPDATE_STATUS_UNPACK, 10);

   s_bThreadProcessArchiveFinished = false;
   strcpy(s_szProcessUploadArchiveCommand, szComm);
   if ( 0 != pthread_create(&s_pThreadProcessArchive, NULL, &_thread_process_archive, NULL) )
   {
      s_bThreadProcessArchiveFinished = true;
      log_softerror_and_alarm("[ProcessUploadTh] Failed to create thread archive processing.");
      log_line("Extracting binaries to location: %s", FOLDER_BINARIES);   
      hw_execute_bash_command_raw(szComm, NULL);
      //system(szComm);
      log_line("Done extracting to location: %s", FOLDER_BINARIES);
      log_line("Done extracting archive.");
   }
   else
   {
      while ( ! s_bThreadProcessArchiveFinished )
      {
         hardware_sleep_ms(200);
         _process_upload_send_status_to_controller(OTA_UPDATE_STATUS_UNPACK, 2);
      }
      log_line("[ProcessUploadTh] Thread to process archive finished.");
   }

   _process_upload_send_status_to_controller(OTA_UPDATE_STATUS_UPDATING, 40);

   sprintf(szComm, "chmod 777 %sruby*", FOLDER_BINARIES);
   hw_execute_bash_command(szComm, NULL);
   hardware_sleep_ms(50);

   log_line("Binaries versions after update:");
   hw_execute_ruby_process_wait(NULL, "ruby_start", "-ver", szOutput, 1);
   log_line("ruby_start: [%s]", szOutput);
   hw_execute_ruby_process_wait(NULL, "ruby_rt_vehicle", "-ver", szOutput, 1);
   log_line("ruby_rt_vehicle: [%s]", szOutput);
   hw_execute_ruby_process_wait(NULL, "ruby_tx_telemetry", "-ver", szOutput, 1);
   log_line("ruby_tx_telemetry: [%s]", szOutput);
   hw_execute_ruby_process_wait(NULL, "ruby_update", "-ver", szOutput, 1);
   log_line("ruby_update: [%s]", szOutput);
   hw_execute_ruby_process_wait(NULL, "ruby_update_vehicle", "-ver", szOutput, 1);
   log_line("ruby_update_vehicle: [%s]", szOutput);

   #ifdef HW_PLATFORM_RASPBERRY
   if ( access( "ruby_capture_raspi", R_OK ) != -1 )
      hw_execute_bash_command("cp -rf ruby_capture_raspi /opt/vc/bin/raspivid", NULL);

   strcpy(szFile, FOLDER_BINARIES);
   strcat(szFile, "ruby_config.txt");
   if ( access( szFile, R_OK ) != -1 )
   {
      hardware_mount_boot();
      hardware_sleep_ms(200);
      hw_execute_bash_command("mv ruby_config.txt /boot/config.txt", NULL);
   }
   #endif

   #ifdef HW_PLATFORM_OPENIPC_CAMERA
   strcpy(szFile, FOLDER_BINARIES);
   strcat(szFile, "majestic");
   if ( access(szFile, R_OK) != -1 )
   {
      sprintf(szComm, "mv -f %smajestic /usr/bin/majestic", FOLDER_BINARIES);
      hw_execute_bash_command(szComm, NULL);
      hw_execute_bash_command("chmod 777 /usr/bin/majestic", NULL);
   }
   #endif

   //sprintf(szComm, "ls -al %sruby_update*", FOLDER_BINARIES);
   //hw_execute_bash_command_raw(szComm, szOutput);
   //log_line("Update files: [%s]", szOutput);

   _process_upload_send_status_to_controller(OTA_UPDATE_STATUS_POST_UPDATING, 10);

   strcpy(szFile, FOLDER_BINARIES);
   strcat(szFile, "ruby_update_vehicle");

   if ( access( szFile, R_OK ) != -1 )
      log_line("ruby_update_vehicle is present.");
   else
   {
      log_line("ruby_update_vehicle is NOT present.");
      sprintf(szComm, "cp -rf %sruby_update %sruby_update_vehicle", FOLDER_BINARIES, FOLDER_BINARIES);
      hw_execute_bash_command_raw(szComm, NULL);
      sprintf(szComm, "chmod 777 %sruby_update*", FOLDER_BINARIES);
      hw_execute_bash_command_raw(szComm, NULL);
      hardware_sleep_ms(100);

      strcpy(szFile, FOLDER_BINARIES);
      strcat(szFile, "ruby_update_vehicle");
      if ( access( szFile, R_OK ) != -1 )
         log_line("ruby_update_vehicle is present now.");
      else
         log_line("ruby_update_vehicle is NOT present yet.");
   }

   strcpy(szFile, FOLDER_BINARIES);
   strcat(szFile, "ruby_update");
   if ( access( szFile, R_OK ) != -1 )
      log_line("ruby_update is present.");
   else
      log_line("ruby_update is NOT present.");
     
   strcpy(szFile, FOLDER_BINARIES);
   strcat(szFile, "ruby_update_worker");
   if ( access( szFile, R_OK ) != -1 )
      log_line("ruby_update_worker is present.");
   else
      log_line("ruby_update_worker is NOT present.");
     
   strcpy(szFile, FOLDER_BINARIES);
   strcat(szFile, "ruby_update_vehicle");
   if ( access( szFile, R_OK ) != -1 )
      hw_execute_ruby_process_wait(NULL, "ruby_update_vehicle", "-pre", NULL, 1);

   // Copy log file to last update
   #if defined(HW_PLATFORM_OPENIPC_CAMERA)
   hw_execute_bash_command("cp -rf /tmp/logs/log_system.txt /root/ruby/last_update_log.txt", NULL);
   #endif
   #if defined(HW_PLATFORM_RASPBERRY)
   hw_execute_bash_command("cp -rf /home/pi/ruby/logs/log_system.txt /home/pi/ruby/logs/last_update_log.txt", NULL);
   #endif
   log_line("Done updating. Cleaning up and reboot");
   s_bUpdateAppliedRebooting = true;

   sprintf(szComm, "rm -rf %s%s", FOLDER_RUBY_TEMP, FILE_TEMP_UPDATE_IN_PROGRESS_APPLY);
   hw_execute_bash_command_silent(szComm, NULL);

   log_line("Give time for power leds to signal end of update...");

   _process_upload_send_status_to_controller(OTA_UPDATE_STATUS_COMPLETED, 50);
   for( int i=0; i<10; i++ )
      hardware_sleep_ms(100);

   log_line("Cleanup and reboot");
   
   _sw_update_close_remove_temp_files();

   _process_upload_send_status_to_controller(OTA_UPDATE_STATUS_COMPLETED, 50);


   // Begin Check and update drivers

   /*
   #ifdef HW_PLATFORM_OPENIPC_CAMERA
   char szDriver[MAX_FILE_PATH_SIZE];
   strcpy(szDriver, FOLDER_BINARIES);
   strcat(szDriver, "drivers/8812eu-oipc.ko");
   
   if ( access(szDriver, R_OK) != -1 )
   {
      sprintf(szComm, "mv -f %s /lib/modules/$(uname -r)/extra/", szDriver);
      hw_execute_bash_command(szComm, NULL);
      hw_execute_bash_command("modprobe cfg80211", NULL);
      hw_execute_bash_command("rmmod 8812eu 2>&1 1>/dev/null", NULL);
      hw_execute_bash_command("insmod /lib/modules/$(uname -r)/extra/8812eu-oipc.ko rtw_tx_pwr_by_rate=0 rtw_tx_pwr_lmt_enable=0", NULL);
   }
   else
      log_line("No new RTL8812EU driver in file [%s]", szDriver);

   strcpy(szDriver, FOLDER_BINARIES);
   strcat(szDriver, "drivers/88XXau-oipc.ko");
   
   if ( access(szDriver, R_OK) != -1 )
   {
      sprintf(szComm, "mv -f %s /lib/modules/$(uname -r)/extra/", szDriver);
      hw_execute_bash_command(szComm, NULL);
      hw_execute_bash_command("modprobe cfg80211", NULL);
      hw_execute_bash_command("rmmod 88XXau 2>&1 1>/dev/null", NULL);
      hw_execute_bash_command("insmod /lib/modules/$(uname -r)/extra/88XXau-oipc.ko rtw_tx_pwr_idx_override=1", NULL);
   }
   else
      log_line("No new RTL8812AU driver in file [%s]", szDriver);
   #endif
   */
   
   // Drivers are installed after reboot, by the presence of ruby_update_vehicle
   //hardware_install_drivers(0);
   // End check and update drivers

   s_bProcessUploadInProgress = false;
   signalReboot();
   return NULL;
}


void process_sw_upload_new(u32 command_param, u8* pBuffer, int length)
{
   if ( (NULL == pBuffer) || (length < (int)(sizeof(t_packet_header) + sizeof(t_packet_header_command) + sizeof(command_packet_sw_package))) )
   {
      log_softerror_and_alarm("Received SW Upload packet of invalid minimum size: %d bytes", length);
      _sw_update_close_remove_temp_files();
      sendCommandReply(COMMAND_RESPONSE_FLAGS_FAILED, 0, 0);
      return;             
   }

   command_packet_sw_package* params = (command_packet_sw_package*)(pBuffer + sizeof(t_packet_header)+sizeof(t_packet_header_command));
   char szComm[256];

   if ( NULL != g_pProcessStats )
      g_pProcessStats->lastActiveTime = g_TimeNow;
   s_uLastTimeReceivedAnySoftwareBlock = g_TimeNow;
   
   bool bSendAck = (bool) command_param;

   log_line("Recv sw pkg seg %d, is last:%d, block size: %d bytes, this block size: %d bytes, total size: %d bytes",
      params->file_block_index, params->is_last_block,
      params->block_length,
      length-sizeof(t_packet_header)-sizeof(t_packet_header_command)-sizeof(command_packet_sw_package),
      params->total_size);

   // Check for cancel
   if ( (params->file_block_index == MAX_U32) || (0 == params->total_size) )
   {
      log_line("Upload canceled");
      sendCommandReply(COMMAND_RESPONSE_FLAGS_OK, 0, 0);
      _sw_update_close_remove_temp_files();
      return;
   }

   if ( (params->total_size <= 0) || (params->block_length <= 0) || (params->total_size > 50000000) )
   {
      log_softerror_and_alarm("Received SW Upload packet of invalid size: %d bytes, total length: %d bytes.", params->block_length, params->total_size);
      _sw_update_close_remove_temp_files();
      sendCommandReply(COMMAND_RESPONSE_FLAGS_FAILED, 0, 0);
      return;             
   }

   if ( ! s_bSoftwareUpdateStoppedVideoPipeline )
   {
      sprintf(szComm, "touch %s%s", FOLDER_RUBY_TEMP, FILE_TEMP_UPDATE_IN_PROGRESS);
      hw_execute_bash_command(szComm, NULL);
      s_bSoftwareUpdateStoppedVideoPipeline = true;
      sendControlMessage(PACKET_TYPE_LOCAL_CONTROL_PAUSE_VIDEO, 0);

      sprintf(szComm, "rm -rf %slog_system_*", FOLDER_LOGS);
      hw_execute_bash_command(szComm, NULL);
      sprintf(szComm, "rm -rf %slog_errors_*", FOLDER_LOGS);
      hw_execute_bash_command(szComm, NULL);
      sprintf(szComm, "rm -rf %slog_video_*", FOLDER_LOGS);
      hw_execute_bash_command(szComm, NULL);
      int iFreeSpaceKb = hardware_get_free_space_kb();
      log_line("Free space on disk: %d Mb", iFreeSpaceKb/1000);
   }

   if ( NULL == s_pSWPackets )
   {
      s_uSWPacketsCount = (params->total_size/params->block_length);
      if ( params->total_size > s_uSWPacketsCount * params->block_length )
         s_uSWPacketsCount++;
      s_uSWPacketsMaxSize = params->block_length + 50;
      s_pSWPackets = (u8**) malloc(s_uSWPacketsCount*sizeof(u8*));
      s_pSWPacketsReceived = (u8*) malloc(s_uSWPacketsCount*sizeof(u8));
      s_pSWPacketsSize = (u32*) malloc(s_uSWPacketsCount*sizeof(u32));

      for( u32 i=0; i<s_uSWPacketsCount; i++ )
      {
         u8* pPacket = (u8*)malloc(s_uSWPacketsMaxSize);
         s_pSWPackets[i] = pPacket;
         s_pSWPacketsReceived[i] = 0;
         s_pSWPacketsSize[i] = 0;
      }
      log_line("SW Upload: allocated buffers for %u packets, max packet size: %u", s_uSWPacketsCount, s_uSWPacketsMaxSize);

      if ( params->type == 0 )
      {
         sprintf(s_szUpdateArchiveFile, "%s%s", FOLDER_UPDATES, "ruby_update.zip");
         log_line("Receiving update zip file, to save it in (%s)", s_szUpdateArchiveFile);
      }
      else
      {
         sprintf(s_szUpdateArchiveFile, "%s%s", FOLDER_UPDATES, "ruby_update.tar");
         log_line("Receiving update tar file, to save it in (%s)", s_szUpdateArchiveFile);
      }
   }

   if ( (params->file_block_index < 0) || (params->file_block_index > s_uSWPacketsCount) )
   {
      log_softerror_and_alarm("Received SW Upload packet index %d out of bounds (%u)", params->file_block_index, s_uSWPacketsCount);
      _sw_update_close_remove_temp_files();
      sendCommandReply(COMMAND_RESPONSE_FLAGS_FAILED, 0, 0);
      return;
   }

   s_pSWPacketsSize[params->file_block_index] = length-sizeof(t_packet_header)-sizeof(t_packet_header_command)-sizeof(command_packet_sw_package);
   s_pSWPacketsReceived[params->file_block_index]++;
   if ( 1 == s_pSWPacketsReceived[params->file_block_index] )
      s_uCurrentReceivedSoftwareSize += s_pSWPacketsSize[params->file_block_index];

   if ( s_pSWPacketsSize[params->file_block_index] > s_uSWPacketsMaxSize )
   {
      log_softerror_and_alarm("Received SW Upload packet index %d too big (%u bytes, max allowed: %u)", params->file_block_index, s_pSWPacketsSize[params->file_block_index], s_uSWPacketsMaxSize);
      _sw_update_close_remove_temp_files();
      sendCommandReply(COMMAND_RESPONSE_FLAGS_FAILED, 0, 0);
      return;
   }

   u8* pPacket = s_pSWPackets[params->file_block_index];
   if ( 0 < s_pSWPacketsSize[params->file_block_index] )
      memcpy(pPacket, pBuffer+sizeof(t_packet_header)+sizeof(t_packet_header_command)+sizeof(command_packet_sw_package), s_pSWPacketsSize[params->file_block_index]);

   if ( ! bSendAck )
      return;

   int iIndexCheck = params->file_block_index;
   int iCount = DEFAULT_UPLOAD_PACKET_CONFIRMATION_FREQUENCY;

   bool bAllPrevOk = true;

   if ( ! params->is_last_block )
   {
      log_line("Checking previously received %d segments, starting from index %d down.", iCount, iIndexCheck);
      while ( iIndexCheck >= 0 && iCount >= 0 )
      {
         if ( 0 == s_pSWPacketsReceived[iIndexCheck] )
         {
            log_line("Update segment %d is missing.", iIndexCheck);
            bAllPrevOk = false;
            break;
         }
         iIndexCheck--;
         iCount--;
      }
   }
   else
   {
      for( u32 i=0; i<s_uSWPacketsCount; i++ )
         if ( 0 == s_pSWPacketsReceived[i] )
            bAllPrevOk = false;
   }

   int nRepeat = s_pSWPacketsReceived[params->file_block_index];
   if ( nRepeat < 2 )
      nRepeat = 2;
   if ( nRepeat > 2 )
      nRepeat = 2;

   if ( params->is_last_block )
      nRepeat = 10;

   if ( bAllPrevOk )
   {
      for( int i=0; i<nRepeat; i++ )
         sendCommandReply(COMMAND_RESPONSE_FLAGS_OK, 0, 2);
   }
   else
   {
      for( int i=0; i<nRepeat; i++ )
         sendCommandReply(COMMAND_RESPONSE_FLAGS_FAILED, 0, 2);
   }
   if ( ! bAllPrevOk )
   {
      log_softerror_and_alarm("Received invalid SW packages for current segments slice. Do nothing. Wait for retransmission.");
      return;
   }
   if ( ! params->is_last_block )
      return;

   // Received all the sw update packets;

   log_line("Received entire SW upload.");

   if ( s_bUpdateInProgress )
   {
      log_line("Update is in progress, ignore sw package packets.");
      return;
   }

   s_bUpdateInProgress = true;


   sprintf(szComm, "mkdir -p %s", FOLDER_UPDATES);
   hw_execute_bash_command(szComm, NULL);
   sprintf(szComm, "chmod 777 %s", FOLDER_UPDATES);
   hw_execute_bash_command(szComm, NULL);
   s_pFileSoftware = fopen(s_szUpdateArchiveFile, "wb");
   if ( NULL == s_pFileSoftware )
   {
      log_softerror_and_alarm("Failed to create file for the downloaded software package. (file (%s))", s_szUpdateArchiveFile);
      log_softerror_and_alarm("The download did not completed correctly. Expected size: %d, received size: %d", params->total_size, s_uCurrentReceivedSoftwareSize );
      _sw_update_close_remove_temp_files();
      s_bUpdateInProgress = false;
      _process_upload_send_status_to_controller(OTA_UPDATE_STATUS_FAILED_DISK_SPACE, 10);
      return;
   }

   u32 fileSize = 0;
   for( u32 i=0; i<s_uSWPacketsCount; i++ )
   {
      if ( s_pSWPacketsSize[i] > 0 )
      if ( s_pSWPacketsSize[i] != (u32)fwrite(s_pSWPackets[i], 1, s_pSWPacketsSize[i], s_pFileSoftware) )
      {
         log_softerror_and_alarm("Failed to write to file for the downloaded software package.");
         _sw_update_close_remove_temp_files();
         s_bUpdateInProgress = false;
         _process_upload_send_status_to_controller(OTA_UPDATE_STATUS_FAILED_DISK_SPACE, 10);
         return;
      }
      fileSize += s_pSWPacketsSize[i];
   }
   fclose(s_pFileSoftware);
   s_pFileSoftware = NULL;

   log_enable_full();
   log_line("Write successfully to SW archive file [%s], total segments: %u, total size: %u bytes", s_szUpdateArchiveFile, s_uSWPacketsCount, fileSize);
   if ( fileSize != params->total_size )
      log_softerror_and_alarm("Missmatch between expected file size (%u) and created file size (%u)!", params->total_size, fileSize);

   sync();

   log_line("Received software package correctly (6.3 method). Update file: [%s]. Applying it.", s_szUpdateArchiveFile);

   if ( 0 != pthread_create(&s_pThreadProcessUpload, NULL, &_thread_process_upload, NULL) )
   {
      log_softerror_and_alarm("Failed to create worker thread to process upload.");
      s_bUpdateInProgress = false;
      s_bProcessUploadInProgress = false;
      _process_upload_send_status_to_controller(OTA_UPDATE_STATUS_FAILED, 10);
      return;
   }
}

bool process_sw_upload_is_started()
{
   return s_bSoftwareUpdateStoppedVideoPipeline;
}

void process_sw_upload_check_timeout(u32 uTimeNow)
{
   if ( (! s_bSoftwareUpdateStoppedVideoPipeline) || s_bProcessUploadInProgress )
      return;

   if ( uTimeNow > s_uLastTimeReceivedAnySoftwareBlock + 5000 )
   {
      log_line("Software upload timed out. No software packets received in last 5 seconds. Resume regular work.");
      s_uLastTimeReceivedAnySoftwareBlock = 0;
      _sw_update_close_remove_temp_files();
   }
}