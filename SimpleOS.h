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

#ifndef SIMPLE_OS_H_
#define SIMPLE_OS_H_

#include <Arduino.h>

#ifndef TASK_COUNT
#error "Please #define TASK_COUNT!"
#elif TASK_COUNT <= 1
#warning "TASK_COUNT should be at least 2 for meaningfull multitasking..."
#endif

#ifndef STACK_SIZE
#define STACK_SIZE 150 //default value
#endif

#ifndef SPACE_REPORTING
#define SPACE_REPORTING 0 //default value
#endif

#define NEED_INIT 0x1
#define SLEEPING 0x2
#define PAUSED 0x4

typedef void (*taskFunction)(); //declare a type called taskFunction that is a function pointer to a void, non arguments function
typedef byte TaskStack; //declare taskStack as an alias for byte (which is an alias for uint_8)

struct TaskInfo //represents all necessary information about a single task
{
	uint16_t stackPointer; //saved stack pointer
	byte taskId; //id of the task
	byte flags; //flags like NEED_INIT or PAUSED
	unsigned long wakeupTime; //the time in ms when the SLEEPING flag will be cleared
	taskFunction function; //function pointer
	#if SPACE_REPORTING
	TaskStack* stackStart; //we need the start of the stack to find out, how many bytes where overwritten
	uint16_t stackSize; //and we need the size
	#endif
};

TaskInfo* currentTask; //the active task
TaskInfo* tasks[TASK_COUNT]; //pointers to all task's TaskInfo
uint8_t newTaskSREG; //the SREG when startMultitasking was called. Contains the interrupt state, will be replicated to all new tasks

//function prototypes
void startMultitasking() __attribute__ ((naked, noreturn));
void _insertTask(byte taskId, taskFunction function);
void pauseTask(byte taskId);
void unpauseTask(byte taskId);
boolean isTaskPaused(byte taskId);
byte getCurrentTaskId();
void restartTask(byte taskId);
void sleep(long ms);
void yield() __attribute__ ((naked)); //we don't need any register-saving pro/epilogue here, so that function is naked
#if SPACE_REPORTING
int getStackUsed(byte taskId);
int getStackSize(byte taskId);
float getStackUsedPercentage(byte taskId);
#endif

//macros that simplify the creation of tasks
#define createTask(name) createTaskWithStackSize(name, STACK_SIZE)
#define createTaskWithStackSize(name, stackSize) \
				const uint16_t taskStackSize_##name = stackSize; /*save the stack size as global constant for later use, will be optimized away.*/\
				TaskStack taskStack_##name[stackSize] __attribute__ ((section (".noinit"))); /*The stack can be in .noinit, zeroing it would take unnecessary time*/\
				TaskInfo task_##name;\
				inline void taskFn_##name() __attribute__((always_inline)); /*the "real" function, can be completely inlined to the "wrapper" function*/\
				 __attribute__((noreturn)) void name()\
				{\
					while(true)\
					{\
						taskFn_##name();\
						yield();\
					}\
				}\
				void taskFn_##name()
