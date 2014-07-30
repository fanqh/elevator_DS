#ifndef SPPB_H
#define SPPB_H

#include <message.h>
#include <spp.h>

#include "messagebase.h"
#include "app_state.h"

/** **/
#define SPPB_PAIRABLE_DURATION 		(90000)
#define SPPB_ECHO_DURATION			(90000)
#define SPPB_PIPE_IDLE_TIMEOUT		(600)		/*in seconds **/

#define SPP_PIPE_PACK_TIMEOUT    20

#define KSPP_RECEIVEDBUF_NUM    256

/** sppb state **/
typedef enum
{
    SPPB_INITIALISING,
    SPPB_READY,				/** this is the initialized and stable state **/
    SPPB_PAIRABLE,
    SPPB_CONNECTING,
    SPPB_CONNECTED,
	SPPB_DISCONNECTING,
	SPPB_STATE_NUM
} sppb_state_t;


typedef enum
{
    PACK_NOSTART,
    PACK_START,
    PACK_FINISH
} pack_state_t;

/** sppb connected sub state **/
typedef enum
{
	CONN_ECHO,
	CONN_PIPE
} connected_state_t;

/** sppb task data, noting that many of them are state- or substate-specific, do create/destroy in entry/exit funcs **/
typedef struct 
{
	/** task **/
    TaskData            task;
	
	/** hal task **/
	Task				hal_task;
	
		
	/** bluetooth addr, used by cl/spp **/
    bdaddr              bd_addr;
	
	/** initialisation result **/	
	bool				cl_initialised;		
	bool				spp_initialised;
	bool				uart_initialised;
	
	/** connected state-specific parameters 						**/
    SPP*                spp;					/* connected state 	**/  /** for connected state parameters, don't clean up when transition between sub-state, 	**/
	Sink				spp_sink;				/* connected state 	**/  /** init and clean in connected enter/exit 											**/
	uint16				spp_sink_busy;			/* connected state 	**/
	bool				command_started;		/* echo state only  **/
	uint16				command_result;			/* echo state only	**/	 /** this code is used to indicate what should be returned. due to parse code, there is no otherway for sync method return value **/
	uint16				uart_sink_busy;			/* pipe state only	**/
	uint16				count_down;				/* pipe state only  **/	 /** these two field are for polling timer **/
	bool				dirty;					/* pipe state only 	**/	
    
    uint8               uart_polarity ;         /*what uart shold active before sending data*/   
    uint16               uart_keeptime;     
    
    uint8               *pSpp_ReceiveBuf;
    uint16               Spp_ReceiveNum;
    
    bool                 buartseting;          
/*
    uint8               *pUart_ReceiveBuf;
    uint16               Uart_ReceiveNum;
*/
	/** main state & sub state */
    sppb_state_t        state;
	connected_state_t	conn_state;
	
} sppb_task_t;

tydef struct
{
    uint8 head;
    bdaddr btaddr;
    uint32 remaintime;
    uint8 tail
}Time_Encryption_t

void sppb_init(Task hal_task);

Task getSppbTask(void);


#endif /** SPPB_H **/


