#include <stdio.h>
#include <stdlib.h>
#include "diag/trace.h"

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"
#include "time.h"

#define CCM_RAM __attribute__((section(".ccmram")))

#define QUEUE_SIZE 3
#define RECIEVER_TIME_PERIOD 100
#define TOTAL_MESSAGES 10
#define BOUNDS_SIZE 6


//************************************************   GLOBAL VARIABLES  *******************************************************************//

//Timer Handles For Each Task
static TimerHandle_t xTimer_Sender1 = NULL;
static TimerHandle_t xTimer_Sender2 = NULL;
static TimerHandle_t xTimer_Sender3 = NULL;
static TimerHandle_t xTimer_Receiver = NULL;



//Stores if timers have started or not (pdTRUE or pdFALSE)
BaseType_t xTimer_SenderOneStarted, xTimer_SenderTwoStarted, xTimer_SenderThreeStarted, xTimer_ReceiverStarted;

//Queue Handle
QueueHandle_t Queue;

BaseType_t isQueueFull;
BaseType_t isQueueEmpty;


//Semaphores
SemaphoreHandle_t SenderOne_Semphr;
SemaphoreHandle_t SenderTwo_Semphr;
SemaphoreHandle_t SenderThree_Semphr;
SemaphoreHandle_t Receiver_Semphr;

//Variables

//Sender and Receiver Counters
int SenderOne_TotalSentMessages=0;
int SenderOne_SentMessages = 0;
int SenderOne_BlockedMessages = 0;
int SenderOne_TotalTime=0;

int SenderTwo_TotalSentMessages=0;
int SenderTwo_SentMessages = 0;
int SenderTwo_BlockedMessages = 0;
int SenderTwo_TotalTime=0;

int SenderThree_TotalSentMessages=0;
int SenderThree_SentMessages = 0;
int SenderThree_BlockedMessages = 0;
int SenderThree_TotalTime=0;

int TotalMessages=0;
int TotalSentMessages = 0;
int TotalBlockedMessages = 0;

int ReceivedMessages = 0;

//Boundary and Period Variables

unsigned int LowerBound[BOUNDS_SIZE]={50,80,110,140,170,200};
unsigned int UpperBound[BOUNDS_SIZE]={150,200,250,300,350,400};
int BoundIndex;

//Variables to store periods of each timer
int InitialPeriodOne = 0;
int InitialPeriodTwo = 0;
int InitialPeriodThree = 0;

//Function Prototypes
static void ReceiverTimer( TimerHandle_t xTimer );  //ReceiverTimer Interrupt Service Routine
static void SenderOneTimer( TimerHandle_t xTimer ); //SenderOneTimer Interrupt Service Routine
static void SenderTwoTimer(TimerHandle_t xTimer);	//SenderTwoTimer Interrupt Service Routine
static void SenderThreeTimer(TimerHandle_t xTimer); //SenderThreeTimer Interrupt Service Routine

void SenderOneTask(void);   //Priority = 1
void SenderTwoTask(void);   //Priority = 1
void SenderThreeTask(void); //Priority = 2
void ReceiverTask(void);	//Priority = 3

void InitSystem(void);		//System Initialization Function -> Resets all variables and sets the first random period for the timers

void PeriodicReset(void);
/*Periodic Reset -> Is Called when receiver receives a set amount of messages.
It prints the stats, resets the variables and updates the random period bounds*/

void UpdateBounds(void); //Helper function called within periodic reset to update the random period bounds

int GenerateRandomNumber(int num1_cpy, int num2_cpy); //Helper function within each sender task to calculate the random period of each task



#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wreturn-type"


//************************************************  MAIN FUNCTION  *********************************************************************//

