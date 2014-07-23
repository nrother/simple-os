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
#define STACK_SIZE 150
#endif

#ifndef SPACE_REPORTING
#define SPACE_REPORTING 0
#endif

#define NEED_INIT 0x1
#define SLEEPING 0x2
#define PAUSED 0x4

//TODO: More comments!
//TOOO: InsertLoopTask or something...

typedef void (*taskFunction)(); //declare a type called threadFunction that is a function pointer to a void, no arguments function
typedef byte TaskStack; //declare taskStack as an alias for byte (which is an alias for uint_8)

struct TaskInfo //represents all necessary information about a single task
{
	uint16_t stackPointer;
	byte taskId;
	byte flags;
	unsigned long wakeupTime;
	taskFunction function; //function pointer
	#if SPACE_REPORTING
	TaskStack* stackStart; //we need the start of the stack to find out, how many bytes where overwritten
	uint16_t stackSize; //and we need the size
	#endif
};

TaskInfo* currentTask; //the active task
TaskInfo* tasks[TASK_COUNT]; //pointers to all task's TaskInfo
uint8_t newTaskSREG; //the SREG when startMultitasking was called. Contains the interrupt state, will be replicated to all new tasks

void startMultitasking() __attribute__ ((naked));
void createTaskInternal(byte taskId, taskFunction function);
void pauseTask(byte taskId);
void unpauseTask(byte taskId);
boolean isTaskPaused(byte taskId);
byte getCurrentTaskId();
void restartTask(byte taskId);
void sleep(long ms);
void yield() __attribute__ ((naked)); //don't generate prologue/epilogue, we will save registers ourself
#if SPACE_REPORTING
int getStackUsed(byte taskId);
#endif

//macros that simplify the creation of tasks
#define createTask(name) createTaskWithStackSize(name, STACK_SIZE)
//save the stack size as global constant for later use, will be optimized away. The stack can be in .noinit, zeroing it would take unnecessary time.
#define createTaskWithStackSize(name, stackSize) \
				const uint16_t taskStackSize_##name = stackSize;\
				TaskStack taskStack_##name[stackSize] __attribute__ ((section (".noinit")));\
				TaskInfo task_##name;\
				inline void taskFn_##name() __attribute__((always_inline));\
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

void pauseTask(byte taskId)
{
	tasks[taskId]->flags |= PAUSED;
}

void unpauseTask(byte taskId)
{
	tasks[taskId]->flags &= ~PAUSED;
}

boolean isTaskPaused(byte taskId)
{
	return tasks[taskId]->flags & PAUSED;
}

byte getCurrentTaskId()
{
	return currentTask->taskId;
}

void sleep(long ms)
{
	currentTask->wakeupTime = millis() + ms;
	currentTask->flags |= SLEEPING;
	yield();
}

void restartTask(byte taskId)
{
	tasks[taskId]->flags |= NEED_INIT; //restarting a task is as simple as setting this flag
}

#if SPACE_REPORTING
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

int getStackSize(byte taskId)
{
	return tasks[taskId]->stackSize;
}

float getStackUsedPercentage(byte taskId)
{
	return ((float)getStackUsed(taskId) / getStackSize(taskId)) * 100.0f;
}
#endif

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

//Inspiration: http://www.avrfreaks.net/modules/FreaksArticles/files/14/Multitasking%20on%20an%20AVR.pdf
void yield()
{
	//save the context:
	asm volatile (
	"push r0			\n"
	"in r0, __SREG__	\n"
	//"cli				\n" //TODO: Are interrupts in yield problematic?
	"push r0			\n"
	"push r1			\n"
	"clr r1				\n" //clear the r1 (assumed to be zero by compiler)
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
	"in r0, __SP_L__	\n" //save the stack pointer (16bit), to the address in x, incrementing x between the two bytes
	"st %a0+, r0		\n"
	"in r0, __SP_H__	\n"
	"st %a0+, r0		\n"
	:
	: "e" (&(currentTask->stackPointer)) //see: http://www.rn-wissen.de/index.php/Inline-Assembler_in_avr-gcc
	);
	
	//switch to temp/default stack for this method (just the "normal" stack at top of the RAM). No need to save the previous value, we just start at the top
	asm volatile (
	"ldi r16, %0			\n" //this will destroy r16, but all registers have undefined value (for the compiler)
	"out __SP_H__, r16		\n"
	"ldi r16, %1	\n"
	"out __SP_L__, r16		\n"
	:
	: "M" (RAMEND & 0xFF), "M" ((RAMEND >> 8) & 0xFF)
	);
	
	while(1) //while we have no suitable process
	{		
		unsigned long time = millis();
		
		byte endTaskId = (currentTask->taskId + 1) % TASK_COUNT;
		byte i = endTaskId; //do check the same process again!
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
		currentTask->flags &= ~NEED_INIT;
		asm volatile (
		"ld r0, %a2+		\n"
		"out __SP_L__, r0	\n"
		"ld r0, %a2+		\n"
		"out __SP_H__, r0	\n"
		"push %0			\n"
		"push %1			\n"
		"out __SREG__, %3	\n" //use the saved SREG as a starting point for the new task
		"ret				\n" //this will start our first process
		:
		: "r" ((uint16_t)(currentTask->function) & 0xFF), "r" (((uint16_t)(currentTask->function) >> 8) & 0xFF), "e" (&(currentTask->stackPointer)), "r" (newTaskSREG)
		);
		
		asm volatile("ret"); //return here, we don't want to pop anything!
	}
	
	//restore the context
	asm volatile (
	"ld r0, %a0+		\n" //restore the stack pointer (16bit), from the address in pointer register, incrementing between the two bytes
	"out __SP_L__, r0	\n"
	"ld r0, %a0+		\n"
	"out __SP_H__, r0	\n"
	"pop r31			\n"
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
	"pop r0				\n"
	"out __SREG__, r0	\n" //this will also restore the interrupt status!
	"pop r0				\n"
	:
	: "e" (&(currentTask->stackPointer))
	);
	
	//and return, but to where we came from...
	asm volatile("ret");
}

#endif /* SIMPLE_OS_H_ */