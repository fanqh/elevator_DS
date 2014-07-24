/* Copyright (C) Cambridge Silicon Radio Limited 2005-2009 */
/* Part of BlueLab 4.1.2-Release */
#include "spp_dev_private.h"
#include "spp_dev_auth.h"
#include "spp_dev_b_leds.h"
#include "spp_dev_b_buttons.h"
#include "at_command.h"
#include "hal.h"
#include "errman.h"

#include <connection.h>
#include <panic.h>
#include <stdio.h>
#include <stream.h>
#include <pio.h>
#include <sink.h>
#include <source.h>

#include <string.h>

#include "debug.h"
#include "sppb.h"
#include "command_return_code.h"
#include "indication.h"
/*
#define USE_SYSTEM_STREAM_CONNECT
*/
/** task data **/
static sppb_task_t sppb;

/* Application power table  */
#define APP_POWER_TABLE_ENTRIES (sizeof(app_keyboard_power_table) / sizeof(lp_power_table))
static const lp_power_table app_keyboard_power_table[]=
{
	/* mode,    	min_interval, max_interval, attempt, timeout, duration */
	{lp_sniff,		20,           52,			1,		 1,	      1},
	{lp_sniff,		54,           162,			1,		 16,	  30},
	{lp_sniff,		164,          402,			1,		 16,	  600},
	{lp_sniff,		404,	      802,			1,		 16,	  0}
};

/* Sniff Subrating parameters used if remote device supports Sniff Subrating */
/* These values are for testing only. Real applications should consider other values. */
#define APP_SSR_MAX_REMOTE_LATENCY      512     /* The maximum time the remote device need not be present when subrating (in 0.625ms units). Must be at least 2 times sniff interval. */
#define APP_SSR_MIN_REMOTE_TIMEOUT      10       /* 	The minimum time the remote device should stay in sniff before entering subrating mode (in 0.625ms units) */
#define APP_SSR_MIN_LOCAL_TIMEOUT       20       /* The minimum time the local device should stay in sniff before entering subrating mode (in 0.625ms units) */
/**************************************************************************************************
  
  from spp_dev_init.h & .c

  */
static void sppDevInit(void);

static void sppDevInit()
{
    spp_init_params init;

    init.client_recipe = 0;
    init.size_service_record = 0;
	init.service_record = 0;
	init.no_service_record = 0;
	
    /* Initialise the spp profile lib, stating that this is device B */ 
    SppInitLazy(getSppbTask(), getSppbTask(), &init);
}

/**************************************************************************************************
  
  from spp_dev_inquire.h & c
  
  */

#define CLASS_OF_DEVICE		0x1F00
void sppDevInquire(sppb_task_t* app);

void sppDevInquire(sppb_task_t* app)
{
    /* Turn off security */
    ConnectionSmRegisterIncomingService(0x0000, 	
										0x0001, 
										0x0000);
    /* Write class of device */
    ConnectionWriteClassOfDevice(CLASS_OF_DEVICE);
    /* Start Inquiry mode */
    setSppState(SPPB_PAIRABLE);
    /* Set devB device to inquiry scan mode, waiting for discovery */
    ConnectionWriteInquiryscanActivity(0x400, 0x200);
    ConnectionSmSetSdpSecurityIn(TRUE);
    /* Make this device discoverable (inquiry scan), and connectable (page scan) */
    ConnectionWriteScanEnable(hci_scan_enable_inq_and_page);
    /* Send timeout message after specified time, if no device is found to be connected with */
    MessageCancelAll(getSppbTask(), SPPB_PAIRABLE_TIMEOUT_IND);
    MessageSendLater(getSppbTask(), SPPB_PAIRABLE_TIMEOUT_IND, 0, 500000);        
}

/**************************************************************************************************
  
  sppb
  
  */

/** debug output **/
static void unhandledSppState(sppb_state_t state, MessageId id);


/** state entry / exit and handers **/
void sppb_handler(Task task, MessageId id, Message message);

/** separate spp layer and cl layer **/
void spp_handler(Task task, MessageId id, Message message);
void cl_handler(Task task, MessageId id, Message message);

/** main state handlers & entry/exits **/
static void initialising_state_handler(Task task, MessageId id, Message message);
static void ready_state_handler(Task task, MessageId id, Message message);
static void pairable_state_handler(Task task, MessageId id, Message message);
static void connecting_state_handler(Task task, MessageId id, Message message);
 void connected_state_handler(Task task, MessageId id, Message message);
 void disconnecting_state_handler(Task task, MessageId id, Message message);
static void initialising_state_enter(void);
static void initialising_state_exit(void);
static void ready_state_enter(void);
static void ready_state_exit(void);
static void pairable_state_enter(void);
static void pairable_state_exit(void);
static void connecting_state_enter(void);
static void connecting_state_exit(void);
static void connected_state_enter(void);
static void connected_state_exit(void);
static void disconnecting_state_enter(void);
static void disconnecting_state_exit(void);

/** scan is the super-state of pairable & connecting **/
static void scan_state_enter(void);		
static void scan_state_exit(void);

/** sub state handlers **/
static void echo_state_handler(Task task, MessageId id, Message message);
static void pipe_state_handler(Task task, MessageId id, Message message);
static void echo_state_enter(void);
static void echo_state_exit(void);
static void pipe_state_enter(void);
static void pipe_state_exit(void);


void process_spp_more_data(void);
void process_spp_more_space(void);
void process_uart_more_data(void);
void process_uart_more_space(void);

void source_push(Source source, Sink sink);

static void buffer1_to_sink(Sink sink, uint8* buffer, uint16 num);
/** static void sink_pull(Sink sink, Source source); **/


/*static  void buffer_to_sink(Sink sink, buffered_bytes_t* buffer);*/ 

static void send_echo_message(Sink sink, at_command_return_code_t ret_code);


Task getSppbTask(void)
{
    return &sppb.task;
}

static void unhandledSppState(sppb_state_t state, MessageId id)
{
    DEBUG(("SPP current state %d message id 0x%x\n", state, id));   
}


void setSppState(const sppb_state_t state)
{
    DEBUG(("SPP State - C=%d N=%d\n",sppb.state, state));
    sppb.state = state;
}

/**************************************************************************************************
  
  initialising state 
  
  */
