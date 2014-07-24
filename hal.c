#include <csrtypes.h>
#include <battery.h>
#include <pio.h>
#include <panic.h>
#include<boot.h>
#include<ps.h>
#include "spp_dev_b_buttons.h"
#include "spp_dev_b_leds.h"
#include "hal.h"
#include "hal_config.h"
#include "hal_private.h"
/*#include "battery_probe.h"*/
#include "errman.h"
#include "debug.h"
#include "indication.h"

#define BEEP_ON_TIME	300
#define BEEP_OFF_TIME	100



#define BEEP_ONCE_DURATION 		(BEEP_ON_TIME + BEEP_OFF_TIME)
#define BEEP_TWICE_DURATION		((BEEP_ON_TIME + BEEP_OFF_TIME) * 2)
#define BEEP_THREE_TIME_DURATION	((BEEP_ON_TIME + BEEP_OFF_TIME) * 3)


/** task **/
halTaskData hal;
uint8 pio5hold;


/** hal task handler **/
static void hal_handler(Task task, MessageId id, Message message);

/** state event handlers **/
void initialising_handler(Task task, MessageId id, Message message);
void activating_handler(Task task, MessageId id, Message message);
void active_handler(Task task, MessageId id, Message message);
void deactivating_handler(Task task, MessageId id, Message message);

/** state entry/exit functions **/
void initialising_state_enter(void);
void initialising_state_exit(void);

void activating_state_enter(void);
void activating_state_exit(void);

void active_state_enter(void);
void active_state_exit(void);

void deactivating_state_enter(void);
void deactivating_state_exit(void);

/** fully initialized means charging state, voltage, and bluetooth status all initialized **/
/** bool isFullyInitialized(void); **/

/** turn on ldo to power myself **/
void enableLDO(void);

/** turn off ldo to unpower myself **/
void disableLDO(void);



void pio_raw_handler(Message message);
void battery_message_handler(Message message);

bool powerAllowedToTurnOn(void);
bool powerAllowedToContinue(void);

		
/** get hal task **/
Task getHalTask() {
	
	return &hal.task;
}

/** hal task handler **/
void hal_handler(Task task, MessageId id, Message message) {
	
	switch (hal.state) {
		
		case INITIALISING:
			initialising_handler(task, id, message);
			break;
			
		case ACTIVATING:
			activating_handler(task, id, message);
			break;
			
		case ACTIVE:
			active_handler(task, id, message);
			break;
			
		case DEACTIVATING:
			deactivating_handler(task, id, message);
			break;
	}
}

void initialising_state_enter(void) {
	
	DEBUG(("hal initialising state enter...\n"));
    

	
	/** init pio **/
	pioInit(&hal.pio_state, getHalTask());
	
	/** init battery lib **/
	/*BatteryInit(&hal.battery_state, getHalTask(), BATTERY_READING_SOURCE, BATTERY_POLLING_PERIOD);*/	
	BatteryInit(&hal.battery_state, getHalTask(), BATTERY_READING_SOURCE, 0);
	
	/** init battery probe **/
	/*battery_probe_start(getHalTask(), BATTERY_PROBE_READING_SOURCE, 200);*/

	disableLDO();
	
	update_indication();
}

void initialising_state_exit(void) {

	DEBUG(("hal initialising state exit...\n"));

	enableLDO();
	
	/*battery_probe_stop();*/
}