//the id is needed to address the correct slot in the array, I'd really like to remove that...
#define insertTask(id, name) \
				tasks[id] = &task_##name;\
				_insertTask(id, name, taskStack_##name, taskStackSize_##name)

///////////////////////////////////////////////////////////////////////////////
// Implementation (no CPP file, so that we can use #defines from the sketch) //
///////////////////////////////////////////////////////////////////////////////

void _insertTask(byte taskId, taskFunction function, TaskStack* stack, uint16_t stackSize)
{
	tasks[taskId]->taskId = taskId;
	tasks[taskId]->flags = NEED_INIT;
	tasks[taskId]->stackPointer = (uint16_t)stack + stackSize - 1; //point to last element in stack
	tasks[taskId]->function = function;
	#if SPACE_REPORTING
	tasks[taskId]->stackSize = stackSize;
	tasks[taskId]->stackStart = stack;
	#endif
}

//pauses the task until upauseTask() is called.
void pauseTask(byte taskId)
{
	tasks[taskId]->flags |= PAUSED;
}

//unpauses a task pause using pauseTask()
void unpauseTask(byte taskId)
{
	tasks[taskId]->flags &= ~PAUSED;
}

//returns true if a task is pauses, false otherwise
boolean isTaskPaused(byte taskId)
{
	return tasks[taskId]->flags & PAUSED;
}

//returns the id of the currently executing task
byte getCurrentTaskId()
{
	return currentTask->taskId;
}

//makes this task sleep for at least the given amount of
//time. Please note: This function may halt the task for
//a longer period of time, if the other tasks fail to 
//call yield() often enough.
//Use this function instead of delay()! Delay will not allow
//other tasks to run, while this task waits!
void sleep(long ms)
{
	currentTask->wakeupTime = millis() + ms;
	currentTask->flags |= SLEEPING;
	yield();
}

//Make the given task to start again from the beginning
void restartTask(byte taskId)
{
	tasks[taskId]->flags |= NEED_INIT; //restarting a task is as simple as setting this flag
}

#if SPACE_REPORTING
//Returns the number of bytes of stack used by the given task.
int getStackUsed(byte taskId)
{
	//run over the complete stack, look for the first untouched "0x55"
	for (uint16_t i = tasks[taskId]->stackSize - 1; i >= 0; i--)
	{
		if(*(tasks[taskId]->stackStart + i) == 0x55)
			return tasks[taskId]->stackSize - i;
	}
	//stack used completely (or even overused):
	return 0;
}

//Returns the size of the stack for the given task as specified
//in createTask()
int getStackSize(byte taskId)
{
	return tasks[taskId]->stackSize;
}

//Get the percentage of stack used by the given task.
float getStackUsedPercentage(byte taskId)
{
	return ((float)getStackUsed(taskId) / getStackSize(taskId)) * 100.0f;
}
#endif

//Starts the execution of task! This function will never return.
void startMultitasking()
{
	#if SPACE_REPORTING
	//run over each task's stack and fill it with 0x55 for getStackUsed()
	for (byte i = 0; i < TASK_COUNT; i++)
	{
		for (uint16_t j = 0; j < tasks[i]->stackSize; j++)
		{
			*(tasks[i]->stackStart + j) = 0x55;
		}
	}
	#endif
	
	//save the SREG state here
	newTaskSREG = SREG;
	
	//we assume that tasks[0] is NEED_INIT and runnable here
	currentTask = tasks[0];
	currentTask->flags &= ~NEED_INIT;
	
	asm volatile (
	"ld r0, %a2+		\n"
	"out __SP_L__, r0	\n"
	"ld r0, %a2+		\n"
	"out __SP_H__, r0	\n"
	"push %0			\n"
	"push %1			\n"
	//"out __SREG__, %3	\n" //SREG is unchanged!
	"ret				\n" //this will start our first process
	:
	: "r" ((uint16_t)(currentTask->function) & 0xFF), "r" (((uint16_t)(currentTask->function) >> 8) & 0xFF), "e" (&(currentTask->stackPointer))
	);
}

//This function interrupts the current task and continues the execution
//of (possible) another task. When this task is chosen to be the next
//in execution (because another taks has called yield()), this function
//will return.
//Inspiration: http://www.avrfreaks.net/modules/FreaksArticles/files/14/Multitasking%20on%20an%20AVR.pdf
void yield()
{
	//This whole function must be written very carefully.
	//We use a lot of inline assembler here, and this
	//is a naked function, so there is also no frame pointer
	
	//TODO: Are interrupts in yield problematic?
	//save the context:
	asm volatile (
	"push r0			\n" //save r0 to the stack
	"in r0, __SREG__	\n" //save the SREG in r0
	//"cli				\n" //s.a.
	"push r0			\n" //save the SREG in r0 on the stack
	"push r1			\n" //...and save the rest of the registers on the stack
	"clr r1				\n" //clear r1 (assumed to be zero by compiler)
	"push r2			\n"
	"push r3			\n"
	"push r4			\n"
	"push r5			\n"
	"push r6			\n"
	"push r7			\n"
	"push r8			\n"
	"push r9			\n"
	"push r10			\n"
	"push r11			\n"
	"push r12			\n"
	"push r13			\n"
	"push r14			\n"
	"push r15			\n"
	"push r16			\n"
	"push r17			\n"
	"push r18			\n"
	"push r19			\n"
	"push r20			\n"
	"push r21			\n"
	"push r22			\n"
	"push r23			\n"
	"push r24			\n"
	"push r25			\n"
	"push r26			\n"
	"push r27			\n"
	"push r28			\n"
	"push r29			\n"
	"push r30			\n"
	"push r31			\n"
	"in r0, __SP_L__	\n" //save the stack pointer (16bit)
	"st %a0+, r0		\n"
	"in r0, __SP_H__	\n"
	"st %a0+, r0		\n"
	:
	: "e" (&(currentTask->stackPointer)) //see: http://www.rn-wissen.de/index.php/Inline-Assembler_in_avr-gcc
	);
	
	//switch to temp/default stack for this method (just the "normal" stack at top of the RAM). No need to save the previous value, we just start at the top
	asm volatile (
	"ldi r16, %0			\n" //this will destroy r16, but all registers have unknown value (for the compiler)
	"out __SP_H__, r16		\n"
	"ldi r16, %1	\n"
	"out __SP_L__, r16		\n"
	:
	: "M" (RAMEND & 0xFF), "M" ((RAMEND >> 8) & 0xFF)
	);
	
	while(1) //while we have no suitable process
	{		
		unsigned long time = millis(); //function calls are no problem here, since we have a valid stack pointer now
		
		byte endTaskId = (currentTask->taskId + 1) % TASK_COUNT;
		byte i = endTaskId; //this is correct, this is a do...while loop!
		do //do...while loop, don't check for the first time, because i == endTaskId for first iteration (intentionally!)
		{
			if(tasks[i]->flags & PAUSED)
				continue;
				
			if(!(tasks[i]->flags & SLEEPING))
			{
				currentTask = tasks[i];
				goto task_found; //break the loop
			}
				
			if((tasks[i]->flags & SLEEPING) && (tasks[i]->wakeupTime <= time)) //wakeup time reached
			{
				tasks[i]->flags &= ~SLEEPING; //unset the sleeping flag
				
				currentTask = tasks[i];
				goto task_found; //break the loop
			}
			
			i = (i+1) % TASK_COUNT;
		} while (i != endTaskId);
		
		//bad: no process is ready to be scheduled. Just wait a bit and retry.
		delayMicroseconds(1000);
	}
	
	task_found:
	
	if(currentTask->flags & NEED_INIT) //process is new, need special handling
	{
		currentTask->flags &= ~NEED_INIT; //remove the flag
		asm volatile (
		"ld r0, %a2+		\n" //load the stack pointer
		"out __SP_L__, r0	\n"
		"ld r0, %a2+		\n"
		"out __SP_H__, r0	\n"
		"push %0			\n" //push the function address to the stack
		"push %1			\n"
		"out __SREG__, %3	\n" //use the saved SREG as a starting point for the new task
		:
		: "r" ((uint16_t)(currentTask->function) & 0xFF), "r" (((uint16_t)(currentTask->function) >> 8) & 0xFF), "e" (&(currentTask->stackPointer)), "r" (newTaskSREG)
		);
		
		asm volatile("ret"); //"return" to the new task we pushed on the stack
	}
	
	//alread running task, restore the context
	asm volatile (
	"ld r0, %a0+		\n" //restore the stack pointer (16bit)
	"out __SP_L__, r0	\n"
	"ld r0, %a0+		\n"
	"out __SP_H__, r0	\n"
	"pop r31			\n" //pop all the registers
	"pop r30			\n"
	"pop r29			\n"
	"pop r28			\n"
	"pop r27			\n"
	"pop r26			\n"
	"pop r25			\n"
	"pop r24			\n"
	"pop r23			\n"
	"pop r22			\n"
	"pop r21			\n"
	"pop r20			\n"
	"pop r19			\n"
	"pop r18			\n"
	"pop r17			\n"
	"pop r16			\n"
	"pop r15			\n"
	"pop r14			\n"
	"pop r13			\n"
	"pop r12			\n"
	"pop r11			\n"
	"pop r10			\n"
	"pop r9				\n"
	"pop r8				\n"
	"pop r7				\n"
	"pop r6				\n"
	"pop r5				\n"
	"pop r4				\n"
	"pop r3				\n"
	"pop r2				\n"
	"pop r1				\n"
	"pop r0				\n" //the old SREG was pushed to the stack
	"out __SREG__, r0	\n" //restore the SREG, this will also restore the interrupt status!
	"pop r0				\n" //and pop off the real value of r0
	:
	: "e" (&(currentTask->stackPointer))
	);
	
	asm volatile("ret"); //...and finally "return" to the task function!
}

#endif /* SIMPLE_OS_H_ */