static void initialising_state_enter(void) {

	Source aSource;
	
	DEBUG(("spp initialising state enter...\n"));

	aSource = StreamUartSource();

	update_indication();
	
	if( aSource )
	{
		StreamConfigure( VM_STREAM_UART_CONFIG, VM_STREAM_UART_THROUGHPUT);
		
		
		StreamConnectDispose( aSource );
		sppb.uart_initialised = TRUE;
		
		/** initialise connection library **/
		ConnectionInit(getSppbTask());
	}
	
}

static void initialising_state_exit(void) {
	
	DEBUG(("spp initialising state exit...\n"));
	
}

static void initialising_state_handler(Task task, MessageId id, Message message) {
	
	switch(id) {
		
		case SPP_INIT_CFM:

			DEBUG(("spp initialising state, SPP_INIT_CFM message arrived...\n"));
			
            /* Check for spp_init_success. What do we do if it failed? */
            if (((SPP_INIT_CFM_T *) message)->status == spp_init_success)
            {
				/** switch to ready state, unconnectable, undiscoverable **/
				sppb.spp_initialised = TRUE;
				
				initialising_state_exit();
				sppb.state = SPPB_READY;
				ready_state_enter();
            }
			else {
				
				/** don't panic **/
			}
            break;
			
		default:
			unhandledSppState(sppb.state, id);
			break;
	}
}

/**************************************************************************************************
  
  ready state
  
  revert state map design, in fact the idle/working external state is the superstate of pairable, connecting, connected and disconnecting.
  
  TODO: if switched back from pairable/connecting state (when powering off), 
	there may be some connect_ind/cfm message in queue, check it and reject them politely
  
  */
static void ready_state_enter() {
	
	app_ext_state_change_message_t* message;
	
	update_indication();
	
	message = (app_ext_state_change_message_t*)malloc(sizeof(app_ext_state_change_message_t));
	
	DEBUG(("spp ready state enter...\n"));
	
	message ->state = APP_EXT_STATE_IDLE;
	MessageSend(sppb.hal_task, APP_EXT_STATE_CHANGE_MESSAGE, message);
}

static void ready_state_exit() {
	app_ext_state_change_message_t* message =  (app_ext_state_change_message_t*)malloc(sizeof(app_ext_state_change_message_t));
	
	DEBUG(("spp ready state exit...\n"));
	
	message ->state = APP_EXT_STATE_WORKING;
	MessageSend(sppb.hal_task, APP_EXT_STATE_CHANGE_MESSAGE, message);	
}

void ready_state_handler(Task task, MessageId id, Message message) {
	
	switch(id) {
		
		case HAL_MESSAGE_SWITCHING_ON:
		
			DEBUG(("spp ready state, HAL_MESSAGE_SWITCHING_ON message arrived...\n"));
			
			ready_state_exit();
			setSppState(SPPB_PAIRABLE);
			scan_state_enter();
			pairable_state_enter();
			break;
			
		default:
			unhandledSppState(sppb.state, id);
			break;			
	}
}

/**************************************************************************************************
  
  scan super-state
  
  */

static void scan_state_enter() {

	DEBUG(("spp scan state enter...\n"));

	
	/** the code is from original bluelab sample in spp_dev_b spp_dev_inquire() **/
	/* don't know if some initialisation could be done multiple times */
    /* Turn off security */
    ConnectionSmRegisterIncomingService(0x0000, 	
										0x0001, 
										0x0000);
    /* Write class of device */
    ConnectionWriteClassOfDevice(CLASS_OF_DEVICE);
    /* Start Inquiry mode */
    /** setSppState(SPPB_PAIRABLE); **/
    /* Set devB device to inquiry scan mode, waiting for discovery */
    ConnectionWriteInquiryscanActivity(0x400, 0x200);
    ConnectionSmSetSdpSecurityIn(TRUE);
    /* Make this device discoverable (inquiry scan), and connectable (page scan) */
    ConnectionWriteScanEnable(hci_scan_enable_inq_and_page);
}

static void scan_state_exit() {

	DEBUG(("spp scan state exit...\n"));

	/* turn off scan **/
	ConnectionWriteScanEnable(hci_scan_enable_off);
}

/**************************************************************************************************
  
  pairable state
  
  */
static void pairable_state_enter() {

	DEBUG(("spp pairable state enter...\n"));
	
	update_indication();
		
	/** start timer **/
    MessageCancelAll(getSppbTask(), SPPB_PAIRABLE_TIMEOUT_IND);
    MessageSendLater(getSppbTask(), SPPB_PAIRABLE_TIMEOUT_IND, 0, SPPB_PAIRABLE_DURATION);	
}

static void pairable_state_exit() {

	DEBUG(("spp pairable state exit...\n"));
	
	/** stop timer **/
    MessageCancelAll(getSppbTask(), SPPB_PAIRABLE_TIMEOUT_IND);	
}

static void pairable_state_handler(Task task, MessageId id, Message message) {
	
	switch(id) {
		
		case SPP_CONNECT_IND:

			DEBUG(("spp pairable state, SPP_CONNECT_IND message arrived...\n"));
		
		    /* Received command that a device is trying to connect. Send response. */
            sppDevAuthoriseConnectInd(&sppb,(SPP_CONNECT_IND_T*)message);
			
			pairable_state_exit();
            sppb.state = SPPB_CONNECTING;
			connecting_state_enter();
			break;
			
		case SPP_CONNECT_CFM:
			{	
				SPP_CONNECT_CFM_T *cfm = (SPP_CONNECT_CFM_T *) message;
				
				DEBUG(("spp pairable state, SPP_CONNECT_CFM message arrived...\n"));
				
				if (cfm->status == rfcomm_connect_success)
                {
                    /* Device has been reset to pairable mode. Disconnect from current device, this is code from official sppb example */
                    SppDisconnect(cfm->spp);
                }
			}  
			break;
			
		case HAL_MESSAGE_SWITCHING_OFF:
			{
				DEBUG(("spp pairable state, HAL_MESSAGE_SWITCHING_OFF message arrived...\n"));
				
				pairable_state_exit();
				scan_state_exit();
				setSppState(SPPB_READY);
				ready_state_enter();
			}
			break;

		case SPPB_PAIRABLE_TIMEOUT_IND:
			{
				DEBUG(("spp pairable state, SPPB_PAIRABLE_TIMEOUT_IND message arrived...\n"));
				
				pairable_state_exit();
				setSppState(SPPB_READY);
				ready_state_enter();				
			}
			break;
		default:
			unhandledSppState(sppb.state, id);
			break;		
	}
}

/**************************************************************************************************
  
  connecting state, no sub-state to maintain, spp/connection layer should do timeout job
  
  */

