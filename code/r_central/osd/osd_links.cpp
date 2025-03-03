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

#include "../../base/base.h"
#include "../../base/hardware.h"
#include "../../base/hardware_radio.h"
#include "../../base/config.h"
#include "../../base/video_capture_res.h"
#include "../../base/models.h"
#include "../../base/ctrl_interfaces.h"
#include "../../base/ctrl_settings.h"
#include "../../common/string_utils.h"

#include "../../renderer/render_engine.h"

#include "osd.h"
#include "osd_common.h"
#include "../colors.h"
#include "osd_links.h"
#include "../shared_vars.h"
#include "../timers.h"

float _osd_render_local_radio_link_tag_new(float xPos, float yPos, int iLocalRadioLinkId, bool bHorizontal, bool bDraw)
{
   char szBuff1[32];
   char szBuff2[32];
   char szBuff3[32];

   float height_text = osd_getFontHeight();
   float height_text_small = osd_getFontHeightSmall();

   int iVehicleRadioLinkId = g_SM_RadioStats.radio_links[iLocalRadioLinkId].matchingVehicleRadioLinkId;
   sprintf(szBuff1, "Link-%d", iVehicleRadioLinkId+1);

   strcpy(szBuff2, str_format_frequency_no_sufix(g_pCurrentModel->radioLinksParams.link_frequency_khz[iVehicleRadioLinkId]));
   
   szBuff3[0] = 0;
   if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
   {
      strcpy(szBuff3, "RELAY");
      strcpy(szBuff2, str_format_frequency_no_sufix(g_pCurrentModel->relay_params.uRelayFrequencyKhz));
   }
   float fWidthLink = g_pRenderEngine->textWidth(g_idFontOSDSmall, szBuff1);
   float fWidthFreq = g_pRenderEngine->textWidth(g_idFontOSD, szBuff2);
   float fWidthRelay = g_pRenderEngine->textWidth(g_idFontOSDSmall, szBuff3);

   if ( fWidthFreq > fWidthLink )
      fWidthLink = fWidthFreq;
   if ( fWidthRelay > fWidthLink )
      fWidthLink = fWidthRelay;

   fWidthLink += 0.5*height_text_small;
   float fHeight = 1.1 * height_text_small + height_text;
   
   if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
      fHeight += 1.0*height_text_small;

   if ( bDraw )
   {
      g_pRenderEngine->drawBackgroundBoundingBoxes(false);

      double pC[4];
      memcpy(pC, get_Color_OSDBackground(), 4*sizeof(double));
      g_pRenderEngine->setFill(pC[0], pC[1], pC[2], 0.4);
      g_pRenderEngine->setStroke(0,0,0,0);
      g_pRenderEngine->drawRoundRect(xPos+1.0*g_pRenderEngine->getPixelWidth(), yPos + 1.0*g_pRenderEngine->getPixelHeight(), fWidthLink, fHeight, 0.003*osd_getScaleOSD());

      osd_set_colors();

      yPos += 0.07 * height_text_small;
      float fWidth = g_pRenderEngine->textWidth(g_idFontOSD, szBuff2);
      g_pRenderEngine->drawText(xPos+(fWidthLink-fWidth)*0.5, yPos, g_idFontOSD, szBuff2);

      yPos += height_text*0.9;
      fWidth = g_pRenderEngine->textWidth(g_idFontOSDSmall, szBuff1);
      g_pRenderEngine->drawText(xPos+(fWidthLink-fWidth)*0.5, yPos,  g_idFontOSDSmall, szBuff1);

      yPos += height_text_small*0.9;

      // Third line
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
      {
         fWidth = g_pRenderEngine->textWidth(g_idFontOSDSmall, "DIS");
         g_pRenderEngine->drawText(xPos+(fWidthLink-fWidth)*0.5, yPos, g_idFontOSDSmall, "DIS");
      }
      else if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
      {
         fWidth = g_pRenderEngine->textWidth(g_idFontOSDSmall, szBuff3);
         g_pRenderEngine->drawText(xPos+(fWidthLink-fWidth)*0.5, yPos, g_idFontOSDSmall, szBuff3);
      }

      if ( g_pCurrentModel->osd_params.osd_flags2[g_pCurrentModel->osd_params.iCurrentOSDLayout] & OSD_FLAG2_SHOW_BACKGROUND_ON_TEXTS_ONLY )
         g_pRenderEngine->drawBackgroundBoundingBoxes(true);
   }

   if ( bHorizontal )
      return fWidthLink;
   else
      return fHeight;
}


float _osd_get_link_bars_width(float fScale)
{
   float height_text = osd_getFontHeight();
   float height_text_small = osd_getFontHeightSmall();
   float iconHeight = height_text*0.92 + height_text_small;
   float iconWidth = 1.4*iconHeight/g_pRenderEngine->getAspectRatio();
   return iconWidth*fScale;
}

float _osd_get_link_bars_height(float fScale)
{
   float height_text = osd_getFontHeight();
   float height_text_small = osd_getFontHeightSmall();
   float iconHeight = height_text*0.92 + height_text_small - 4.0*g_pRenderEngine->getPixelHeight();
   return iconHeight*fScale;
}