void initialising_handler(Task task, MessageId id, Message message) {
	
	switch (id) {
		
	case PIO_RAW:
			{
				DEBUG(("hal initialising state, PIO_RAW message arrived...\n"));
			
				/** update charging state, and no check, even battery low we have nothing to do **/
				pio_raw_handler(message);
				update_indication();
			}
			break;
	 case POWER_BUTTON_PRESS:
			
			DEBUG(("hal active state, POWER_BUTTON_PRESS message arrived...\n"));
      /*             
            PioSetDir(1<<5, 0);
            pio5hold = (PioGet()>>5)&0x1;
      */    
			MessageSendLater(getHalTask(), HAL_POWER_BUTTON_HELD_SHORT, 0, 2000);
        /*    if(1==pio5hold)*/
			    MessageSendLater(getHalTask(), HAL_POWER_BUTTON_HELD_LONG, 0, 10000);
			break;
			
	case POWER_BUTTON_RELEASE:
			
			DEBUG(("hal active state, POWER_BUTTON_RELEASE message arrived...\n"));
			
			MessageCancelAll(getHalTask(), HAL_POWER_BUTTON_HELD_SHORT);
			MessageCancelAll(getHalTask(), HAL_POWER_BUTTON_HELD_LONG);
			break;
            
	case HAL_POWER_BUTTON_HELD_SHORT:
			{
				DEBUG(("hal initialising state, POWER_BUTTON_HELD_SHORT message arrived...\n"));
                /* set pio5 input mode for press key detection*/
             /*      
                 pio5hold = (PioGet()>>5)&0x1;
                if(pio5hold!=1)
                    MessageCancelAll(getHalTask(), HAL_POWER_BUTTON_HELD_LONG);
       */
				initialising_state_exit();
			
				/** let error mananger check initialization result, and determing the control flow **/
				initialisationFinished();
			
				hal.state = ACTIVATING;
			
				/** if passed, go on **/
				activating_state_enter();
			}
			break;
            
		case BATTERY_READING_MESSAGE:
/*		case BATTERY_PROBING_MESSAGE:*/
			{
				DEBUG(("hal warming-up state, BATTERY_READING_MESSAGE message arrived...\n"));
				/** update battery reading and no check, even battery low we have nothing to do **/
				if (hal.voltage == K_VoltageInit) {

					BatteryInit(&hal.battery_state, getHalTask(), BATTERY_READING_SOURCE, BATTERY_POLLING_PERIOD);
				}
				battery_message_handler(message);
				
				update_indication();
			}
			break;	
			
		case APP_EXT_STATE_CHANGE_MESSAGE:
			{
				app_ext_state_change_message_t* msg = (app_ext_state_change_message_t*)message;
				hal.app_state = msg ->state;
			}
			break;
	}
}

void activating_state_enter(void) {
	
	DEBUG(("hal activating state enter...\n"));
	
	ledsPlay(ALL_LEDS_OFF);
	ledsPlay(BEEP_TWICE);
	
	MessageSendLater(getHalTask(), HAL_ACTIVATING_TIMEOUT, 0, BEEP_TWICE_DURATION + 100);
}

void activating_state_exit(void) {
	
	DEBUG(("hal activating state exit...\n"));
}

void activating_handler(Task task, MessageId id, Message message) {
	
	switch(id) {
		
		case PIO_RAW:
		
			DEBUG(("hal activating state, PIO_RAW message arrived...\n"));
			pio_raw_handler(message);
			
			if (!powerAllowedToTurnOn) {
				
				/*****
				  
				  Theoretically we should do something here because user may unplug the charging cable
				  right after entering this state and the battery is low. But it seems to not
				  triggering severe problem, we can let this issue to be solved in next state, 
				  when next batter update message comes, the program will do the checking.
				  
				  Other possible solutions: 
				  1. in initialising state, device is allowed to be powered on when battery is not too low,
				  even if it is in charging. This is common design in mobile phone.
				  2. checking it when entering the next state.
				  
				  In fact, we should go back to initialising state, but up to now, nothing to do. :)
				  
				  ****/
			}
			
			break;
     case POWER_BUTTON_PRESS:
			
			DEBUG(("hal active state, POWER_BUTTON_PRESS message arrived...\n"));
            
			MessageSendLater(getHalTask(), HAL_POWER_BUTTON_HELD_SHORT, 0, 2000);
			break;

			
	case POWER_BUTTON_RELEASE:
			
			DEBUG(("hal active state, POWER_BUTTON_RELEASE message arrived...\n"));
            MessageCancelAll(getHalTask(), HAL_POWER_BUTTON_HELD_LONG);			
			MessageCancelAll(getHalTask(), HAL_POWER_BUTTON_HELD_SHORT);
			break;
			
	case HAL_POWER_BUTTON_HELD_SHORT:
			
			DEBUG(("hal activating state, POWER_BUTTON_HELD_SHORT message arrived...\n"));
			
			/** neglect this message in this state **/
			
			break;
     case HAL_POWER_BUTTON_HELD_LONG:
            {
                DEBUG(("hal active state, HAL_POWER_BUTTON_HELD_LONG message arrived...\n"));
            /*
                pio5hold = (PioGet()>>5)&0x1;
                if(pio5hold==1)*/
                    BootSetMode(0);
            }
            break;
			
	case BATTERY_READING_MESSAGE:
			
			DEBUG(("hal activating state, BATTERY_READING_MESSAGE message arrived...\n"));
			
			battery_message_handler(message);
			
			/** see above comment on PIO_RAW case **/
			
			break;
		
			
    case HAL_ACTIVATING_TIMEOUT:
			
			DEBUG(("hal activating state, HAL_ACTIVATING_TIMEOUT message arrived...\n"));
			
			activating_state_exit();
			hal.state = ACTIVE;
			active_state_enter();
			break;
	}
}