int main(int argc, char* argv[])
{
	//Initiates the rand() function in GenerateRandomNumber() and lets it make a random number different each time.
	srand(time(NULL));

	//Create Queue
	Queue = xQueueCreate(QUEUE_SIZE, sizeof(char[20]));

	//Create Sender Semaphore
	SenderOne_Semphr = xSemaphoreCreateBinary();
	SenderTwo_Semphr = xSemaphoreCreateBinary();
	SenderThree_Semphr = xSemaphoreCreateBinary();

	//Create Receiver Semaphore
	Receiver_Semphr = xSemaphoreCreateBinary();

	InitSystem();

	//Create Sender and Receiver Tasks
	xTaskCreate(SenderOneTask, "Sender One Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	xTaskCreate(SenderTwoTask, "Sender Two Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	xTaskCreate(SenderThreeTask, "Sender Three Task", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
	xTaskCreate(ReceiverTask, "Receiver Task", configMINIMAL_STACK_SIZE, NULL, 3, NULL);


	//Create Timers
	xTimer_Sender1  = 	xTimerCreate( "SenderTimer1", ( pdMS_TO_TICKS(InitialPeriodOne) ), pdTRUE, ( void * ) 0, SenderOneTimer);
	xTimer_Sender2  = 	xTimerCreate( "SenderTimer2", ( pdMS_TO_TICKS(InitialPeriodTwo) ), pdTRUE, ( void * ) 1, SenderTwoTimer);
	xTimer_Sender3  = 	xTimerCreate( "SenderTimer3", ( pdMS_TO_TICKS(InitialPeriodThree) ), pdTRUE, ( void * ) 2, SenderThreeTimer);
	xTimer_Receiver = 	xTimerCreate( "ReceiverTimer", ( pdMS_TO_TICKS(RECIEVER_TIME_PERIOD) ), pdTRUE, ( void * ) 3, ReceiverTimer);


	//Check if timers were created successfully. if yes, then it starts the timers.
	if( ( xTimer_Sender1 != NULL ) && ( xTimer_Sender2 != NULL ) && (xTimer_Sender3 != NULL) && (xTimer_Receiver != NULL))
	{
		xTimer_SenderOneStarted 	= 	xTimerStart( xTimer_Sender1, 0 );
		xTimer_SenderTwoStarted 	= 	xTimerStart( xTimer_Sender2, 0 );
		xTimer_SenderThreeStarted 	= 	xTimerStart( xTimer_Sender3, 0 );
		xTimer_ReceiverStarted 		= 	xTimerStart( xTimer_Receiver, 0 );

	}

	//Checks if timers Started Successfully. If yes, Start the Scheduler
	if( xTimer_SenderOneStarted == pdPASS && xTimer_SenderTwoStarted == pdPASS && xTimer_SenderThreeStarted == pdPASS && xTimer_ReceiverStarted == pdPASS)
	{
		trace_puts("Scheduler Starting");
		trace_printf("*********************************************************** \n");
		vTaskStartScheduler();
	}

	return 0;
}

#pragma GCC diagnostic pop



//************************************************   FUNCTION DEFINITIONS  *******************************************************************//

void InitSystem(void)
{
	//Resets Counters
	SenderOne_TotalSentMessages=0;
	SenderOne_SentMessages = 0;
	SenderOne_BlockedMessages = 0;
	SenderOne_TotalTime=0;

	SenderTwo_TotalSentMessages=0;
	SenderTwo_SentMessages = 0;
	SenderTwo_BlockedMessages = 0;
	SenderTwo_TotalTime=0;

	SenderThree_TotalSentMessages=0;
	SenderThree_SentMessages = 0;
	SenderThree_BlockedMessages = 0;
	SenderThree_TotalTime=0;

	TotalMessages=0;
	TotalSentMessages = 0;
	TotalBlockedMessages = 0;

	ReceivedMessages = 0;

	xQueueReset(Queue); // Clear the queue

	BoundIndex=0; //start first iteration

	// Generate random initial periods for each sender
	InitialPeriodOne =   GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex]);
	InitialPeriodTwo =   GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex]);
	InitialPeriodThree = GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex]);


}


