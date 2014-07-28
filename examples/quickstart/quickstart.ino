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
 
//this is a simple example to show you the basics of
//SimpleOS. Have a look at the README and the "bigExample"
//for more details!
//This example will blink an led on pin 13 and print "hi" to Serial
//every five seconds. Each task is code, as if he would be the only
//one running on the CPU!

#define TASK_COUNT 2 //VERY IMPORTANT: This line must go before the line with the #include!
//TASK_COUNT specifies how many tasks you like to handle
#include <SimpleOS.h>

int led = 13; //this is an build-in led on most boards

createTask(blink)
{
	digitalWrite(led, HIGH);   // turn the LED on (HIGH is the voltage level)
	sleep(1000);               // wait for a second
	digitalWrite(led, LOW);    // turn the LED off by making the voltage LOW
	sleep(1000);               // wait for a second
	//DONT USE DELAY! Use sleep() instead of delay()!
	//Using delay will block the complete sketch, sleep allows others
	//tasks to execute in the meantime!
}

createTask(say_hello)
{
	Serial.println("hi!");
	sleep(5000); //waits for 5sec. Dont use delay here!
}

void setup()
{
	Serial.begin(9600);
	pinMode(13, OUTPUT);
	
	//we need specify each task here, or it won't get excuted.
	//make sure, you have exactly as many tasks, as you specified
	//with TASK_COUNT on top of the file!
	//Call to insert task must be before startMultitasking, and
	//may not be used in tasks.
	insertTask(0, blink); //make sure you just give the NAME of the task, don't call it ("blink", not "blink()" !) 
	insertTask(1, say_hello); //the number is the "taskId", you need it for functions like pauseTask.
	
	Serial.println("Ready for multitasking!");
	
	startMultitasking(); //this function starts up Simple OS.
	//From now on, you tasks will start executing. This function will
	//never return, so don't write any code below it!
}

void loop()
{
	//IMPORTANT: When you use multitasking, loop will not be called anymore!
	//Move all you code to tasks, created with createTask(). But make sure,
	//it's still included in your sketch (like this), otherwise it won't compile...
}