float _osd_show_link_bars(float xPos, float yPos, u32 uLastRxTime, float fQuality, float fScale)
{
   float iconHeight = _osd_get_link_bars_height(fScale);
   float iconWidth = 1.5*iconHeight/g_pRenderEngine->getAspectRatio();
   
   bool bShowRed = false;

   if ( (uLastRxTime != 0) && (uLastRxTime < g_TimeNow-2000) )
      bShowRed = true;
   if ( fQuality < OSD_QUALITY_LEVEL_CRITICAL/100.0 )
      bShowRed = true;

   osd_set_colors();
   for( int i=0; i<4; i++ )
   {
      float x = xPos - i*iconWidth/4.0;
      float h = iconHeight;
      h -= (iconHeight/4.0)*i;

      double pc[4];
      memcpy(pc, get_Color_OSDText(), 4*sizeof(double));

      if ( bShowRed )
         memcpy(pc, get_Color_IconError(), 4*sizeof(double));
      else if ( fQuality < 0.001 )
         memcpy(pc, get_Color_OSDText(), 4*sizeof(double));
      else if ( fQuality < OSD_QUALITY_LEVEL_WARNING/100.0 )
         memcpy(pc, get_Color_IconWarning(), 4*sizeof(double));
      g_pRenderEngine->setColors(pc);
      g_pRenderEngine->setStroke(0,0,0,0.5);
      g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);

      if ( bShowRed )
      {
      }
      else if ( fQuality < 0.001 )
      {
         g_pRenderEngine->setFill(0,0,0,0.5);
         g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], 0.9);
      }
      else if ( fQuality <= 1.05 - 0.25*(i+1) )
      {
         g_pRenderEngine->setFill(0,0,0,0.1);
         g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], 0.5);
      }

      g_pRenderEngine->drawRoundRect(x-iconWidth/4.0, yPos+iconHeight-h, iconWidth/6, h, 0.003*osd_getScaleOSD()*fScale);
   }

   osd_set_colors();
   if ( fQuality < -0.1 )
      g_pRenderEngine->drawTextLeft(xPos-iconWidth*0.8, yPos, g_idFontOSD, "x");
   return iconWidth;
}


float _osd_get_radio_link_new_height()
{
   float height_text_small = osd_getFontHeightSmall();

   float fHeightLink = _osd_get_link_bars_height(1.0);

   if ( g_pCurrentModel->osd_params.osd_flags[g_pCurrentModel->osd_params.iCurrentOSDLayout] & OSD_FLAG_SHOW_RADIO_INTERFACES_INFO )
      fHeightLink += height_text_small;

   if ( g_pCurrentModel->osd_params.osd_flags2[g_pCurrentModel->osd_params.iCurrentOSDLayout] & OSD_FLAG2_SHOW_RADIO_LINK_INTERFACES_EXTENDED )
      fHeightLink += height_text_small;
   return fHeightLink;
}


float _osd_show_radio_bars_info(float xPos, float yPos, u32 uLastRxTime, int iMaxRxQuality, int dbm, int iSNR, int iDataRateBps, bool bShowBars, bool bShowNumbers, bool bUplink, bool bHorizontal, bool bDraw, bool bDatarateChanged)
{
   if ( ! bShowNumbers )
   if ( ! bShowBars )
      return 0.0;

   float height_text_small = osd_getFontHeightSmall();
   float fTotalWidth = 0.0;
   float fTotalHeight = _osd_get_link_bars_height(1.0);
   float fMaxRxQuality = ((float)iMaxRxQuality)/100.0;
   static u32 s_uTimeStartFlashLinkBars = 0;
   bool bShowRed = false;

   if ( (uLastRxTime != 0) && (uLastRxTime < g_TimeNow-2000) )
   {
      bShowRed = true;
      if ( s_uTimeStartFlashLinkBars < uLastRxTime )
         s_uTimeStartFlashLinkBars = g_TimeNow;
   }
   if ( fMaxRxQuality < OSD_QUALITY_LEVEL_CRITICAL/100.0 )
      bShowRed = true;

   if ( (bShowRed) && (0 != s_uTimeStartFlashLinkBars) )
   {
      if ( g_TimeNow < s_uTimeStartFlashLinkBars + 2000 )
      if ( (g_TimeNow/150) % 2 )
         bDraw = false;
   }

   if ( bShowRed )
   {
      g_pRenderEngine->setColors(get_Color_IconError());
      g_pRenderEngine->setStroke(0,0,0,0.5);
      g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
      osd_set_colors_text(get_Color_IconError());
   }
   else
      osd_set_colors();

   if ( bShowBars )
   {
       float fSize = _osd_get_link_bars_width(1.0);
       if ( bDraw )
          _osd_show_link_bars(xPos+fSize,yPos, uLastRxTime, fMaxRxQuality, 1.0);
       xPos += fSize;
       fTotalWidth += fSize;
   }

   float fIconHeight = fTotalHeight;
   float fIconWidth = 0.5*fIconHeight/g_pRenderEngine->getAspectRatio();

   if ( bDraw )
   {
      if ( bUplink )
         g_pRenderEngine->drawIcon(xPos-3.0*g_pRenderEngine->getPixelWidth(),yPos, fIconWidth, fIconHeight, g_idIconArrowUp);
      else
         g_pRenderEngine->drawIcon(xPos-3.0*g_pRenderEngine->getPixelWidth(),yPos, fIconWidth, fIconHeight, g_idIconArrowDown);
   }

   xPos += fIconWidth*1.1;
   fTotalWidth += fIconWidth*1.1;

   if ( bShowNumbers )
   {
      char szLine1[64];
      char szLine2[64];

      //sprintf(szLine1, "%d%%", iMaxRxQuality);
      if ( dbm < -120 )
         strcpy(szLine1, "--- dBm");
      else
         sprintf(szLine1, "%d dBm", dbm);
      
      if ( iSNR <= 0 )
         strcpy(szLine2, "SNR: ---");
      else
         sprintf(szLine2, "SNR: %d", iSNR);

      float fWidthLine1 = g_pRenderEngine->textWidth(g_idFontOSDSmall, "-110 dBm");
      float fWidthLine2 = g_pRenderEngine->textWidth(g_idFontOSDSmall, "SNR: -100");
      if ( fWidthLine2 > fWidthLine1 )
         fWidthLine1 = fWidthLine2;

      if ( bDraw )
      {
         g_pRenderEngine->drawText(xPos + g_pRenderEngine->getPixelWidth()*3.0, yPos, g_idFontOSDSmall, szLine1);
         g_pRenderEngine->drawText(xPos + g_pRenderEngine->getPixelWidth()*3.0, yPos+height_text_small, g_idFontOSDSmall, szLine2);
      }
      fTotalWidth += fWidthLine1 + g_pRenderEngine->getPixelWidth()*3.0;
   }
   if ( bHorizontal )
      return fTotalWidth;
   else
      return fTotalHeight;
}