void PeriodicReset(void)
{
		//Local Variables for random period calculation

	    int RandomPeriodOne = 0;
		int RandomPeriodTwo = 0;
		int RandomPeriodThree = 0;
		int AverageTimerOne=0;
		int AverageTimerTwo=0;
		int AverageTimerThree=0;
		int AverageTotal=0;

		//Calculating Statistics

		//Total Sent, Succefully Sent and Blocked Messages
		TotalSentMessages = SenderOne_SentMessages + SenderTwo_SentMessages + SenderThree_SentMessages;
		TotalBlockedMessages = SenderOne_BlockedMessages + SenderTwo_BlockedMessages + SenderThree_BlockedMessages;
		TotalMessages=TotalSentMessages+TotalBlockedMessages;

		//Total Succefully Sent for Each sender Task
		SenderOne_TotalSentMessages=SenderOne_SentMessages+SenderOne_BlockedMessages;
		SenderTwo_TotalSentMessages=SenderTwo_SentMessages+SenderTwo_BlockedMessages;
		SenderThree_TotalSentMessages=SenderThree_SentMessages+SenderThree_BlockedMessages;

		//Average Period Calculation for each task
		AverageTimerOne=SenderOne_TotalTime/SenderOne_TotalSentMessages;
		AverageTimerTwo=SenderTwo_TotalTime/SenderTwo_TotalSentMessages;
		AverageTimerThree=SenderThree_TotalTime/SenderThree_TotalSentMessages;

		// Overall Average Period
		AverageTotal=((AverageTimerOne+AverageTimerTwo+AverageTimerThree)/3);

		trace_printf("\n\n");
		trace_printf("*************************** Stats for iteration %d ************************************\n", BoundIndex+1);

		// Printing Stats

		//Average Periods
		trace_printf("Average Period for Sender One: %d \n", AverageTimerOne);
		trace_printf("Average Period for Sender Two: %d \n", AverageTimerTwo);
		trace_printf("Average Period for Sender Three: %d \n", AverageTimerThree);
		trace_printf("Average Period for Iteration: %d \n\n", AverageTotal);

		//Total Stats
		trace_printf("Total Messages Sent by the Senders:  %d \n", TotalMessages);
		trace_printf("Total Successfully Sent Messages by the Senders:  %d \n", TotalSentMessages);
		trace_printf("Total Blocked Messages by the Senders:  %d \n", TotalBlockedMessages);
		trace_printf("Total Received Messages:  %d \n\n", ReceivedMessages);

		//Stats for each task
		trace_printf("Total Sent Messages by Sender One:  %d \n", SenderOne_TotalSentMessages);
		trace_printf("Successfully Sent Messages to The Queue by Sender One: %d \n", SenderOne_SentMessages);
		trace_printf("Blocked Messages of Sender One %d \n\n", SenderOne_BlockedMessages);

		trace_printf("Total Sent Messages by Sender Two:  %d \n", SenderTwo_TotalSentMessages);
		trace_printf("Successfully Sent Messages to The Queue by Sender Two: %d \n", SenderTwo_SentMessages);
		trace_printf("Blocked Messages of Sender Two %d \n\n", SenderTwo_BlockedMessages);

		trace_printf("Total Sent Messages by Sender Three:  %d \n", SenderThree_TotalSentMessages);
		trace_printf("Successfully Sent Messages to The Queue by Sender Three: %d \n", SenderThree_SentMessages);
		trace_printf("Blocked Messages of Sender Three %d \n\n", SenderThree_BlockedMessages);


		//Resets Counters
		SenderOne_TotalSentMessages=0;
		SenderOne_SentMessages = 0;
		SenderOne_BlockedMessages = 0;
		SenderOne_TotalTime=0;

		SenderTwo_TotalSentMessages=0;
		SenderTwo_SentMessages = 0;
		SenderTwo_BlockedMessages = 0;
		SenderTwo_TotalTime=0;

		SenderThree_TotalSentMessages=0;
		SenderThree_SentMessages = 0;
		SenderThree_BlockedMessages = 0;
		SenderThree_TotalTime=0;

		TotalMessages=0;
		TotalSentMessages = 0;
		TotalBlockedMessages = 0;

		ReceivedMessages = 0;

		xQueueReset(Queue); // Clear the queue

		//Increment Bound index
		UpdateBounds();

		// Generate Random number between new bounds and Re-apply the Periods of each SenderTimer
		RandomPeriodOne = GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex]);
		RandomPeriodTwo = GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex]);
		RandomPeriodThree = GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex]);
		xTimerChangePeriod(xTimer_Sender1, pdMS_TO_TICKS(RandomPeriodOne), 0);
		xTimerChangePeriod(xTimer_Sender2, pdMS_TO_TICKS(RandomPeriodTwo), 0);
		xTimerChangePeriod(xTimer_Sender3, pdMS_TO_TICKS(RandomPeriodThree), 0);
		trace_printf("\n*****************************************************************************\n\n");
}

void UpdateBounds(void)
{
	BoundIndex++;
	if (BoundIndex >= BOUNDS_SIZE)
	{
		trace_puts("Game Over!\n");

		// Destroy timers
		xTimerDelete(xTimer_Sender1, 0);
		xTimerDelete(xTimer_Sender2, 0);
		xTimerDelete(xTimer_Sender3, 0);
		xTimerDelete(xTimer_Receiver, 0);
		exit(0);// Stop execution
	}

}

int GenerateRandomNumber(int num1_cpy, int num2_cpy)
{
	int random = 0;
	random = (rand()%(num2_cpy - num1_cpy + 1)) + num1_cpy;
    return random;
}


