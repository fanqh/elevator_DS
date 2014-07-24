#include "hal.h"
#include "hal_private.h"
#include "sppb.h"
#include "spp_dev_private.h"
#include "indication.h"
#include "spp_dev_b_leds.h"

static uint16 currentState = IND_NONE;

uint16 calcIndication(void);

uint16 calcIndication(void) {

	halTaskData* hal_task;
	sppb_task_t* sppb_task;

	
	hal_task = (halTaskData*)getHalTask();
	sppb_task = (sppb_task_t*)getSppbTask();

	switch(hal_task ->state)
	{
		case INITIALISING:

			if (hal_task ->voltage == 0xFFFF || hal_task ->charging_state == CHARGING_UNKNOWN) {

				return IND_UNKNOWN;
			}
			else {

				if (hal_task ->voltage > 3900) {

					return IND_BATT_FINE;
				}
				else if (hal_task ->voltage > 3600) {

					return IND_BATT_MODEST;
				}
				else {
					return IND_BATT_LOW;
				}
			}
			break;
			
		case ACTIVATING:

			return IND_NONE;
			break;
			
		case ACTIVE:

			switch(sppb_task ->state) 
			{
				case SPPB_STATE_NUM:
				case SPPB_INITIALISING:
				case SPPB_READY:
				case SPPB_DISCONNECTING:

					return IND_NONE;
					break;

				case SPPB_PAIRABLE:
				case SPPB_CONNECTING:

					return IND_INQUIRY_PAGE_ON;
					break;

				case SPPB_CONNECTED:

					switch(sppb_task ->conn_state) {

						case CONN_ECHO:

							return IND_ECHO;
							break;

						case CONN_PIPE:

							switch(hal_task ->charging_state) {

								case CHARGING_CHARGING:

									if (hal_task ->voltage > 3900) {
										return IND_PIPE_CHG_BATT_FINE;
									}
									else if (hal_task ->voltage > 3600) {

										return IND_PIPE_CHG_BATT_MODEST;
									}
									else {
										return IND_PIPE_CHG_BATT_LOW;
									}
									break;

								case CHARGING_NOT_CHARGING:

									if (hal_task ->voltage >3600) {
										return IND_PIPE_BATT_NOT_LOW;
									}
									else {
										return IND_PIPE_BATT_LOW;
									}
									break;
									
								case CHARGING_UNKNOWN:
									
									return currentState;
							}
							break;
					}

					break;
				}
			
			
			break;
		case DEACTIVATING:

			return IND_NONE;
			break;
	}

	/** should not go here **/
	return IND_NONE;
}

void update_indication(void) {
	
	uint16 state = calcIndication();

	if (state == currentState) {

		return;
	}
	
	currentState = state;
	
	ledsPlay(currentState);
}























