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
#include "menu.h"
#include "menu_objects.h"
#include "menu_text.h"
#include "menu_vehicle_dev.h"
#include "menu_item_section.h"
#include "menu_confirmation.h"
#include "menu_system_dev_stats.h"
#include "menu_system_video_profiles.h"
#include "../../radio/radiolink.h"
#include "../../base/utils.h"



MenuVehicleDev::MenuVehicleDev(void)
:Menu(MENU_ID_VEHICLE_DEV, "Developer Settings", NULL)
{
   m_Width = 0.36;
   m_xPos = menu_get_XStartPos(m_Width); m_yPos = 0.16;
   
   addItems();
}


void MenuVehicleDev::addItems()
{
   int iTmp = getSelectedMenuItemIndex();
   ControllerSettings* pCS = get_ControllerSettings();
   float fSliderWidth = 0.14 * m_sfScaleFactor;
   removeAllItems();

   m_IndexDevStats = addMenuItem( new MenuItem("Developer Stats Windows") );
   m_pMenuItems[m_IndexDevStats]->showArrow();

   m_IndexVideoProfiles = addMenuItem(new MenuItem("Video Link Profiles", "Change video link profiles and params."));
   m_pMenuItems[m_IndexVideoProfiles]->showArrow();

   addMenuItem(new MenuItemSection("Radio Links"));

   if ( NULL == g_pCurrentModel )
      return;

   char szTitle[128];
   if ( g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_USE_PCAP_RADIO_TX )
      strcpy(szTitle, "Vehicle Radio Tx Type (now PPCAP)");
   else
      strcpy(szTitle, "Vehicle Radio Tx Type (now socket)");
   m_pItemsSelect[9] = new MenuItemSelect(szTitle, "What method the vehicle uses for transmitting radio packets.");
   m_pItemsSelect[9]->addSelection("Sockets");
   m_pItemsSelect[9]->addSelection("PPCAP");
   m_pItemsSelect[9]->setIsEditable();
   m_pItemsSelect[9]->setSelectedIndex(0);
   if ( g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_USE_PCAP_RADIO_TX )
      m_pItemsSelect[9]->setSelectedIndex(1);
   m_IndexPCAPRadioTx = addMenuItem(m_pItemsSelect[9]);

   m_pItemsSelect[6] = new MenuItemSelect("Bypass kernel sockets buffers", "Skip kernel's qdisc (traffic control) layer (PACKET_QDISC_BYPASS).");
   m_pItemsSelect[6]->addSelection("No");
   m_pItemsSelect[6]->addSelection("Yes");
   m_pItemsSelect[6]->setIsEditable();
   m_pItemsSelect[6]->setSelectedIndex((g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags & MODEL_RADIOLINKS_FLAGS_BYPASS_SOCKETS_BUFFERS)?1:0);
   m_IndexBypassSocketBuffers = addMenuItem(m_pItemsSelect[6]);

   m_pItemsSelect[0] = new MenuItemSelect("RxTx Sync Type", "How the Rx/Tx time slots between vehicle and controller are synchronized.");
   m_pItemsSelect[0]->addSelection("None");
   m_pItemsSelect[0]->addSelection("Basic");
   m_pItemsSelect[0]->addSelection("Advanced");
   m_pItemsSelect[0]->setIsEditable();
   m_pItemsSelect[0]->setSelection( g_pCurrentModel->rxtx_sync_type );
   m_IndexClockSyncType = addMenuItem(m_pItemsSelect[0]);


   m_pItemsSelect[1] = new MenuItemSelect("Radio Silence Failsafe", "Restarts the vehicle if there is radio silence for more than 1 minute.");
   m_pItemsSelect[1]->addSelection("Disabled");
   m_pItemsSelect[1]->addSelection("Enabled");
   m_pItemsSelect[1]->setSelection( (g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_RADIO_SILENCE_FAILSAFE)?1:0 );
   m_IndexRadioSilence = addMenuItem(m_pItemsSelect[1]);

   m_pItemsSlider[9] = new MenuItemSlider("Radio Rx Loop Check Max Time (ms)", "The threshold for generating an alarm when radio Rx loop takes too much time (in miliseconds).", 1,1000,10, fSliderWidth);
   m_pItemsSlider[9]->setStep(1);
   m_pItemsSlider[9]->setCurrentValue(pCS->iDevRxLoopTimeout);
   m_IndexRxLoopTimeout = addMenuItem(m_pItemsSlider[9]);

   addMenuItem(new MenuItemSection("Debugging"));

   m_pItemsSelect[15] = new MenuItemSelect("Inject recoverable video tx faults", "Inject video tx faults: packets that can't be transmitted but could be retransmitted.");
   m_pItemsSelect[15]->addSelection("No");
   m_pItemsSelect[15]->addSelection("Yes");
   m_pItemsSelect[15]->setUseMultiViewLayout();
   m_pItemsSelect[15]->setSelectedIndex(0);
   if ( g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_INJECT_RECOVERABLE_VIDEO_FAULTS )
      m_pItemsSelect[15]->setSelectedIndex(1);
   m_IndexInjectMinorVideoFaults = addMenuItem(m_pItemsSelect[15]);

   m_pItemsSelect[12] = new MenuItemSelect("Inject unrecoverable video tx faults", "Inject video tx faults: packets that can't be transmitted and packets that can't be retransmitted too.");
   m_pItemsSelect[12]->addSelection("No");
   m_pItemsSelect[12]->addSelection("Yes");
   m_pItemsSelect[12]->setUseMultiViewLayout();
   m_pItemsSelect[12]->setSelectedIndex(0);
   if ( g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_INJECT_VIDEO_FAULTS )
      m_pItemsSelect[12]->setSelectedIndex(1);
   m_IndexInjectVideoFaults = addMenuItem(m_pItemsSelect[12]);


   m_IndexResetDev = addMenuItem(new MenuItem("Reset Developer Settings", "Resets all the developer settings to the factory default values."));

   for( int i=0; i<m_ItemsCount; i++ )
      m_pMenuItems[i]->setTextColor(get_Color_Dev());

   m_SelectedIndex = iTmp;
   if ( m_SelectedIndex >= m_ItemsCount )
      m_SelectedIndex = m_ItemsCount-1;
}


