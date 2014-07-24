#include "debug.h"
#include "hal.h"
#include "sppb.h"

#include <pio.h>


int main(void)
{				
    DEBUG(("Main Started...\n"));

	
	hal_init(getSppbTask());
	sppb_init(getHalTask());
	
	MessageLoop();	
    
    return 0;
}