float _osd_show_local_radio_link_new(float xPos, float yPos, int iLocalRadioLinkId, bool bHorizontal, bool bRender, float* pfTotalWidth, float* pfTotalHeight)
{
   float fMarginY = 0.5*osd_getSpacingV();
   float fMarginX = 0.5*osd_getSpacingH();

   if ( ! bHorizontal )
      xPos += 4.0*g_pRenderEngine->getPixelWidth();
   float xStart = xPos;
   float yStart = yPos;

   float height_text = osd_getFontHeight();
   float height_text_small = osd_getFontHeightSmall();
   float height_text_extra_small = g_pRenderEngine->textHeight(g_idFontOSDExtraSmall);

   float fWidthTag = _osd_render_local_radio_link_tag_new(xPos,yPos, iLocalRadioLinkId, true, false);
   float fHeightTag = _osd_render_local_radio_link_tag_new(xPos,yPos, iLocalRadioLinkId, false, false);

   float fTotalWidthLink = 0.0;
   float fTotalHeightLink = fHeightTag;
   float fHeightLink = _osd_get_radio_link_new_height();

   if ( ! bHorizontal )
      fTotalHeightLink = 0.0;

   if ( bHorizontal )
   if ( fHeightLink > fTotalHeightLink )
      fTotalHeightLink = fHeightLink;

   ControllerSettings* pCS = get_ControllerSettings();

   int iVehicleRadioLinkId = g_SM_RadioStats.radio_links[iLocalRadioLinkId].matchingVehicleRadioLinkId;
   
   int iRuntimeInfoToUse = 0; // Main vehicle
   Model* pModelToUse = g_pCurrentModel;
   // int iRuntimeInfoToUse = osd_get_current_data_source_vehicle_index;
   //Model* pModelToUse = osd_get_current_data_source_vehicle_model();
   
   if ( NULL == pModelToUse )
      return 0.0;

   int iIndexSMRouterVehicleRuntimeInfo = -1;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( g_SM_RouterVehiclesRuntimeInfo.uVehiclesIds[i] == pModelToUse->uVehicleId )
      {
         iIndexSMRouterVehicleRuntimeInfo = i;
         break;
      }
   }

   bool bShowVehicleSide = false;
   bool bShowControllerSide = false;
   bool bShowBars = false;
   bool bShowNumbers = false;

   bool bShowSummary = true;
   bool bShowInterfaces = false;
   bool bShowInterfacesExtended = false;

   if ( pModelToUse->osd_params.osd_flags[pModelToUse->osd_params.iCurrentOSDLayout] & OSD_FLAG_SHOW_RADIO_LINKS )
      bShowControllerSide = true;

   if ( pModelToUse->osd_params.osd_flags[pModelToUse->osd_params.iCurrentOSDLayout] & OSD_FLAG_SHOW_VEHICLE_RADIO_LINKS )
      bShowVehicleSide = true;

   if ( pModelToUse->osd_params.osd_flags2[pModelToUse->osd_params.iCurrentOSDLayout] & OSD_FLAG2_SHOW_RADIO_LINK_QUALITY_BARS )
      bShowBars = true;

   if ( pModelToUse->osd_params.osd_flags2[pModelToUse->osd_params.iCurrentOSDLayout] & OSD_FLAG2_SHOW_RADIO_LINK_QUALITY_NUMBERS )
      bShowNumbers = true;

   if ( pModelToUse->osd_params.osd_flags[pModelToUse->osd_params.iCurrentOSDLayout] & OSD_FLAG_SHOW_RADIO_INTERFACES_INFO )
   {
      bShowInterfaces = true;
      bShowSummary = false;
   }

   if ( pModelToUse->osd_params.osd_flags2[pModelToUse->osd_params.iCurrentOSDLayout] & OSD_FLAG2_SHOW_RADIO_LINK_INTERFACES_EXTENDED )
   {
      bShowInterfaces = true;
      bShowInterfacesExtended = true;
      bShowSummary = false;    
   }

   if ( pModelToUse->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
   {
      bShowVehicleSide = true;
      bShowControllerSide = false;
   }

   if ( bHorizontal )
   {
      xPos -= fMarginX;
      yPos += fMarginY;
   }
   else
   {
      xPos += fMarginX;
      yPos += fMarginY;
   }

   if ( ! bHorizontal )
   {
      _osd_render_local_radio_link_tag_new(xStart,yStart, iLocalRadioLinkId, bHorizontal, bRender);
      fTotalHeightLink += fHeightTag;
      yPos += fHeightTag;
   }

   float dySignalBars = 0.0;
   if ( (! bShowInterfaces) && (! bShowInterfacesExtended) )
       dySignalBars = height_text_small*0.3;
   else
      dySignalBars = height_text_small*0.1;

   // Show vehicle side

   if ( bShowVehicleSide )
   {
      int nRxQuality = 0;
      int iDataRateUplinkBPS = 0;
      u32 uLastRxTime = 0;
      int dbm = -200;
      int iSNR = -200;
      
      for( int i=0; i<pModelToUse->radioInterfacesParams.interfaces_count; i++ )
      {
         if ( pModelToUse->radioInterfacesParams.interface_link_id[i] != iVehicleRadioLinkId )
            continue;
         if ( g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_link_quality[i] > nRxQuality )
            nRxQuality = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_link_quality[i];
         
         iDataRateUplinkBPS = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.last_recv_datarate_bps[i];
         if ( pModelToUse->radioLinkIsSiKRadio(iVehicleRadioLinkId) )
            iDataRateUplinkBPS = pModelToUse->radioLinksParams.link_datarate_data_bps[iVehicleRadioLinkId];

         if ( ! pModelToUse->radioLinkIsSiKRadio(iVehicleRadioLinkId) )
         if ( (g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_rssi_dbm[i]-200) > dbm )
         {
            dbm = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_rssi_dbm[i]-200;
            if ( g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_noise_dbm[i] > 0 )
               iSNR = dbm + g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_noise_dbm[i];
         }
      }

      int iVehicleRadioCard = -1;
      for( int i=0; i<pModelToUse->radioInterfacesParams.interfaces_count; i++ )
      {
         if ( pModelToUse->radioInterfacesParams.interface_link_id[i] == iVehicleRadioLinkId )
         {
            iVehicleRadioCard = i;
            break;
         }
      }
      if ( iVehicleRadioCard != -1 )
          uLastRxTime = g_TimeNow - (g_VehiclesRuntimeInfo[iRuntimeInfoToUse].SMVehicleRxStats[iVehicleRadioLinkId].timeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].SMVehicleRxStats[iVehicleRadioLinkId].timeLastRxPacket);
      
      if ( -1 != iIndexSMRouterVehicleRuntimeInfo )
         uLastRxTime = g_SM_RouterVehiclesRuntimeInfo.uLastTimeReceivedAckFromVehicle[iIndexSMRouterVehicleRuntimeInfo];

      if ( bShowSummary )
      {
         bool bAsUplink = true;
         if ( pModelToUse->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
            bAsUplink = false;
         float fSize = _osd_show_radio_bars_info(xPos, yPos+dySignalBars, uLastRxTime, nRxQuality, dbm, iSNR, iDataRateUplinkBPS, bShowBars, bShowNumbers, bAsUplink, bHorizontal, false, false);
         if ( bHorizontal )
         {
            xPos -= fSize;
            fTotalWidthLink += fSize;
         }
         else if ( fSize > fTotalWidthLink )
            fTotalWidthLink = fSize;

         if ( bRender )
            _osd_show_radio_bars_info(xPos, yPos+dySignalBars, uLastRxTime, nRxQuality, dbm, iSNR, iDataRateUplinkBPS, bShowBars, bShowNumbers, bAsUplink, bHorizontal, true, false);
         if ( ! bHorizontal )
         {
             yPos += fSize;
             fTotalHeightLink += fSize;
         }
      }
      else // Vehicle side, not summary
      {
         static int s_iLastOSDVehRadioLinkInterfacesRecvDatarates[MAX_RADIO_INTERFACES][MAX_RADIO_INTERFACES];
         static u32 s_uLastOSDVehRadioLinkInterfacesRecvDataratesChangeTimes[MAX_RADIO_INTERFACES][MAX_RADIO_INTERFACES];
         static bool s_bLastOSDVehRadioLinKInterfacesRecvDataratesInit = true;
         if ( s_bLastOSDVehRadioLinKInterfacesRecvDataratesInit )
         {
            s_bLastOSDVehRadioLinKInterfacesRecvDataratesInit = false;
            for( int k=0; k<MAX_RADIO_INTERFACES; k++ )
            for( int j=0; j<MAX_RADIO_INTERFACES; j++ )
            {
               s_iLastOSDVehRadioLinkInterfacesRecvDatarates[k][j] = 0;
               s_uLastOSDVehRadioLinkInterfacesRecvDataratesChangeTimes[k][j] = 0;
            }
         }
            
         int iCountShown = 0;
         for( int i=0; i<pModelToUse->radioInterfacesParams.interfaces_count; i++ )
         {
            if ( pModelToUse->radioInterfacesParams.interface_link_id[i] != iVehicleRadioLinkId )
               continue;
            nRxQuality = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_link_quality[i];
            uLastRxTime = g_TimeNow - (g_VehiclesRuntimeInfo[iRuntimeInfoToUse].SMVehicleRxStats[i].timeNow - g_VehiclesRuntimeInfo[iRuntimeInfoToUse].SMVehicleRxStats[i].timeLastRxPacket);
            if ( -1 != iIndexSMRouterVehicleRuntimeInfo )
            if ( g_SM_RouterVehiclesRuntimeInfo.iIsVehicleLinkToControllerLostAlarm[iIndexSMRouterVehicleRuntimeInfo] )
               uLastRxTime = g_SM_RouterVehiclesRuntimeInfo.uLastTimeReceivedAckFromVehicle[iIndexSMRouterVehicleRuntimeInfo];
            if ( -1 != iIndexSMRouterVehicleRuntimeInfo )
            if ( g_SM_RouterVehiclesRuntimeInfo.uLastTimeReceivedAckFromVehicle[iIndexSMRouterVehicleRuntimeInfo] < g_TimeNow-2000 )
               uLastRxTime = g_SM_RouterVehiclesRuntimeInfo.uLastTimeReceivedAckFromVehicle[iIndexSMRouterVehicleRuntimeInfo];

            iDataRateUplinkBPS = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.last_recv_datarate_bps[i];
            dbm = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_rssi_dbm[i]-200;

            if ( pModelToUse->radioLinkIsSiKRadio(iVehicleRadioLinkId) )
            {
               iDataRateUplinkBPS = g_pCurrentModel->radioLinksParams.link_datarate_data_bps[iVehicleRadioLinkId];
               dbm = -200;
            }

            if ( iDataRateUplinkBPS != s_iLastOSDVehRadioLinkInterfacesRecvDatarates[iVehicleRadioLinkId][i] )
            {
               s_iLastOSDVehRadioLinkInterfacesRecvDatarates[iVehicleRadioLinkId][i] = iDataRateUplinkBPS;
               s_uLastOSDVehRadioLinkInterfacesRecvDataratesChangeTimes[iVehicleRadioLinkId][i] = g_TimeNow;
            }

            if ( iCountShown > 0 )
            {
               if ( bHorizontal )
               {
                  xPos -= osd_getSpacingH()*0.5;
                  fTotalWidthLink += osd_getSpacingH()*0.5;
               }
               else
               {
                  yPos += osd_getSpacingV()*0.5;
                  fTotalHeightLink += osd_getSpacingV()*0.5;
               }
            }

            bool bAsUplink = true;
            if ( pModelToUse->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
               bAsUplink = false;
         
            bool bDRChanged = false;
            if ( g_bOSDElementChangeNotification )
            if ( (pModelToUse->osd_params.osd_flags3[pModelToUse->osd_params.iCurrentOSDLayout] & OSD_FLAG3_HIGHLIGHT_CHANGING_ELEMENTS) ||
                 pCS->iDeveloperMode )
            if ( g_TimeNow < s_uLastOSDVehRadioLinkInterfacesRecvDataratesChangeTimes[iVehicleRadioLinkId][i] + g_uOSDElementChangeTimeout )
               bDRChanged = true;

            float fSize = _osd_show_radio_bars_info(xPos, yPos+dySignalBars, uLastRxTime, nRxQuality, dbm, iSNR, iDataRateUplinkBPS, bShowBars, bShowNumbers, bAsUplink, bHorizontal, false, false);
            if ( bHorizontal )
            {
               xPos -= fSize;
               fTotalWidthLink += fSize;
            }
            else if ( fSize > fTotalWidthLink )
               fTotalWidthLink = fSize;

            if ( bRender )
               _osd_show_radio_bars_info(xPos, yPos+dySignalBars, uLastRxTime, nRxQuality, dbm, iSNR, iDataRateUplinkBPS, bShowBars, bShowNumbers, bAsUplink, bHorizontal, true, false);
            if ( ! bHorizontal )
            {
                yPos += fSize;
                fTotalHeightLink += fSize;
            }

            char szLine1[128];
            char szBuffDR[32];
            bool bShowLine1AsError = false;
            sprintf(szLine1, "%s: RX/TX ", pModelToUse->radioInterfacesParams.interface_szPort[i] );
            
            str_format_bitrate(iDataRateUplinkBPS, szBuffDR);
            if ( strlen(szBuffDR) > 4 )
            if ( szBuffDR[strlen(szBuffDR)-2] == 'p' )
               szBuffDR[strlen(szBuffDR)-2] = 0;
            
            strcat(szLine1, szBuffDR);

            if ( bShowLine1AsError )
               g_pRenderEngine->setColors(get_Color_IconError());

            if ( bDRChanged && g_bOSDElementChangeNotification )
            if ( (pModelToUse->osd_params.osd_flags3[pModelToUse->osd_params.iCurrentOSDLayout] & OSD_FLAG3_HIGHLIGHT_CHANGING_ELEMENTS)
                 || pCS->iDeveloperMode )
               osd_set_colors_text(get_Color_OSDElementChanged());

            float fWidthLine1 = g_pRenderEngine->textWidth(g_idFontOSDExtraSmall, szLine1);
            int iFontLine1 = g_idFontOSDSmall;
            float dyLine1 = 0.0;
            if ( fWidthLine1 > fSize*0.8 )
            {
               iFontLine1 = g_idFontOSDExtraSmall;
               dyLine1 = (height_text_small - height_text_extra_small)*0.6;
            }
            fWidthLine1 = g_pRenderEngine->textWidth(iFontLine1, szLine1);
            
            if ( (! bDRChanged) || (g_TimeNow >= s_uLastOSDVehRadioLinkInterfacesRecvDataratesChangeTimes[iVehicleRadioLinkId][i] + g_uOSDElementChangeTimeout/2) ||
                 (bDRChanged && ((g_TimeNow/g_uOSDElementChangeBlinkInterval)%2)))
            {
               if ( bHorizontal )
               {
                  if ( bRender )
                     g_pRenderEngine->drawText(xPos + (fSize - fWidthLine1)*0.5, yPos + dyLine1 + dySignalBars + _osd_get_link_bars_height(1.0), iFontLine1, szLine1);
               }
               else
               {
                  if ( bRender )
                     g_pRenderEngine->drawText(xStart + (osd_getVerticalBarWidth() - fWidthLine1)*0.5, yPos + dyLine1 + dySignalBars, iFontLine1, szLine1);
                  yPos += height_text_small;
                  fTotalHeightLink += height_text_small;             
               }
            }
            if ( bShowLine1AsError || bDRChanged )
               osd_set_colors();

            iCountShown++;
         }
      }
   }

   // Separator between controller and vehicle side

   if ( bShowVehicleSide && bShowControllerSide )
   {
       if ( bHorizontal )
       {
          xPos -= osd_getSpacingH()*0.5;
          fTotalWidthLink += osd_getSpacingH()*0.5;
       }
       else
       {
          yPos += osd_getSpacingV()*0.6;
          fTotalHeightLink += osd_getSpacingV()*0.6;
       }
   }

   // Controller side

   if ( bShowControllerSide )
   {
      int iCountInterfacesForCurrentLink = 0;
      if ( iLocalRadioLinkId >= 0 )
      {
         for( int k=0; k<g_SM_RadioStats.countLocalRadioInterfaces; k++ )
            if ( g_SM_RadioStats.radio_interfaces[k].assignedLocalRadioLinkId == iLocalRadioLinkId )
               iCountInterfacesForCurrentLink++;
      }

      int nRxQuality = 0;
      int iRecvDataRateVideo = 0;
      int iRecvDataRateData = 0;
      u32 uLastRxTime = 0;
      int dbm = -200;
      int iSNR = -200;
      for( int i=0; i<g_SM_RadioStats.countLocalRadioInterfaces; i++ )
      {
         if ( g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId != iLocalRadioLinkId )
            continue;
         if ( g_SM_RadioStats.radio_interfaces[i].rxQuality > nRxQuality )
            nRxQuality = g_SM_RadioStats.radio_interfaces[i].rxQuality;
      
         if ( g_SM_RadioStats.radio_interfaces[i].lastRecvDataRateVideo > iRecvDataRateVideo )
           iRecvDataRateVideo = g_SM_RadioStats.radio_interfaces[i].lastRecvDataRateVideo;
         if ( g_SM_RadioStats.radio_interfaces[i].lastRecvDataRateData > iRecvDataRateData )
           iRecvDataRateData = g_SM_RadioStats.radio_interfaces[i].lastRecvDataRateData;

         if ( g_SM_RadioStats.radio_interfaces[i].timeLastRxPacket > uLastRxTime )
            uLastRxTime = g_SM_RadioStats.radio_interfaces[i].timeLastRxPacket;

         if ( g_fOSDDbm[i] > dbm )
            dbm = g_fOSDDbm[i];
         if ( g_fOSDSNR[i] > iSNR )
            iSNR = g_fOSDSNR[i];
      }

      if ( 0 == iRecvDataRateVideo )
         iRecvDataRateVideo = iRecvDataRateData;

      if ( pModelToUse->radioLinkIsSiKRadio(iVehicleRadioLinkId) )
      {
         iRecvDataRateVideo = pModelToUse->radioLinksParams.link_datarate_data_bps[iVehicleRadioLinkId];
         iRecvDataRateData = iRecvDataRateVideo;
      }
      
      if ( bShowSummary )
      {
         float fSize = _osd_show_radio_bars_info(xPos, yPos+dySignalBars, uLastRxTime, nRxQuality, dbm, iSNR, iRecvDataRateVideo, bShowBars, bShowNumbers, false, bHorizontal, false, false);
         if ( bHorizontal )
         {
            xPos -= fSize;
            fTotalWidthLink += fSize;
         }
         else if ( fSize > fTotalWidthLink )
            fTotalWidthLink = fSize;

         if ( bRender )
            _osd_show_radio_bars_info(xPos, yPos+dySignalBars, uLastRxTime, nRxQuality, dbm, iSNR, iRecvDataRateVideo, bShowBars, bShowNumbers, false, bHorizontal, true, true);
         if ( ! bHorizontal )
         {
             yPos += fSize;
             fTotalHeightLink += fSize;
         }
      }
      else // Controller side, not summary
      {
         static int s_iLastOSDRadioLinkInterfacesRecvDatarates[MAX_RADIO_INTERFACES][MAX_RADIO_INTERFACES];
         static u32 s_uLastOSDRadioLinkInterfacesRecvDataratesChangeTimes[MAX_RADIO_INTERFACES][MAX_RADIO_INTERFACES];
         static bool s_bLastOSDRadioLinKInterfacesRecvDataratesInit = true;
         if ( s_bLastOSDRadioLinKInterfacesRecvDataratesInit )
         {
            s_bLastOSDRadioLinKInterfacesRecvDataratesInit = false;
            for( int k=0; k<MAX_RADIO_INTERFACES; k++ )
            for( int j=0; j<MAX_RADIO_INTERFACES; j++ )
            {
               s_iLastOSDRadioLinkInterfacesRecvDatarates[k][j] = 0;
               s_uLastOSDRadioLinkInterfacesRecvDataratesChangeTimes[k][j] = 0;
            }
         }

         int iCountShown = 0;
         for( int i=0; i<g_SM_RadioStats.countLocalRadioInterfaces; i++ )
         {
            if ( g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId != iLocalRadioLinkId )
               continue;
            nRxQuality = g_SM_RadioStats.radio_interfaces[i].rxQuality;
            uLastRxTime = g_SM_RadioStats.radio_interfaces[i].timeLastRxPacket;
            iRecvDataRateVideo = g_SM_RadioStats.radio_interfaces[i].lastRecvDataRateVideo;
            if ( 0 == iRecvDataRateVideo )
               iRecvDataRateVideo = g_SM_RadioStats.radio_interfaces[i].lastRecvDataRateData;
              
            if ( g_pCurrentModel->radioLinkIsSiKRadio(iVehicleRadioLinkId) )
               iRecvDataRateVideo = g_pCurrentModel->radioLinksParams.link_datarate_data_bps[iVehicleRadioLinkId];

            if ( iRecvDataRateVideo != s_iLastOSDRadioLinkInterfacesRecvDatarates[iLocalRadioLinkId][i] )
            {
               s_iLastOSDRadioLinkInterfacesRecvDatarates[iLocalRadioLinkId][i] = iRecvDataRateVideo;
               s_uLastOSDRadioLinkInterfacesRecvDataratesChangeTimes[iLocalRadioLinkId][i] = g_TimeNow;
            }

            bool bDRChanged = false;
            if ( g_bOSDElementChangeNotification )
            if ( (pModelToUse->osd_params.osd_flags3[pModelToUse->osd_params.iCurrentOSDLayout] & OSD_FLAG3_HIGHLIGHT_CHANGING_ELEMENTS) ||
                 pCS->iDeveloperMode )
            if ( g_TimeNow < s_uLastOSDRadioLinkInterfacesRecvDataratesChangeTimes[iLocalRadioLinkId][i] + g_uOSDElementChangeTimeout )
               bDRChanged = true;

            bool bIsTxCard = false;
            if ( (iLocalRadioLinkId >= 0) && (g_SM_RadioStats.radio_links[iLocalRadioLinkId].lastTxInterfaceIndex == i) && (1 < g_SM_RadioStats.countLocalRadioInterfaces) )
               bIsTxCard = true;
      
            if ( iCountShown > 0 )
            {
               if ( bHorizontal )
               {
                  xPos -= osd_getSpacingH()*0.5;
                  fTotalWidthLink += osd_getSpacingH()*0.5;
               }
               else
               {
                  yPos += osd_getSpacingV()*0.5;
                  fTotalHeightLink += osd_getSpacingV()*0.5;
               }
            }
            float fSize = _osd_show_radio_bars_info(xPos, yPos+dySignalBars, uLastRxTime, nRxQuality, g_fOSDDbm[i], g_fOSDSNR[i], iRecvDataRateVideo, bShowBars, bShowNumbers, false, bHorizontal, false, false);
            if ( bHorizontal )
            {
               xPos -= fSize;
               fTotalWidthLink += fSize;
            }
            else if ( fSize > fTotalWidthLink )
               fTotalWidthLink = fSize;

            if ( bRender )
               _osd_show_radio_bars_info(xPos, yPos+dySignalBars, uLastRxTime, nRxQuality, g_fOSDDbm[i], g_fOSDSNR[i], iRecvDataRateVideo, bShowBars, bShowNumbers, false, bHorizontal, true, true);
            
            if ( bIsTxCard && (1<iCountInterfacesForCurrentLink) )
            if ( bRender )
               g_pRenderEngine->drawIcon(xPos-height_text*0.1, yPos+dySignalBars, height_text*0.9/g_pRenderEngine->getAspectRatio() , height_text*0.9, g_idIconUplink2);

            if ( ! bHorizontal )
            {
                yPos += fSize;
                fTotalHeightLink += fSize;
            }

            char szLine1[128];
            bool bShowLine1AsError = false;
            radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);

            if ( NULL != pRadioHWInfo )
            {
               if ( g_SM_RadioStats.radio_links[iLocalRadioLinkId].lastTxInterfaceIndex == i )
                  sprintf(szLine1, "%s: RX/TX ", pRadioHWInfo->szUSBPort );
               else if ( g_SM_RadioStats.radio_interfaces[i].openedForRead )
                  sprintf(szLine1, "%s: RX ", pRadioHWInfo->szUSBPort );
               else if ( g_SM_RadioStats.radio_interfaces[i].openedForWrite )
                  sprintf(szLine1, "%s: -- ", pRadioHWInfo->szUSBPort );
               else
                  sprintf(szLine1, "%s: N/A ", pRadioHWInfo->szUSBPort );
            }
            else
               strcpy(szLine1, "N/A ");

            char szBuffDR[32];
            str_format_bitrate(iRecvDataRateVideo, szBuffDR);
            if ( strlen(szBuffDR) > 4 )
            if ( szBuffDR[strlen(szBuffDR)-2] == 'p' )
               szBuffDR[strlen(szBuffDR)-2] = 0;

            strcat(szLine1, szBuffDR);
            
            if ( bShowLine1AsError )
               g_pRenderEngine->setColors(get_Color_IconError());

            if ( bDRChanged && g_bOSDElementChangeNotification )
            if ( (pModelToUse->osd_params.osd_flags3[pModelToUse->osd_params.iCurrentOSDLayout] & OSD_FLAG3_HIGHLIGHT_CHANGING_ELEMENTS) ||
                pCS->iDeveloperMode )
               osd_set_colors_text(get_Color_OSDElementChanged());

            float fWidthLine1 = g_pRenderEngine->textWidth(g_idFontOSDExtraSmall, szLine1);
            int iFontLine1 = g_idFontOSDSmall;
            float dyLine1 = 0.0;
            if ( fWidthLine1 > fSize*0.8 )
            {
               iFontLine1 = g_idFontOSDExtraSmall;
               dyLine1 = (height_text_small - height_text_extra_small)*0.6;
            }
            fWidthLine1 = g_pRenderEngine->textWidth(iFontLine1, szLine1);
            
            if ( (! bDRChanged) || (g_TimeNow >= s_uLastOSDRadioLinkInterfacesRecvDataratesChangeTimes[iLocalRadioLinkId][i] + g_uOSDElementChangeTimeout/2) ||
                 (bDRChanged && ((g_TimeNow/g_uOSDElementChangeBlinkInterval)%2)))
            {
               if ( bHorizontal )
               {
                  if ( bRender )
                     g_pRenderEngine->drawText(xPos, yPos + dyLine1 + dySignalBars + _osd_get_link_bars_height(1.0), iFontLine1, szLine1);
               }
               else
               {
                  if ( bRender )
                     g_pRenderEngine->drawText(xStart + (osd_getVerticalBarWidth() - fWidthLine1)*0.5, yPos + dyLine1 + dySignalBars, iFontLine1, szLine1);
                  yPos += height_text_small;
                  fTotalHeightLink += height_text_small;             
               }
            }
            if ( bShowLine1AsError || bDRChanged )
               osd_set_colors();

            if ( bShowInterfacesExtended )
            {
               char szCardName[128];
               controllerGetCardUserDefinedNameOrType(pRadioHWInfo, szCardName);
          
               if ( NULL != pRadioHWInfo )
                  snprintf(szLine1, sizeof(szLine1)/sizeof(szLine1[0]), "%s%s", (controllerIsCardInternal(pRadioHWInfo->szMAC)?"":"(Ext) "), szCardName );
               else
                  strcpy(szLine1, szCardName);
                 
               float fWidthLine1 = g_pRenderEngine->textWidth(g_idFontOSDExtraSmall, szLine1);
               if ( bHorizontal )
               {
                  if ( bRender )
                     g_pRenderEngine->drawText(xPos, yPos + dySignalBars + _osd_get_link_bars_height(1.0) + height_text_small, g_idFontOSDExtraSmall, szLine1);
               }
               else
               {
                  if ( bRender )
                     g_pRenderEngine->drawText(xStart + (osd_getVerticalBarWidth() - fWidthLine1)*0.5, yPos + dySignalBars, g_idFontOSDExtraSmall, szLine1);
                  yPos += height_text_extra_small;
                  fTotalHeightLink += height_text_extra_small;             
               }
            }
            iCountShown++;
         }
      }
   }

   if ( bHorizontal )
   {
      xPos -= fWidthTag;
      fTotalWidthLink += fWidthTag;
      if ( bRender )
         _osd_render_local_radio_link_tag_new(xStart-fTotalWidthLink-2.0*fMarginX,yStart, iLocalRadioLinkId, bHorizontal, true);
   }

   if ( NULL != pfTotalWidth )
      *pfTotalWidth = fTotalWidthLink;
   if ( NULL != pfTotalHeight )
      *pfTotalHeight = fTotalHeightLink;

   if ( bHorizontal )
      return fTotalWidthLink + 2.0 * fMarginX;
   else
      return fTotalHeightLink + 2.0 * fMarginY;
}