void MenuVehicleDev::valuesToUI()
{
   addItems();
}

void MenuVehicleDev::onShow()
{
   addItems();
   Menu::onShow();
}

void MenuVehicleDev::Render()
{
   RenderPrepare();
   float yTop = RenderFrameAndTitle();
   float y = yTop;
   for( int i=0; i<m_ItemsCount; i++ )
      y += RenderItem(i,y);

   RenderEnd(yTop);
}


void MenuVehicleDev::onReturnFromChild(int iChildMenuId, int returnValue)
{
   Menu::onReturnFromChild(iChildMenuId, returnValue);

   if ( (2 == iChildMenuId/1000) && (1 == returnValue) )
   {
      if ( ! handle_commands_send_to_vehicle(COMMAND_ID_RESET_ALL_DEVELOPER_FLAGS, 0, NULL, 0) )
      {
         valuesToUI();
         return;
      }

      //Menu* pm = new MenuConfirmation("Developer Settings Reset", "Vehicle will reboot now.", 3, true);
      //pm->m_yPos = 0.4;
      //add_menu_to_stack(pm);
      return;
   }
}


void MenuVehicleDev::onSelectItem()
{
   Menu::onSelectItem();
   if ( m_pMenuItems[m_SelectedIndex]->isEditing() )
      return;

   if ( NULL == g_pCurrentModel )
      return;

   if ( m_IndexDevStats == m_SelectedIndex )
   {
      add_menu_to_stack(new MenuSystemDevStats());
      return;
   }

   if ( m_IndexVideoProfiles == m_SelectedIndex )
   {
      add_menu_to_stack(new MenuSystemVideoProfiles());
      return;
   }
   
   if ( m_IndexPCAPRadioTx == m_SelectedIndex )
   {
      ControllerSettings* pCS = get_ControllerSettings();
      if ( 0 == m_pItemsSelect[9]->getSelectedIndex() )
         g_pCurrentModel->uDeveloperFlags &= (~DEVELOPER_FLAGS_USE_PCAP_RADIO_TX);
      else
         g_pCurrentModel->uDeveloperFlags |= DEVELOPER_FLAGS_USE_PCAP_RADIO_TX;
      if ( ! handle_commands_send_developer_flags(pCS->iDeveloperMode, g_pCurrentModel->uDeveloperFlags) )
         valuesToUI();
      return;
   }

   if ( m_IndexBypassSocketBuffers == m_SelectedIndex )
   {
      u32 uFlags = g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags;
      if ( 0 == m_pItemsSelect[6]->getSelectedIndex() )
         uFlags &= ~MODEL_RADIOLINKS_FLAGS_BYPASS_SOCKETS_BUFFERS;
      else
         uFlags |= MODEL_RADIOLINKS_FLAGS_BYPASS_SOCKETS_BUFFERS;
      if ( ! handle_commands_send_to_vehicle(COMMAND_ID_SET_RADIO_LINKS_FLAGS, uFlags, NULL, 0) )
         valuesToUI();
      return;
   }

   if ( m_IndexClockSyncType == m_SelectedIndex )
   {
      int rxtx = m_pItemsSelect[0]->getSelectedIndex();
      if ( rxtx != g_pCurrentModel->rxtx_sync_type )
      if ( ! handle_commands_send_to_vehicle(COMMAND_ID_SET_RXTX_SYNC_TYPE, rxtx, NULL, 0) )
         valuesToUI();
      return;
   }

   if ( m_IndexRadioSilence == m_SelectedIndex )
   {
      ControllerSettings* pCS = get_ControllerSettings();
      if ( 0 == m_pItemsSelect[1]->getSelectedIndex() )
         g_pCurrentModel->uDeveloperFlags &= (~DEVELOPER_FLAGS_BIT_RADIO_SILENCE_FAILSAFE);
      else
         g_pCurrentModel->uDeveloperFlags |= DEVELOPER_FLAGS_BIT_RADIO_SILENCE_FAILSAFE;
      if ( ! handle_commands_send_developer_flags(pCS->iDeveloperMode, g_pCurrentModel->uDeveloperFlags) )
         valuesToUI();
      return;
   }

   if ( m_IndexRxLoopTimeout == m_SelectedIndex )
   {
      ControllerSettings* pCS = get_ControllerSettings();
      pCS->iDevRxLoopTimeout = m_pItemsSlider[9]->getCurrentValue();
      save_ControllerSettings();
      valuesToUI();
      send_control_message_to_router(PACKET_TYPE_LOCAL_CONTROL_CONTROLLER_CHANGED, PACKET_COMPONENT_LOCAL_CONTROL);
      
      if ( ! handle_commands_send_developer_flags(pCS->iDeveloperMode, g_pCurrentModel->uDeveloperFlags) )
         valuesToUI(); 
      return;
   }

   if ( m_IndexInjectVideoFaults == m_SelectedIndex )
   {
      ControllerSettings* pCS = get_ControllerSettings();
      if ( 0 == m_pItemsSelect[12]->getSelectedIndex() )
         g_pCurrentModel->uDeveloperFlags &= (~DEVELOPER_FLAGS_BIT_INJECT_VIDEO_FAULTS);
      else
         g_pCurrentModel->uDeveloperFlags |= DEVELOPER_FLAGS_BIT_INJECT_VIDEO_FAULTS;
      if ( ! handle_commands_send_developer_flags(pCS->iDeveloperMode, g_pCurrentModel->uDeveloperFlags) )
         valuesToUI();  
   }

   if ( m_IndexInjectMinorVideoFaults == m_SelectedIndex )
   {
      ControllerSettings* pCS = get_ControllerSettings();
      if ( 0 == m_pItemsSelect[15]->getSelectedIndex() )
         g_pCurrentModel->uDeveloperFlags &= (~DEVELOPER_FLAGS_BIT_INJECT_RECOVERABLE_VIDEO_FAULTS);
      else
         g_pCurrentModel->uDeveloperFlags |= DEVELOPER_FLAGS_BIT_INJECT_RECOVERABLE_VIDEO_FAULTS;
      if ( ! handle_commands_send_developer_flags(pCS->iDeveloperMode, g_pCurrentModel->uDeveloperFlags) )
         valuesToUI();  
   }

   if ( m_IndexResetDev == m_SelectedIndex )
   {
      Menu* pm = new MenuConfirmation("Developer Settings Reset", "All developer settings where reset.", 2);
      pm->m_yPos = 0.4;
      add_menu_to_stack(pm);
   }

}