void active_state_enter(void) {
	
	DEBUG(("hal active state enter...\n"));
	
	MessageSend(hal.profile_task, HAL_MESSAGE_SWITCHING_ON, 0);
	
	update_indication();
}

void active_state_exit(void) {
	
	DEBUG(("hal active state exit...\n"));
}

void active_handler(Task task, MessageId id, Message message) {
	
	switch (id) {
		
		case PIO_RAW:
		
			DEBUG(("hal active state, PIO_RAW message arrived...\n"));
			
			pio_raw_handler(message);
			
			if (!powerAllowedToContinue()) {
				
				active_state_exit();
				hal.state = DEACTIVATING;
				deactivating_state_enter();
			}
			else {
				
				update_indication();
			}
			
			break;
            
    case POWER_BUTTON_PRESS:
			
			DEBUG(("hal active state, POWER_BUTTON_PRESS message arrived...\n"));
            
			MessageSendLater(getHalTask(), HAL_POWER_BUTTON_HELD_SHORT, 0, 2000);
			break;

			
	case POWER_BUTTON_RELEASE:
			
			DEBUG(("hal active state, POWER_BUTTON_RELEASE message arrived...\n"));
			
			MessageCancelAll(getHalTask(), HAL_POWER_BUTTON_HELD_SHORT);
            MessageCancelAll(getHalTask(), HAL_POWER_BUTTON_HELD_LONG);
			break;
		
	case HAL_POWER_BUTTON_HELD_SHORT:
			
			DEBUG(("hal active state, POWER_BUTTON_HELD_SHORT message arrived...\n"));
		
			/** turning off **/
			active_state_exit();
			hal.state = DEACTIVATING;
			deactivating_state_enter();

			break;
    case HAL_POWER_BUTTON_HELD_LONG:
          {
                DEBUG(("hal initialising state, HAL_POWER_BUTTON_HELD_LONG message arrived...\n"));
            
            /*     pio5hold = (PioGet()>>5)&0x1;
                if(pio5hold==1)*/
                    BootSetMode(0);
          }
            break;
		
		case BATTERY_READING_MESSAGE:
			/*
			DEBUG(("hal active state, BATTERY_READING_MESSAGE message arrived...\n"));
			*/
			battery_message_handler(message);
			
			if (!powerAllowedToContinue()) {
				
				DEBUG(("hal active state, powerAllowedToContinue failed...\n"));
				
				active_state_exit();
				hal.state = DEACTIVATING;	
				deactivating_state_enter();
			}
			else {
				update_indication();
			}
			break;	
			
		case APP_EXT_STATE_CHANGE_MESSAGE:
			{
				app_ext_state_change_message_t* msg = (app_ext_state_change_message_t*)message;
				hal.app_state = msg ->state;
				
				if (hal.app_state == APP_EXT_STATE_IDLE) {
					
					active_state_exit();
					hal.state = DEACTIVATING;
					deactivating_state_enter();
				}
			}
			break;
	}
}

void deactivating_state_enter(void) {
	
	DEBUG(("hal deactivating state enter...\n"));
	
	ledsPlay(ALL_LEDS_OFF);
	ledsPlay(BEEP_TWICE);
	
	MessageSendLater(getHalTask(), HAL_DEACTIVATING_TIMEOUT, 0, BEEP_TWICE_DURATION + 100);
	
	/** send message to profile **/
	MessageSend(hal.profile_task, HAL_MESSAGE_SWITCHING_OFF, 0);
}

void deactivating_state_exit(void) {
	
	DEBUG(("hal deactivating state exit...\n"));
}