static void connecting_state_enter() {
	
	DEBUG(("spp connecting state enter...\n"));
	
	update_indication();

}

static void connecting_state_exit() {

	DEBUG(("spp connecting state exit...\n"));
}

static void connecting_state_handler(Task task, MessageId id, Message message) {
	
	switch(id) {
		
		case SPP_CONNECT_CFM:
			{
				SPP_CONNECT_CFM_T *cfm = (SPP_CONNECT_CFM_T *) message;
				
				DEBUG(("spp connecting state, SPP_CONNECT_CFM message arrived...\n"));

				if (cfm -> status == rfcomm_connect_success) {
				
					/** switching off scan, stop timer **/
					/** ConnectionWriteScanEnable(hci_scan_enable_off); **/
					/** (void) MessageCancelFirst(&sppb.task, SPPB_PAIRABLE_TIMEOUT_IND); **/
					connecting_state_exit();
					scan_state_exit();
					
                	sppb.spp = cfm->spp;
					sppb.spp_sink = cfm ->sink;
                    
                    ConnectionReadRemoteSuppFeatures(getSppbTask(), sppb.spp_sink); 
                	setSppState(SPPB_CONNECTED);
					connected_state_enter();
				}
				else {
					
					/** go back **/
					connecting_state_exit();
					sppb.state = SPPB_PAIRABLE;
					pairable_state_enter();
				}
			}
			break;	
			
		default:
			unhandledSppState(sppb.state, id);
			break;		
	}
}

/**************************************************************************************************
  
  connected state
  
  */
void connected_state_enter() {
	
	Sink sink;
	Source source;
	
	DEBUG(("spp connected state enter...\n"));
	
	sink = sppb.spp_sink;
	source = StreamSourceFromSink(sink);
	
	sppb.spp_sink_busy = 0;
	SourceDrop( source , SourceSize( source ));
	
	SourceConfigure( source, VM_SOURCE_MESSAGES, VM_MESSAGES_SOME );
	SinkConfigure( sink, VM_SINK_MESSAGES, VM_MESSAGES_SOME );

	/** enter init sub-state **/
	sppb.conn_state = CONN_ECHO;
	echo_state_enter();
}


void connected_state_exit() {

	DEBUG(("spp connected state exit...\n"));

	switch(sppb.conn_state) {
	
		case CONN_ECHO:
			
			echo_state_exit();
			break;
			
		case CONN_PIPE:
			
			pipe_state_exit();
			break;
			
		default:
			
			break;
	}
	
	/** clear spp related message **/
	(void)MessageCancelAll(getSppbTask(), SPP_MESSAGE_MORE_DATA); /** here we do it anyway **/
	if (sppb.spp_sink) {
		StreamDisconnect(0, sppb.spp_sink);
		StreamDisconnect(StreamSourceFromSink(sppb.spp_sink), 0);
		SourceDrop( StreamSourceFromSink(sppb.spp_sink), SourceSize( StreamSourceFromSink(sppb.spp_sink) ) );
	}
	
	(void)MessageCancelAll(getSppbTask(), SPP_MESSAGE_MORE_SPACE);
	sppb.spp_sink = 0;
	sppb.spp_sink_busy = 0;
	
	/** dont clear sppb.spp, the next state need it, it covers both connected state AND disconnecting state **/
}

static void echo_state_enter(void) {

	DEBUG(("spp connected state echo subState enter...\n"));

	update_indication();

	sppb.command_started = FALSE;
	sppb.command_result = 0xFFFF;

	/** start timer **/
	MessageSendLater(getSppbTask(), SPPB_ECHO_TIMEOUT_IND, 0, SPPB_ECHO_DURATION);
}

static void echo_state_exit(void) {

	DEBUG(("spp connected state echo subState exit...\n"));
	
	/** stop timer, MessageCancelFirst() may also work **/
	(void)MessageCancelAll(getSppbTask(), SPPB_ECHO_TIMEOUT_IND);
	
	/** clear echo command timer **/
	(void)MessageCancelAll(getSppbTask(), SPPB_ECHO_COMMAND_TIMEOUT);
	
	/** cancel waiting job message, not used now but may add this feature in future **/
	(void)MessageCancelAll(getSppbTask(), SPPB_ECHO_SINK_READY);
	
	sppb.command_started = FALSE;
	sppb.command_result = 0xFFFF;
}

static void echo_source(Source source) {
	
	/** init command result as invalid value **/
	sppb.command_result = 0xFFFF;
			
	/** parse **/
	parseSource(source, getSppbTask());
			
	if (SourceSize(source) > 0) {	/** multiple command in once is not allowed **/
				
		SourceDrop(source, SourceSize(source));
		sppb.command_result = CMD_RET_UNRECOGNIZED;
	}
	else if (sppb.command_result == 0xFFFF) {
				
		/** unknown reason but we make sure **/
		sppb.command_result = CMD_RET_UNRECOGNIZED;
	}
			
	/** start echo job **/
	/**MessageSendConditionally(getSppbTask(), SPPB_ECHO_SINK_READY, 0, &sppb.spp_sink_busy);**/     /**DT??**/
}

