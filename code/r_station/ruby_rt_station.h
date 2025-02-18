#pragma once

void broadcast_router_ready();
void send_message_to_central(u32 uPacketType, u32 uParam, bool bTelemetryToo);
void send_alarm_to_central(u32 uAlarm, u32 uFlags1, u32 uFlags2);
bool links_set_cards_frequencies_and_params(int iVehicleLinkId);
bool links_set_cards_frequencies_for_search( u32 iSearchFreq, bool bSiKSearch, int iAirDataRate, int iECC, int iLBT, int iMCSTR );

void reasign_radio_links(bool bSilent);

void video_processors_init();
void video_processors_cleanup();