void deactivating_handler(Task task, MessageId id, Message message) {
	
	/** no need to react to any message except timeout, after all, we are going to panic **/
	
	switch(id) {
		
		case PIO_RAW:
		
			DEBUG(("hal deactivating state, PIO_RAW message arrived...\n"));
		
			/** pio_raw_handler(message); **/
			break;
            
   case POWER_BUTTON_PRESS:
			
			DEBUG(("hal active state, POWER_BUTTON_PRESS message arrived...\n"));
            
			MessageSendLater(getHalTask(), HAL_POWER_BUTTON_HELD_SHORT, 0, 2000);
			break;

			
	case POWER_BUTTON_RELEASE:
			
			DEBUG(("hal active state, POWER_BUTTON_RELEASE message arrived...\n"));
			
			MessageCancelAll(getHalTask(), HAL_POWER_BUTTON_HELD_SHORT);
			break;
			
	case HAL_POWER_BUTTON_HELD_SHORT:
			
			DEBUG(("hal deactivating state, POWER_BUTTON_HELD_SHORT message arrived...\n"));
			break;
			
		case BATTERY_READING_MESSAGE:
			/*
			DEBUG(("hal deactivating state, BATTERY_READING_MESSAGE message arrived...\n"));
			*/
			/** battery_message_handler(message); **/
			break;		
			
		case HAL_DEACTIVATING_TIMEOUT:
			
			DEBUG(("hal deactivating state, HAL_DEACTIVATING_TIMEOUT message arrived...\n"));
			
			/** brute way !!! */
			Panic();
			/**
			deactivating_state_exit();
			hal.state = DORMANT;
			initialising_state_enter(); **/
			break;
	}
}


void hal_init(Task profileTask) {
    
    uint16 psBuff[2] = {6,8};
    uint16 read[2];
    uint16 state ;
	
	/** set task hander **/
	hal.task.handler = hal_handler;
	
	/** set profile task **/
	hal.profile_task = profileTask;
	
	/** init charging state **/
	hal.charging_state = CHARGING_UNKNOWN;
	
	/** set voltage to invalid value **/
	hal.voltage = 0xFFFF;
	
	/** app state **/
	hal.app_state = APP_EXT_STATE_UNKNOWN;
	
	/** set init state **/
	hal.state = INITIALISING;
    

   state = PsStore (2, psBuff, 2);
    DEBUG(("PsStore return state %d \n", state));
    
    PsRetrieve (2, read, 2); 
    
    DEBUG(("PsStore return state %2d \n", read[0]));
	
	initialising_state_enter();
}

/************
  
  common functions 
  
  ************/

void enableLDO(void) {
	
	/** set output and drive high **/
	PioSetDir(PIO_LDO_ENABLE, PIO_LDO_ENABLE);
	PioSet(PIO_LDO_ENABLE, PIO_LDO_ENABLE);
}

void disableLDO(void) {
	
	/** set input, don't know pull-up detail, need to clarify **/
	PioSetDir(PIO_LDO_ENABLE, 0);
	
	/** maybe this could change, according the pio.h document, all pios has weak pull-up / pull-down, according the bit value in corresponding output register **/
	PioSet(PIO_LDO_ENABLE, 0); 
}

void SetUartTX()
{
    
    PioSetDir(PIO3, PIO3);
    PioSet(PIO3, PIO3);    
}

void ResetUartTX()
{
    PioSetDir(PIO3, PIO3);
    PioSet(PIO3, 0);
}
 

/** this function may need further refine **/
void pio_raw_handler(Message message) {

	PIO_RAW_T* pio_raw = (PIO_RAW_T*)message;
	
	hal.charging_state = (pio_raw ->pio & PIO_CHARGE_DETECTION) ? CHARGING_CHARGING : CHARGING_NOT_CHARGING;
}

/** see $bluelab$\src\lib\battery\battery.c, sendReading function for message type **/
void battery_message_handler(Message message) {
	
	uint32* mV = (uint32*)message;
	
	/** should we need unsigned long ??? **/
	hal.voltage = (*mV) * (22 + 15) / 15;
}

bool powerAllowedToTurnOn(void) {

	if (hal.charging_state == CHARGING_NOT_CHARGING && hal.voltage < BATTERY_LOW_HYSTERESIS_HIGH_BOUND) {
		
		return FALSE;
	}

	return TRUE;
}

bool powerAllowedToContinue(void) {
	
	if (hal.charging_state == CHARGING_NOT_CHARGING && hal.voltage < BATTERY_LOW_HYSTERESIS_LOW_BOUND) {
		
		return FALSE;
	}
	
	return TRUE;
}