float osd_show_local_radio_link_new(float xPos, float yPos, int iLocalRadioLinkId, bool bHorizontal)
{
   float fTotalWidthLink = 0.0;
   float fTotalHeightLink = 0;
   float fMarginY = 0.5*osd_getSpacingV();
   float fMarginX = 0.5*osd_getSpacingH();

   _osd_show_local_radio_link_new(xPos, yPos, iLocalRadioLinkId, bHorizontal, false, &fTotalWidthLink, &fTotalHeightLink);

   bool bRectBlending = g_pRenderEngine->isRectBlendingEnabled();

   double pC[4];
   memcpy(pC, get_Color_OSDText(), 4*sizeof(double));
   g_pRenderEngine->setFill(0,0,0,0.1);
   g_pRenderEngine->setStroke(pC[0], pC[1], pC[2], 0.7);
   g_pRenderEngine->setStrokeSize(2.0);

   if ( bHorizontal )
      g_pRenderEngine->drawRoundRect(xPos-fTotalWidthLink-2.0*fMarginX, yPos, fTotalWidthLink + 2.0*fMarginX, fTotalHeightLink + 2.0*fMarginY, 0.003*osd_getScaleOSD());
   else
      g_pRenderEngine->drawRoundRect(xPos, yPos, osd_getVerticalBarWidth() - 8.0*g_pRenderEngine->getPixelWidth(), fTotalHeightLink + 2.0*fMarginY, 0.003*osd_getScaleOSD());

   if ( bRectBlending )
      g_pRenderEngine->enableRectBlending();
   else
      g_pRenderEngine->disableRectBlending();

   float fRet = _osd_show_local_radio_link_new(xPos, yPos, iLocalRadioLinkId, bHorizontal, true, NULL, NULL);
   osd_set_colors();
   return fRet;
}