static void echo_state_handler(Task task, MessageId id, Message message) {
	
	switch (id) {
		
		case SPP_MESSAGE_MORE_DATA:
		{
			Source source;
			uint16 size;
			const uint8* buf;
						
			DEBUG(("spp connected state echo subState, SPP_MESSAGE_MORE_DATA message arrived...\n"));
			
			if (sppb.command_started == FALSE) {
				
				MessageSendLater(getSppbTask(), SPPB_ECHO_COMMAND_TIMEOUT, 0, 100);
				sppb.command_started = TRUE;
			}
			
			source = StreamSourceFromSink(sppb.spp_sink);
			size = SourceSize(source);	/** size won't be zero **/
			buf = SourceMap(source);
			

			if (size > 2 && buf[size-2] == 0x0d && buf[size-1] == 0x0a) {	/** trailing 0x0d0a, command finished **/
				
				MessageCancelAll(getSppbTask(), SPPB_ECHO_COMMAND_TIMEOUT);
				sppb.command_started = FALSE;
				
				echo_source(source);
                MessageSendConditionally(getSppbTask(), SPPB_ECHO_SINK_READY, 0, &sppb.spp_sink_busy);     /**fan**/
			}
			else 
            {
               
                SourceDrop(source, size);
				
				/** do nothing, waiting for more bytes or timeout **/
			}

			/* reschedule timeout message **/
			(void)MessageCancelAll(getSppbTask(), SPPB_ECHO_TIMEOUT_IND);
			MessageSendLater(getSppbTask(), SPPB_ECHO_TIMEOUT_IND, 0, SPPB_ECHO_DURATION);
		}
		break;
		
		case SPPB_ECHO_COMMAND_TIMEOUT:
		{
			Source source = StreamSourceFromSink(sppb.spp_sink);
			sppb.command_started = FALSE;
			
			/** we know it's a garbage command, but we process it anyway **/
			echo_source(source);
            MessageSendConditionally(getSppbTask(), SPPB_ECHO_SINK_READY, 0, &sppb.spp_sink_busy);     /**fan**/
		}
		break;
        
        case SPP_ECHO_PIOSTATE_TIMEOUT:  /**fan**/
        {
            PioSetDir(PIO3, 0);
	      /*  PioSet(PIO3, 0);
            sppb.command_result = CMD_RET_OK;*/
            MessageSendConditionally(getSppbTask(), SPPB_ECHO_SINK_READY, 0, &sppb.spp_sink_busy);     /**fan**/
            
        }
        break;
               
		case SPPB_ECHO_SINK_READY:

			DEBUG(("spp connected state echo subState, SPPB_ECHO_SINK_READY message arrived...\n"));
			
			/** now the sink is ready, send echo message **/
			send_echo_message(sppb.spp_sink, sppb.command_result);
			if (sppb.command_result == CMD_RET_OK) {
				
				echo_state_exit();
				sppb.conn_state = CONN_PIPE;
				pipe_state_enter();
			}
			break;
		
		case SPPB_ECHO_TIMEOUT_IND:

			DEBUG(("spp connected state echo subState, SPPB_ECHO_TIMEOUT_IND message arrived...\n"));
			
			connected_state_exit();
			sppb.state = SPPB_DISCONNECTING;
			disconnecting_state_enter(); 	/* need discussing the design **/
			break;
        case SPP_MESSAGE_MORE_SPACE:
			
			DEBUG(("spp connected state, SPP_MESSAGE_MORE_SPACE message arrived...\n"));
			
			/** this is a persistent state during sub state transition and should be processed in this handler **/
			DEBUG(( "    spp sink has %d byte more space, spp_sink_busy erase... \n", (SinkSlack(sppb.spp_sink)) ));
			sppb.spp_sink_busy = FALSE;
			break;
		
		default:
			unhandledSppState(sppb.state, id);
			break;
	}
}

void pipe_state_enter(void) {

	Sink sink;
	Source source;
	
	DEBUG(("spp connected state pipe subState enter...\n"));
	
    
	update_indication();
	
	sink = StreamUartSink();
	source = StreamUartSource();
	
	/** undisconnect the source/sink **/
	StreamDisconnect(0, sink);
	StreamDisconnect(source, 0);
	
	/** cancel all more_data messages if any **/
	(void)MessageCancelAll(getSppbTask(), MESSAGE_MORE_DATA);
	
	/** drop all data if any **/
	SourceDrop( source, SourceSize( source ) );
	
		/** init locals **/
	sppb.uart_sink_busy = FALSE;
	sppb.count_down = SPPB_PIPE_IDLE_TIMEOUT;	
	sppb.dirty = FALSE;

    sppb.buartseting = FALSE;
    MessageCancelAll(getSppbTask(), SPP_PIPE_PACK_FINISH);

	
#ifdef USE_SYSTEM_STREAM_CONNECT
	
	StreamConnect(StreamUartSource(), sppb.spp_sink);
	StreamConnect(StreamSourceFromSink(sppb.spp_sink), StreamUartSink());
	
#else
	
	/** set uart as send more_data/more_space messages only once, see api reference **/
	SourceConfigure( source, VM_SOURCE_MESSAGES, VM_MESSAGES_SOME);
	SinkConfigure( sink, VM_SINK_MESSAGES, VM_MESSAGES_SOME);
	
	/** register myself as source/sink message receiver **/
	MessageSinkTask(sink, getSppbTask());
	
	/** start countdown clock 
	MessageSendLater(getSppbTask(), SPPB_PIPE_COUNT_DOWN, 0, 1000);**/
	
#endif	
}

void pipe_state_exit(void) {
	
	
	
/*	DEBUG(("spp connected state pipe subState exit...\n"));*/
	

	
#ifdef USE_SYSTEM_STREAM_CONNECT
	
	StreamDisconnect(StreamUartSource(), sppb.spp_sink);
	StreamDisconnect(StreamSourceFromSink(sppb.spp_sink), StreamUartSink());
	
	SourceDrop(StreamUartSource(), SourceSize(StreamUartSource()));
	SourceDrop(StreamSourceFromSink(sppb.spp_sink), SourceSize(StreamSourceFromSink(sppb.spp_sink)));
	
	StreamConnectDispose(StreamUartSource());
	
#else	
	
	Source aSource;
	
	aSource = StreamUartSource();

	/** clear uart source and related resource **/
	(void)MessageCancelAll(getSppbTask(), SPPB_PIPE_SPP_SINK_READY);
	(void)MessageCancelAll(getSppbTask(), MESSAGE_MORE_DATA);
	SourceDrop( aSource, SourceSize( aSource ));

	/** clear uart sink and related resource **/
	(void)MessageCancelAll(getSppbTask(), MESSAGE_MORE_SPACE);
	sppb.uart_sink_busy = FALSE;
	
	/** redirect uart stream **/
	StreamConnectDispose(StreamUartSource());
	
	/** clear all job created from spp_message_more_data **/
	(void)MessageCancelAll(getSppbTask(), SPPB_PIPE_UART_SINK_READY);
	(void)MessageCancelAll(getSppbTask(), SPP_MESSAGE_MORE_DATA);

	/** spp_sink and related resource is maintained by super state, no need to clean-up here **/
	
	/** stop clock **/
	(void)MessageCancelAll(getSppbTask(), SPPB_PIPE_COUNT_DOWN);
	sppb.count_down = 0;
	sppb.dirty = 0;
    

    sppb.buartseting = FALSE;
    MessageCancelAll(getSppbTask(), SPP_PIPE_PACK_FINISH);
    

	
	/** we are leaving pipe sub state, not connected super state **/
	/** so don't clear spp sink related thing, they are maintained by super state, 
		left them to connected state handler **/
	/** (void)MessageCancelAll(getSppbTask(), SPP_MESSAGE_MORE_SPACE); **/
#endif
	
}

