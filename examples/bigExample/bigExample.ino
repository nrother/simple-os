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
 
//This is a example that tries to use every
//available feature to test and showcase it.
//For a quick start look at the other, smaller
//and simpler example!

//these #defines have to be done BEFORE #including the SimpleOS header file!
#define TASK_COUNT 3 //how many tasks do you want?
#define SPACE_REPORTING 1 //enable SPACE_REPORTING to have access to getStackUsed()
#define STACK_SIZE 250 //you may define STACK_SPACE to override the default stack size of 150 (when not created with createTaskWithStackSize())

#include <SimpleOS.h>

//create a simple taks with the name "blink_led", with the default stacksize of 250 (or 150 if STACK_SIZE 250 wouldn't be defined) 
createTask(blink_led)
{
	int cnt = 0;
	
	while(true) //This is an endless loop.
	{
		cnt++;
		if(cnt >= 10)
		{
			cnt = 0;
			restartTask(2); //this will make task 2 to start again from the beginning
		}
		
		Serial.println("T1");
		
		sleep(10); //pause this task for 10ms, pass the control to another task
		//IMPORTANT: Don't use delay(), this will block the CPU, sleep allows others tasks to run in the spare time
	}
}

createTask(test_restart)
{
	while(true)
	{
		int a = 0;
		while(a < 10)
		{
			yield();
			a++;
		}
		//this line will never be reached, because the task is restarted before!
		Serial.println("failed!");
	}
}

int led = 13;

createTaskWithStackSize(blink_led2, 40) //don't make the stack much smaller that this, about 35 byte are needed by the OS!
{
	digitalWrite(led, LOW);
	delay(20); //We use delay here because of the very short delay, but in general use sleep()!
	digitalWrite(led, HIGH);
	delay(20);
}

void setup()
{	
	Serial.begin(9600);
	pinMode(led, OUTPUT);
	
	insertTask(0, blink_led); //only the name of the task, NOT "blink_led()" (don't call the function, pass its name!)
	insertTask(1, blink_led2);
	insertTask(2, test_restart);
	
	startMultitasking();
}

void loop()
{
	//IMPORTANT: When you use multitasking, loop will not be called anymore!
	//Move all you code to tasks, created with createTask()
}