void SenderOneTask(void)
{
	TickType_t SenderOneTicks;
	char ticksOne[10]; //Stores the string version of the ticks (after conversion)
	char SentMessage1[20]; //Stores the Sent Message
	while(1)
	{
		xSemaphoreTake(SenderOne_Semphr, portMAX_DELAY);

		SenderOneTicks = xTaskGetTickCount(); //Gets Tick count since the start of the program

		//Creating the message
		itoa(SenderOneTicks, ticksOne, 10); //Converts integer (SenderOneTicks) into array of characters (ticksOne)
		strcpy(SentMessage1, "Time is ");
		strcat(SentMessage1 , ticksOne);	//Concatenates "Time is" with the Tick count

		trace_printf("[Sender 1] Time is %d \n", SenderOneTicks); //prints the message with sender ID (Easier to read in console)

		if(isQueueFull == pdTRUE) //Checks if Queue is full. if True it will get blocked. If false it will get sent
		{
			SenderOne_BlockedMessages++;
			// Read the period of the timer
			TickType_t timerPeriod = xTimerGetPeriod(xTimer_Sender1); //Total period calculator (for average calculations in PeriodicReset() Function)
			SenderOne_TotalTime+=timerPeriod * portTICK_PERIOD_MS;
			xTimerChangePeriod(xTimer_Sender1, pdMS_TO_TICKS(GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex])), 0); //updates Timer Period to a new random period

		}
		else if(xQueueSend(Queue, SentMessage1 ,0) == pdTRUE) //Checks if the message is sent successfully.
		{
			SenderOne_SentMessages++;
			TickType_t timerPeriod = xTimerGetPeriod(xTimer_Sender1); //Total period calculator (for average calculations in PeriodicReset() Function)
			SenderOne_TotalTime+=timerPeriod * portTICK_PERIOD_MS;
			xTimerChangePeriod(xTimer_Sender1, pdMS_TO_TICKS(GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex])), 0); //updates Timer Period to a new random period
		}
	}
}

void SenderTwoTask(void)
{
	TickType_t SenderTwoTicks;
	char ticksTwo[10]; //Stores the string version of the ticks (after conversion)
	char SentMessage2[20]; //Stores the Sent Message
	while(1)
	{
		xSemaphoreTake(SenderTwo_Semphr, portMAX_DELAY);

		SenderTwoTicks = xTaskGetTickCount(); //Gets Tick count since the start of the program

		//Creating the message
		itoa(SenderTwoTicks, ticksTwo, 10); //Converts integer (SenderOneTicks) into array of characters (ticksOne)
		strcpy(SentMessage2, "Time is ");
		strcat(SentMessage2 , ticksTwo);	//Concatenates "Time is" with the Tick count

		trace_printf("[Sender 2] Time is %d \n", SenderTwoTicks); //prints the message with sender ID (Easier to read in console)

		if(isQueueFull == pdTRUE) //Checks if Queue is full. if True it will get blocked. If false it will get sent
		{
			SenderTwo_BlockedMessages++;
			// Read the period of the timer
			TickType_t timerPeriod = xTimerGetPeriod(xTimer_Sender2); //Total period calculator (for average calculations in PeriodicReset() Function)
			SenderTwo_TotalTime+=timerPeriod * portTICK_PERIOD_MS;
			xTimerChangePeriod(xTimer_Sender2, pdMS_TO_TICKS(GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex])), 0); //updates Timer Period to a new random period

		}
		else if(xQueueSend(Queue, SentMessage2 ,0) == pdTRUE) //Checks if the message is sent successfully.
		{
			SenderTwo_SentMessages++;
			TickType_t timerPeriod = xTimerGetPeriod(xTimer_Sender2); //Total period calculator (for average calculations in PeriodicReset() Function)
			SenderTwo_TotalTime+=timerPeriod * portTICK_PERIOD_MS;
			xTimerChangePeriod(xTimer_Sender2, pdMS_TO_TICKS(GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex])), 0); //updates Timer Period to a new random period
		}
	}
}