static void pipe_state_handler(Task task, MessageId id, Message message) {
	
	switch (id) {
		
		case SPP_MESSAGE_MORE_DATA:
			
			DEBUG(("spp connected state pipe subState, SPP_MESSAGE_MORE_DATA message arrived...\n"));
			{
                const uint8* buf;
			  	Source source = StreamSourceFromSink(sppb.spp_sink);
				uint16 size = SourceSize(source);				
				buf = SourceMap(source);  
                sppb.dirty = TRUE;
				
			   if ((size > 2) && (buf[size-2] == 0x0d) && (buf[size-1] == 0x0a)&&(buf[0]== 0x0d)&&(buf[1]== 0x0a)) 
               {	
                   
                   DEBUG(("   spp connected state pipe subState,SET command arrived\n"));
				   echo_source(source); 
                   send_echo_message(sppb.spp_sink, sppb.command_result);
                   if(sppb.command_result != CMD_RET_OK)
                   {
                       pipe_state_exit();
                       connected_state_enter();   
                   }
                   sppb.uart_sink_busy = FALSE;
                   sppb.spp_sink_busy = FALSE;
                   sppb.buartseting = FALSE;
                   SourceDrop(source, size);
			   }
               else 
               {
                   if(sppb.buartseting == TRUE)
                   {
                       SourceDrop(source, size);
                   }
                   else
                   {
                       MessageCancelAll(getSppbTask(), SPP_PIPE_PACK_FINISH);
                       MessageSendLater(task, SPP_PIPE_PACK_FINISH, 0, SPP_PIPE_PACK_TIMEOUT); 
                   }

               }
           }
           break;
                
        case  SPP_PIPE_PACK_FINISH :
            {             
                Source source = StreamSourceFromSink(sppb.spp_sink);
				uint16 size = SourceSize(source);
				
                DEBUG(("spp connected state pipe subState,SPP_PIPE_PACK_FINISH arrived active uart\n"));    	
			    sppb.Spp_ReceiveNum = size;
				memcpy(sppb.pSpp_ReceiveBuf, SourceMap(source), size);/*if size more than 256 what can i do???*/
				sppb.buartseting = TRUE;
                SourceDrop(source, size);          

               if(sppb.uart_polarity==0)
                {
                    ResetUartTX();
                    MessageSendLater(task, SPP_ECHO_PIOSTATE_TIMEOUT, 0, sppb.uart_keeptime);
                }
                else if(sppb.uart_polarity==1)
                {
                    SetUartTX();
                    MessageSendLater(task, SPP_ECHO_PIOSTATE_TIMEOUT, 0, sppb.uart_keeptime);
                }
                else
                    MessageSend(task, SPP_ECHO_PIOSTATE_TIMEOUT,0);                     
           }
            
        break;
        
        case  SPP_ECHO_PIOSTATE_TIMEOUT:        
        {
             DEBUG(("spp connected state pipe subState,SPP_ECHO_PIOSTATE_TIMEOUT arrived uart had been actived and send data\n"  ));
             PioSetDir(PIO3, 0);          
             MessageSendLater(getSppbTask(), SPPB_PIPE_UART_SINK_READY, 0, 1);           
        }    
        break;
                
            
        case SPP_MESSAGE_MORE_SPACE:
            
          { 

			DEBUG(("spp connected state, SPP_MESSAGE_MORE_SPACE message arrived...\n"));
	
			DEBUG(( "    spp sink has %d byte more space, spp_sink_busy erase... \n", (SinkSlack(sppb.spp_sink)) ));
			sppb.spp_sink_busy = FALSE;
        }
			break;
		
		case MESSAGE_MORE_DATA:

			DEBUG(("spp connected state pipe subState, MESSAGE_MORE_DATA message arrived...\n"));
              			
			{
				Source source = StreamUartSource();
				uint16 size = SourceSize(source);
           
				if (size) 
                {
#if 0                    
					sppb.Uart_ReceiveNum = size;
					memcpy(sppb.pUart_ReceiveBuf, SourceMap(source), size);
					SourceDrop(source, size);
#endif
					MessageSendConditionally(getSppbTask(), SPPB_PIPE_SPP_SINK_READY, 0, &sppb.uart_sink_busy);
				}
				else 
                {
					SourceDrop(source, size);
				}
				sppb.dirty = TRUE;
				
				DEBUG(( "    uart source has %d bytes now... job scheduled... \n",size));
			}
			break;
			
		case MESSAGE_MORE_SPACE:
         
           {
                
			DEBUG(("spp connected state pipe subState, MESSAGE_MORE_SPACE message arrived...\n"));	
            sppb.uart_sink_busy = FALSE;
			DEBUG(("    uart has %d byte more space, busy flag erased...\n", (SinkSlack(StreamUartSink())) ));
            
          }          
			break;
#if 0			
		case SPPB_PIPE_COUNT_DOWN:
			
/*			DEBUG(("spp connected state pipe subState, SPPB_PIPE_COUNT_DOWN message arrived...\n"));*/
			if (sppb.dirty) 
            {
				
				sppb.count_down = SPPB_PIPE_IDLE_TIMEOUT;
				sppb.dirty = FALSE;
				MessageSendLater(getSppbTask(), SPPB_PIPE_COUNT_DOWN, 0, 1000);
			}
			else 
            {
				
				sppb.count_down = sppb.count_down - 1;
				
				if (sppb.count_down == 0) 
                {
					 
					connected_state_exit();
					sppb.state = SPPB_DISCONNECTING;
					disconnecting_state_enter();
				}
				else 
                {
					
					MessageSendLater(getSppbTask(), SPPB_PIPE_COUNT_DOWN, 0, 1000);
				}
			}
			break;
#endif			
		case SPPB_PIPE_UART_SINK_READY:
			
            DEBUG(("spp connected state pipe subState, SPPB_PIPE_UART_SINK_READY message arrived...\n"));
           {    
			 	Source source;
				Sink sink;
				
				sink = StreamUartSink();
				source = StreamSourceFromSink(sppb.spp_sink);
				
				if (sink == 0 || !SinkIsValid(sink)) 
                {	
					raise_exception(3, 1);
				}								
                if (SinkSlack(sink) == 0) { /** this should not happen **/
					
					DEBUG(("    UART_SINK_READY arrived but sink is not available............ERROR!!!\n"));
					
					raise_exception(1, 1);
				}
             
           else  if ((sppb.Spp_ReceiveNum)&&(sppb.uart_sink_busy==FALSE)) 
             {                        

			      DEBUG(("spp connected state pipe subState, message is valid...\n"));
                   
                  sppb.uart_sink_busy = TRUE; 
                  
			      buffer1_to_sink(StreamUartSink(), sppb.pSpp_ReceiveBuf, sppb.Spp_ReceiveNum);
                  sppb.buartseting = FALSE;
                  
                  memset(sppb.pSpp_ReceiveBuf, 0, sppb.Spp_ReceiveNum);
                  sppb.Spp_ReceiveNum = 0; 
			  }
           }
			break;
			
		case SPPB_PIPE_SPP_SINK_READY:
            
            DEBUG(("spp connected state pipe subState, SPPB_PIPE_SPP_SINK_READY message arrived...\n"));
			{
             
				Source source;
				Sink sink;
				
				sink = sppb.spp_sink;
				source = StreamUartSource();
				
				if (sink == 0 || !SinkIsValid(sink)) 
                {
					raise_exception(3, 1);
                    DEBUG(("    SinkIsValid arrived3, 6 .........ERROR ??? \n"));
				}
				else if (source == 0 || !SourceIsValid(source)) 
                {
					raise_exception(3, 1);
                    DEBUG(("    SourceIsValid 3, 8.........ERROR ??? \n"));
				}
				
				DEBUG(( "    ---- begin of SPPB_PIPE_SPP_SINK_READY processing ----\n" ));
				
				if (SourceSize(source) == 0) {	/** nothing to send **/
					
					DEBUG(("    SPP_SINK_READY arrived but source has no data to send.........ERROR ??? \n"));
				}
				else if (SinkSlack(sink) == 0) { /** this should not happen **/
					
					DEBUG(("    SPP_SINK_READY arrived but sink is not available3, 1...........ERROR !!! \n"));
					
					raise_exception(3, 1);
				}
				else 
                {
					
					/** sink_pull(StreamUartSink(), StreamSourceFromSink(sppb.spp_sink)); **/
					uint16 count = SourceSize(source);
					uint16 count_moved;
					bool flush_result;
					
					DEBUG(("    uart source want to send %d byte data...\n", count));
					if (count > SinkSlack(sink)) 
                    {
                        DEBUG(("    but spp sink has only %d byte space...\n", count));
                        raise_exception(3, 1);
						/*count = SinkSlack(sink);**/	
					}
					
					count_moved = StreamMove(sink, source, count);
                    if (count_moved != count) 
                    {
                        DEBUG(("    count_moved != count"));
						raise_exception(3, 3);
                        
					}
					else {
					}
					
					flush_result = SinkFlush(sink, count);
					if ((flush_result == FALSE)) {
                        DEBUG(("    flush_result!=count_moved"));
						raise_exception(3, 1);
                        
					}
					
					sppb.spp_sink_busy = TRUE;
					
					DEBUG(("    %d bytes moved from uart source to spp sink... spp busy flag set... \n", count));
					
					if (SourceSize(source) == 0)
                    {	
						
						DEBUG(("    uart source has no more bytes to send... \n"));
					}
				}
				
				DEBUG(( "    ---- end of SPPB_PIPE_SPP_SINK_READY processing ----\n" ));
			}

            
            
#if 0
			DEBUG(("spp connected state pipe subState, SPPB_PIPE_SPP_SINK_READY message arrived...\n"));

			{
                sppb.spp_sink_busy = TRUE;
				buffer1_to_sink(sppb.spp_sink, sppb.pUart_ReceiveBuf, sppb.Uart_ReceiveNum);
                memset(sppb.pUart_ReceiveBuf, 0, sppb.Spp_ReceiveNum);
                sppb.Uart_ReceiveNum = 0; 
			} 
#endif            
			break;
		default:
			unhandledSppState(sppb.state, id);
			break;
	}
}

