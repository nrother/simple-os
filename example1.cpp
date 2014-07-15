/*
 * SimpleOS
 *
 * A really simple cooperative multitasking OS for Arduino
 * and other AVR micro controllers.
 *
 * Author Niklas Rother <info@niklas-rother.de>
 * License: MIT
 *
 */

#define TASK_COUNT 3 //this have to be done BEFORE #including the SimpleOS header file!
#define SPACE_REPORTING 1 //enable SPACE_REPORTING to have access to getStackSpaceLeft()

#include <Arduino.h>
#include "SimpleOS.h"

createTask(test_restart)
{
	while(true)
	{
		int a = 0;
		while(a < 50)
		{
			yield();
			a++;
		}
		PORTC ^= 0xFF;
	}
}

createTask(blink_led)
{
	int cnt = 0;
	
	while(true) //This is an endless loop. Task must NEVER reach the end of there code!
	{
		cnt++;
		if(cnt >= 10)
		{
			cnt = 0;
			//restartTask(2);
			restartTask(test_restart);
		}
		
		PORTA ^= 0xFF;
		yield();
		//sleep(5);
	}
}

createTaskWithStackSize(blink_led2, 40) //don't make the stack much smaller that this, about 35 byte are needed by the OS!
{
	while(true)
	{
		PORTB ^= 0xFF;
		yield();
		//sleep(5);
	}
}

int main()
{	
	insertTask(0, blink_led); //only the name of the task, NOT "blink_led()" (don't call the function, pass its name!)
	insertTask(1, blink_led2);
	insertTask(2, test_restart);
	
	startMultitasking();
}