void SenderThreeTask(void)
{
	TickType_t SenderThreeTicks;
	char ticksThree[10]; //Stores the string version of the ticks (after conversion)
	char SentMessage3[20]; //Stores the Sent Message
	while(1)
	{
		xSemaphoreTake(SenderThree_Semphr, portMAX_DELAY);

		SenderThreeTicks = xTaskGetTickCount(); //Gets Tick count since the start of the program

		//Creating the message
		itoa(SenderThreeTicks, ticksThree, 10); //Converts integer (SenderOneTicks) into array of characters (ticksOne)
		strcpy(SentMessage3, "Time is ");
		strcat(SentMessage3 , ticksThree);	//Concatenates "Time is" with the Tick count

		trace_printf("[Sender 3] Time is %d \n", SenderThreeTicks); //prints the message with sender ID (Easier to read in console)

		if(isQueueFull == pdTRUE) //Checks if Queue is full. if True it will get blocked. If false it will get sent
		{
			SenderTwo_BlockedMessages++;
			// Read the period of the timer
			TickType_t timerPeriod = xTimerGetPeriod(xTimer_Sender3); //Total period calculator (for average calculations in PeriodicReset() Function)
			SenderThree_TotalTime+=timerPeriod * portTICK_PERIOD_MS;
			xTimerChangePeriod(xTimer_Sender3, pdMS_TO_TICKS(GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex])), 0); //updates Timer Period to a new random period

		}
		else if(xQueueSend(Queue, SentMessage3 ,0) == pdTRUE) //Checks if the message is sent successfully.
		{
			SenderThree_SentMessages++;
			TickType_t timerPeriod = xTimerGetPeriod(xTimer_Sender3); //Total period calculator (for average calculations in PeriodicReset() Function)
			SenderThree_TotalTime+=timerPeriod * portTICK_PERIOD_MS;
			xTimerChangePeriod(xTimer_Sender3, pdMS_TO_TICKS(GenerateRandomNumber(LowerBound[BoundIndex], UpperBound[BoundIndex])), 0); //updates Timer Period to a new random period
		}
	}
}

void ReceiverTask(void)
{
	char receivedMessage[20];

	while (1)
	{
		// Wait for the semaphore to be given by the timer callback
		xSemaphoreTake(Receiver_Semphr, portMAX_DELAY);

	    // Try to receive a message from the queue
	    if (xQueueReceive(Queue, receivedMessage, 0) == pdPASS)
	    {
	    	ReceivedMessages++; // Increment received counter if a message is received
	    }
	 }
}


static void ReceiverTimer( TimerHandle_t xTimer )
{
	xSemaphoreGive(Receiver_Semphr); // Unblock the receiver task
	//If The numebr of received messages is greater than the set number of messages Call the PeriodicReset function
	if (ReceivedMessages >= TOTAL_MESSAGES)
	{
	   PeriodicReset();
	}

}

static void SenderOneTimer( TimerHandle_t xTimer )
{

	xSemaphoreGive(SenderOne_Semphr);//Unblock the senderOne Task
	isQueueFull = xQueueIsQueueFullFromISR(Queue);//Check if Queue is full
}

static void SenderTwoTimer( TimerHandle_t xTimer )
{

	xSemaphoreGive(SenderTwo_Semphr);//Unblock the senderOne Task
	isQueueFull = xQueueIsQueueFullFromISR(Queue);//Check if Queue is full
}

static void SenderThreeTimer( TimerHandle_t xTimer )
{

	xSemaphoreGive(SenderThree_Semphr);//Unblock the senderOne Task
	isQueueFull = xQueueIsQueueFullFromISR(Queue);//Check if Queue is full

}

/************************************************************************************************************************/
void vApplicationMallocFailedHook( void )
{
	/* Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configconfigCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
volatile size_t xFreeStackSpace;

	/* This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amout of FreeRTOS heap that
	remains unallocated. */
	xFreeStackSpace = xPortGetFreeHeapSize();

	if( xFreeStackSpace > 100 )
	{
		/* By now, the kernel has allocated everything it is going to, so
		if there is a lot of heap remaining unallocated then
		the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
		reduced accordingly. */
	}
}

void vApplicationTickHook(void) {
}

StaticTask_t xIdleTaskTCB CCM_RAM;
StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE] CCM_RAM;

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize) {
  /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
  state will be stored. */
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

  /* Pass out the array that will be used as the Idle task's stack. */
  *ppxIdleTaskStackBuffer = uxIdleTaskStack;

  /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
  Note that, as the array is necessarily of type StackType_t,
  configMINIMAL_STACK_SIZE is specified in words, not bytes. */
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

static StaticTask_t xTimerTaskTCB CCM_RAM;
static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH] CCM_RAM;

/* configUSE_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
application must provide an implementation of vApplicationGetTimerTaskMemory()
to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize) {
  *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
  *ppxTimerTaskStackBuffer = uxTimerTaskStack;
  *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}