void connected_state_handler(Task task, MessageId id, Message message) {
	
	switch(id) {
		
		case SPP_DISCONNECT_IND:	 /*passively disconnected, switching to scan **/

			DEBUG(("spp connected state, SPP_DISCONNECT_IND message arrived...\n"));
			
			/** sub-state exit first **/
			switch(sppb.conn_state) {
				
				case CONN_ECHO:
				
					echo_state_exit();
					break;
					
				case CONN_PIPE:
					
					pipe_state_exit();
					break;
					
			}
		
			connected_state_exit();
			setSppState(SPPB_PAIRABLE);
			scan_state_enter();
			pairable_state_enter();
		
			break;
			
		case HAL_MESSAGE_SWITCHING_OFF:
			
			DEBUG(("spp connected state, HAL_MESSAGE_SWITCHING_OFF message arrived...\n"));
			
			connected_state_exit();
			setSppState(SPPB_DISCONNECTING);
			disconnecting_state_enter();
			
			break;
			
		case SPP_MESSAGE_MORE_SPACE:
			

			
		case SPP_MESSAGE_MORE_DATA:			
		case MESSAGE_MORE_DATA:
		case MESSAGE_MORE_SPACE:
		default:	 
			
			switch (sppb.conn_state) {
				
				case CONN_ECHO:
				
					echo_state_handler(task, id, message);
					break;
					
				case CONN_PIPE:
					
					pipe_state_handler(task, id, message);
					break;
				
			}
			break;
	}				 
}

void disconnecting_state_enter(void) {
	
	DEBUG(("spp disconnecting state enter...\n"));
	
	update_indication();
	/** check reason and output debug **/
	SppDisconnect(sppb.spp);
}

void disconnecting_state_exit(void) {
	
	DEBUG(("spp disconnecting state exit...\n"));
	/** nothing to do **/
}

 void disconnecting_state_handler(Task task, MessageId id, Message message) {
	
	switch(id) {
		
		case SPP_DISCONNECT_IND:
		
			DEBUG(("spp disconnecting state, SPP_DISCONNECT_IND message arrived...\n"));
			disconnecting_state_exit();
			setSppState(SPPB_READY);
			ready_state_enter();
			
			break;
		
		default:
			unhandledSppState(sppb.state, id);
			break;		
	}
}

void sppb_handler(Task task, MessageId id, Message message) {
	
	sppb_state_t state = sppb.state;
	
	if ((id & 0xFF00) == CL_MESSAGE_BASE) {
		
		cl_handler(task, id, message);
		return;
	}
	
	switch (state) {
		
		case SPPB_INITIALISING:
			initialising_state_handler(task, id, message);
			break;
			
		case SPPB_READY:
			ready_state_handler(task, id, message);
			break;
			
		case SPPB_PAIRABLE:
			pairable_state_handler(task, id, message);
			break;
			
		case SPPB_CONNECTING:
			connecting_state_handler(task, id, message);
			break;
			
		case SPPB_CONNECTED:
			connected_state_handler(task, id, message);
			break;
			
		case SPPB_DISCONNECTING:
			disconnecting_state_handler(task, id, message);
			break;
			
		default:
			break;
	}
}

/*************************************************************************
NAME    
    appHandleClDmRemoteFeaturesCfm
DESCRIPTION
    This function handles the CM_DM_REMOTE_FEATURES_CFM message, this
    message in response to a ConnectionReadRemoteSuppFeatures function. This
    function calls ConnectionSetSniffSubRatePolicy to enable Sniff Subrating
    if remote device supports it.
RETURNS
    void     
*/
static void appHandleClDmRemoteFeaturesCfm(sppb_task_t *theApp, CL_DM_REMOTE_FEATURES_CFM_T *cfm)
{
    
	
    /* Set link low power policy */
    ConnectionSetLinkPolicy(theApp->spp_sink, APP_POWER_TABLE_ENTRIES, app_keyboard_power_table);    
    
    if (cfm->status == hci_success &&
        cfm->features[2] & 0x0200)                  /* remote supports Sniff Subrating */
    {
        
        DEBUG(("the remote support sniff mode\n"));
        /* Set up Sniff Subrating parameters */
        ConnectionSetSniffSubRatePolicy(theApp->spp_sink,
                                        APP_SSR_MAX_REMOTE_LATENCY,
                                        APP_SSR_MIN_REMOTE_TIMEOUT,
                                        APP_SSR_MIN_LOCAL_TIMEOUT);
    }
}


/** connection library layer **/
void cl_handler(Task task, MessageId id, Message message) {
	
	switch (id) {	
        
        
               /*fan debuge*/    
    case CL_DM_MODE_CHANGE_EVENT:
        
            DEBUG(("CL_DM_MODE_CHANGE_EVENT arrived&&&&&&&&&&\r\n"));
        
            break;
            
     case  CL_DM_REMOTE_FEATURES_CFM:
            
           DEBUG(("appHandleClDmRemoteFeaturesCfm\n"));
           appHandleClDmRemoteFeaturesCfm(&sppb, (CL_DM_REMOTE_FEATURES_CFM_T*) message);

          break;
          
          
	case CL_INIT_CFM:
        DEBUG(("CL_INIT_CFM\n"));
        if(((CL_INIT_CFM_T*)message)->status == success) {
			
			/** change sub-state **/
			sppb.cl_initialised = TRUE;
            sppDevInit();   
		}
        else {
			
            /** don't panic **/
		}
        break;
    case CL_DM_LINK_SUPERVISION_TIMEOUT_IND:
        DEBUG(("CL_DM_LINK_SUPERVISION_TIMEOUT_IND\n"));
        break;
    case CL_DM_SNIFF_SUB_RATING_IND:
        DEBUG(("CL_DM_SNIFF_SUB_RATING_IND\n"));
        break;
	
    case CL_DM_ACL_OPENED_IND:
        DEBUG(("CL_DM_ACL_OPENED_IND\n"));
        break;
    case CL_DM_ACL_CLOSED_IND:
        DEBUG(("CL_DM_ACL_CLOSED_IND\n"));
        break;
    case CL_SM_PIN_CODE_IND:
        DEBUG(("CL_SM_PIN_CODE_IND\n"));
        sppDevHandlePinCodeRequest((CL_SM_PIN_CODE_IND_T *) message);
        break;
    case CL_SM_AUTHORISE_IND:  
        DEBUG(("CL_SM_PIN_CODE_IND\n"));
        sppDevAuthoriseResponse((CL_SM_AUTHORISE_IND_T*) message);
        break;
    case CL_SM_AUTHENTICATE_CFM:
        DEBUG(("CL_SM_AUTHENTICATE_CFM\n"));
        sppDevSetTrustLevel((CL_SM_AUTHENTICATE_CFM_T*)message);    
        break;
    case CL_SM_ENCRYPTION_KEY_REFRESH_IND:
        DEBUG(("CL_SM_ENCRYPTION_KEY_REFRESH_IND\n"));
        break;
    case CL_DM_LINK_POLICY_IND:
        DEBUG(("CL_DM_LINK_POLICY_IND\n"));
        break;
    case CL_SM_IO_CAPABILITY_REQ_IND:
        DEBUG(("CL_SM_IO_CAPABILITY_REQ_IND\n"));
        ConnectionSmIoCapabilityResponse( &sppb.bd_addr, 
                                          cl_sm_io_cap_no_input_no_output,
                                          FALSE,
                                          TRUE,
                                          FALSE,
                                          0,
                                          0 );
        break;
 
    case CL_SM_REMOTE_IO_CAPABILITY_IND:
        {
            CL_SM_REMOTE_IO_CAPABILITY_IND_T *csricit = 
                    ( CL_SM_REMOTE_IO_CAPABILITY_IND_T *) message;

            DEBUG(("CL_SM_REMOTE_IO_CAPABILITY_REQ_IND\n"));
            
            DEBUG(("\t Remote Addr: nap %04x uap %02x lap %08lx\n",
                    csricit->bd_addr.nap,
                    csricit->bd_addr.uap,
                    csricit->bd_addr.lap ));
            sppb.bd_addr = csricit->bd_addr;
        }
        break;
		
	default:
		break;
	}
}

/***
  
  ***/
void sppb_init(Task hal_task) {
	
	sppb.pSpp_ReceiveBuf = malloc( KSPP_RECEIVEDBUF_NUM );
    
    if( NULL ==  sppb.pSpp_ReceiveBuf )
    {
        Panic();
    }
#if 0    
    sppb.pUart_ReceiveBuf = malloc( KSPP_RECEIVEDBUF_NUM );
    
     if( NULL ==  sppb.pUart_ReceiveBuf )
    {
        Panic();
    }
#endif    
	sppb.task.handler = sppb_handler;
	sppb.hal_task = hal_task;
	
	sppb.spp = 0;
	sppb.spp_sink = 0;
	sppb.cl_initialised = FALSE;
	sppb.spp_initialised = FALSE;
	sppb.uart_initialised = FALSE;
	
	sppb.state = SPPB_INITIALISING;
	
	initialising_state_enter();
}

#if 0

/** be sure to call this function only if sink has MORE_SPACE !!! **/
 void buffer_to_sink(Sink sink, buffered_bytes_t* buffer) {
	
	uint16 size, offset;
	uint8* dst;
	
    
	if (sink == 0 || buffer == 0) {
		
		DEBUG(("buffer_to_sink, invalid parameter...\n"));
		return;
	}
	size = SinkSlack(sink);
	
	if (size < buffer -> size) {
		
		DEBUG(("buffer_to_sink, buffer size is larger than available space in sink...\n"));
		return;
	}
	offset = SinkClaim(sink, buffer ->size);
	if (offset == 0xFFFF) {
		
		DEBUG(("buffer_to_sink, SinkClaim failed...\n"));
		return;
	}
	dst = SinkMap(sink);
	if (dst == 0) {
		
		DEBUG(("buffer_to_sink, SinkMap failed...\n"));
		return;
	}
	memcpy(dst + offset, buffer ->buf, buffer ->size);
	
	if (SinkFlush(sink, buffer ->size) == 0) {
	
		DEBUG(("buffer_to_sink, SinkFlush fail...\n"));
		return;
	}
	DEBUG(("buffer_to_sink, SinkMap sucess...\n"));	
	return;
}

#endif 

void buffer1_to_sink(Sink sink, uint8* buffer, uint16 num) {
	
	uint16 size, offset;
	uint8* dst;
	
    
	if (sink == 0 || num == 0) {
		
		DEBUG(("buffer_to_sink, invalid parameter...\n"));
		return;
	}
	size = SinkSlack(sink);
	
	if (size < num) {
		
		DEBUG(("buffer_to_sink, buffer size is larger than available space in sink...\n"));
		return;
	}
	offset = SinkClaim(sink, num);
	if (offset == 0xFFFF) {
		
		DEBUG(("buffer_to_sink, SinkClaim failed...\n"));
		return;
	}
	dst = SinkMap(sink);
	if (dst == 0) {
		
		DEBUG(("buffer_to_sink, SinkMap failed...\n"));
		return;
	}
	memcpy(dst + offset, buffer, num);
    
    DEBUG(("buffer_to_sink, %d...\n",num));	
    
	
	if (SinkFlush(sink, num) == 0) {
	
		DEBUG(("buffer_to_sink, SinkFlush fail...\n"));
		return;
	}
	DEBUG(("buffer_to_sink, SinkMap sucess...\n"));	
	return;
}


const char rt_ok[32] = "\r\nOK\r\n";
const char baud_err[32] = "\r\nBAUDRATE ERROR\r\n";
const char stop_err[32] = "\r\nSTOP ERROR\r\n";
const char parity_err[32] = "\r\nPARITY ERROR\r\n";
const char polarity_err[32] = "\r\npolarity ERROR\r\n";
const char unrecognized[32] = "\r\nUNRECOGNIZED\r\n";



/** this function should , but not return error, need refine **/
void send_echo_message(Sink sink, at_command_return_code_t ret_code) {
	
	uint16 length, offset;
	const char* p;
	uint8* dest;
	
	switch (ret_code) {
		
		case CMD_RET_OK:
		
			p = rt_ok;
			break;
			
		case CMD_RET_UNSUPPORTED_BAUDRATE:
			
			p = baud_err;
			break;
		
		case CMD_RET_UNSUPPORTED_STOP:
			
			p = stop_err;
			break;
			
		case CMD_RET_UNSUPPORTED_PARITY:
			
			p = parity_err;
			break;
            
        case CMD_RET_UNSUPPORTED_P0LARITY:
            p = polarity_err;
            break;
			
		case CMD_RET_UNRECOGNIZED:
		default:
			
			p = unrecognized;
			break;
	}	
	
	length = strlen(p);
	
	offset = SinkClaim(sink, length);
	if (offset == 0xFFFF) return;
	
	dest = SinkMap(sink);
	memcpy(dest + offset, p, length);
	
	/** may return zero for error **/
	SinkFlush(sink, length);
}






#if 0 /* push-pull code */

void source_push(Source source, Sink sink) {
	
	uint16 source_size, sink_space, count;
	
	if (source == 0 || sink == 0) {
		return;
	}
	
	source_size = SourceSize(source);
	if (source_size == 0) {
		return;
	}
	
	sink_space = SinkSlack(sink);
	if (sink_space == 0) {
		return;
	}
	
	count = sink_space;
	if (source_size < count) {
		count = source_size;
	}
	
	StreamMove(sink, source, count);
	SinkFlush(sink, count);
}

void process_spp_more_data(void) {
	
	PanicFalse(sppb.state == SPPB_CONNECTED);
	
	if (sppb.conn_state == CONN_ECHO) {
		
		/** in echo sub state **/
		Source source = StreamSourceFromSink(sppb.spp_sink);
		
		do {
			parseSource(source, getSppbTask());
			
			if (sppb.conn_state != CONN_ECHO) {
								
				return;
			}
			
		} while (SourceSize(source) > 0);
	}
	else if (sppb.conn_state == CONN_PIPE) {
		
		source_push(StreamSourceFromSink(sppb.spp_sink), StreamUartSink()); 
	}
	else {	
	}
}

void process_spp_more_space(void) {
	
	PanicFalse(sppb.state == SPPB_CONNECTED);
	
	if (sppb.conn_state == CONN_ECHO) {
		
		/** nothing to do **/
	}
	else if (sppb.conn_state == CONN_PIPE) {
		
		sink_pull(sppb.spp_sink, StreamSourceFromSink(StreamUartSink()));
	}
	else {
	}
}

void process_uart_more_data(void) {
	
	PanicFalse(sppb.state == SPPB_CONNECTED);
	
	if (sppb.conn_state == CONN_ECHO) {
		
		/** drop data **/
		Source source = StreamSourceFromSink(StreamUartSink());
		SourceDrop(source, SourceSize(source));
	}
	else if (sppb.conn_state == CONN_PIPE) {
		
		source_push(StreamSourceFromSink(StreamUartSink()), sppb.spp_sink);
	}
	else {
	}
}

void process_uart_more_space(void) {
	
	PanicFalse(sppb.state == SPPB_CONNECTED);
	
	if (sppb.conn_state == CONN_ECHO) {
		
		/** nothing to do **/
	}
	else if (sppb.conn_state == CONN_PIPE) {
		
		sink_pull(StreamUartSink(), StreamSourceFromSink(sppb.spp_sink));
	}
	else {
	}
}

#endif











