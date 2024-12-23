/*
 * Copyright (c) 2015-2020, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== gpiointerrupt.c ========
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

/* Driver Header files */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/Timer.h>
#include <ti/drivers/UART.h>

/* Driver configuration */
#include "ti_drivers_config.h"

/* Definitions */
#define DISPLAY(x) UART_write(uart, &output, x)
#define timerPeriod 100
#define numTasks 3
#define checkButtonPeriod 200
#define checkTemperaturePeriod 500
#define updateHeatModeAndServerPeriod 1000

/*
 *  ======== Task Type ========
 *
 *  Defines structure for the task type.
 */
typedef struct task {
    int state;                    // Current state of the task
    unsigned long period;         // Rate at which the task should tick
    unsigned long elapsedTime;    // Time since task's previous tick
    int (*tickFunction)(int);     // Function to call for task's tick
} task;

/*
 *  ======== Driver Handles ========
 */
I2C_Handle i2c;         // I2C driver handle
Timer_Handle timer0;    // Timer driver handle
UART_Handle uart;       // UART driver handle

/*
 *  ======== Global Variables ========
 */
// UART global variables
char output[64];
int bytesToSend;

// I2C global variables
static const struct
{
    uint8_t address;
    uint8_t resultReg;
    char *id;
}
sensors[3] = {
    { 0x48, 0x0000, "11X" },
    { 0x49, 0x0000, "116" },
    { 0x41, 0x0001, "006" }
};
uint8_t txBuffer[1];
uint8_t rxBuffer[2];
I2C_Transaction i2cTransaction;

// Timer global variables
volatile unsigned char TimerFlag = 0;

// Thermostat global variables
enum BUTTON_STATES {INCREASE_TEMPERATURE, DECREASE_TEMPERATURE, BUTTON_INIT} BUTTON_STATE;  // States for setting which button was pressed.
enum TEMPERATURE_SENSOR_STATES {READ_TEMPERATURE, TEMPERATURE_SENSOR_INIT};                 // States for the temperature sensor.
enum HEATING_STATES {HEAT_OFF, HEAT_ON, HEAT_INIT};                                         // States for the heating (heat/led off or on).
int16_t ambientTemperature = 0;                                                             // Initialize temperature to 0 (will be updated by sensor reading).
int16_t setPoint = 20;                                                                      // Initialize set-point for thermostat at 20�C (68�F).
int seconds = 0;                                                                            // Initialize seconds to 0 (will be updated by timer).

/*
 *  ======== Callback ========
 */
// GPIO button callback function to increase the thermostat set-point.
void gpioIncreaseTemperatureCallback(uint_least8_t index)
{
    BUTTON_STATE = INCREASE_TEMPERATURE;
}

// GPIO button callback function to decrease the thermostat set-point.
void gpioDecreaseTemperatureCallback(uint_least8_t index)
{
    BUTTON_STATE = DECREASE_TEMPERATURE;
}

// Timer callback
void timerCallback(Timer_Handle myHandle, int_fast16_t status)
{
    TimerFlag = 1;  // Set flag to 1 to indicate timer is running.
}

/*
 *  ======== Initializations ========
 */
// Initialize UART
void initUART(void)
{
    UART_Params uartParams;

    // Init the driver
    UART_init();

    // Configure the driver
    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_BINARY;
    uartParams.readDataMode = UART_DATA_BINARY;
    uartParams.readReturnMode = UART_RETURN_FULL;
    uartParams.baudRate = 115200;

    // Open the driver
    uart = UART_open(CONFIG_UART_0, &uartParams);
    if (uart == NULL)
    {
        /* UART_open() failed */
        while (1);
    }
}

// Initialize I2C
void initI2C(void)
{
    int8_t i, found;
    I2C_Params i2cParams;

    DISPLAY(snprintf(output, 64, "Initializing I2C Driver - "));

    // Init the driver
    I2C_init();

    // Configure the driver
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;

    // Open the driver
    i2c = I2C_open(CONFIG_I2C_0, &i2cParams);
    if (i2c == NULL)
    {
        DISPLAY(snprintf(output, 64, "Failed\n\r"));
        while (1);
    }

    DISPLAY(snprintf(output, 32, "Passed\n\r"));

    // Boards were shipped with different sensors.
    // Welcome to the world of embedded systems.
    // Try to determine which sensor we have.
    // Scan through the possible sensor addresses.

    /* Common I2C transaction setup */
    i2cTransaction.writeBuf = txBuffer;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf = rxBuffer;
    i2cTransaction.readCount = 0;

    found = false;
    for (i=0; i<3; ++i)
    {
         i2cTransaction.slaveAddress = sensors[i].address;
         txBuffer[0] = sensors[i].resultReg;

         DISPLAY(snprintf(output, 64, "Is this %s? ", sensors[i].id));
         if (I2C_transfer(i2c, &i2cTransaction))
         {
             DISPLAY(snprintf(output, 64, "Found\n\r"));
             found = true;
             break;
         }
         DISPLAY(snprintf(output, 64, "No\n\r"));
    }

    if(found)
    {
        DISPLAY(snprintf(output, 64, "Detected TMP%s I2C address: %x\n\r", sensors[i].id, i2cTransaction.slaveAddress));
    }
    else
    {
        DISPLAY(snprintf(output, 64, "Temperature sensor not found, contact professor\n\r"));
    }
}

// Initialize GPIO
void initGPIO(void)
{
    /* Call driver init functions for GPIO */
    GPIO_init();

    /* Configure the LED and button pins */
    GPIO_setConfig(CONFIG_GPIO_LED_0, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    GPIO_setConfig(CONFIG_GPIO_BUTTON_0, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);

    /* Start with LED off */
    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);

    /* Install Button callback */
    GPIO_setCallback(CONFIG_GPIO_BUTTON_0, gpioIncreaseTemperatureCallback);

    /* Enable interrupts */
    GPIO_enableInt(CONFIG_GPIO_BUTTON_0);

    /*
     *  If more than one input pin is available for your device, interrupts
     *  will be enabled on CONFIG_GPIO_BUTTON1.
     */
    if (CONFIG_GPIO_BUTTON_0 != CONFIG_GPIO_BUTTON_1) {
        /* Configure BUTTON1 pin */
        GPIO_setConfig(CONFIG_GPIO_BUTTON_1, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);

        /* Install Button callback */
        GPIO_setCallback(CONFIG_GPIO_BUTTON_1, gpioDecreaseTemperatureCallback);
        GPIO_enableInt(CONFIG_GPIO_BUTTON_1);
    }

    BUTTON_STATE = BUTTON_INIT;
}

// Initialize Timer
void initTimer(void)
{
    Timer_Params params;

    // Init the driver
    Timer_init();

    // Configure the driver
    Timer_Params_init(&params);
    params.period = 100000;                         // Set period to 1/10th of 1 second.
    params.periodUnits = Timer_PERIOD_US;           // Period specified in micro seconds
    params.timerMode = Timer_CONTINUOUS_CALLBACK;   // Timer runs continuously.
    params.timerCallback = timerCallback;           // Calls timerCallback method for timer callback.

    // Open the driver
    timer0 = Timer_open(CONFIG_TIMER_0, &params);
    if (timer0 == NULL)
    {
        /* Failed to initialized timer */
        while (1) {}
    }
    if (Timer_start(timer0) == Timer_STATUS_ERROR)
    {
        /* Failed to start timer */
        while (1) {}
    }
}

/*
 *  ======== adjustSetPointTemperature ========
 *
 *  Check the current state of BUTTON_PRESSED to determine if the
 *  increase or decrease temperature button has been pressed and
 *  then resets BUTTON_PRESSED.
 */
int adjustSetPointTemperature(int state)
{
    // Checks if desired temperature has been adjusted.
    switch (state)
    {
        case INCREASE_TEMPERATURE:
            if (setPoint < 99)      // Ensure temperature is not set to above 99�C.
            {
                setPoint++;
            }
            BUTTON_STATE = BUTTON_INIT;
            break;
        case DECREASE_TEMPERATURE:
            if (setPoint > 0)       // Ensure temperature is not set lower than 0�C.
            {
                setPoint--;
            }
            BUTTON_STATE = BUTTON_INIT;
            break;
    }
    state = BUTTON_STATE;           // Reset the state of BUTTON_PRESSED after reading.

    return state;
}

/*
 *  ======== readTemp ========
 *  This function reads the current temperature from the sensor using I2C
 *  and returns the temperature value as a 16-bit integer.
 */
int16_t readTemp(void)
{
    int16_t temperature = 0;  // Initialize temperature to 0
    i2cTransaction.readCount = 2;  // Expect to read 2 bytes from the sensor

    // Perform I2C transfer to read temperature data
    if (I2C_transfer(i2c, &i2cTransaction))
    {
        /*
        * Extract the temperature value from the received data.
        * The high byte is shifted left by 8 bits and combined with the low byte.
        * The resulting value is multiplied by 0.0078125 to get degrees Celsius.
        */
        temperature = (rxBuffer[0] << 8) | (rxBuffer[1]);
        temperature *= 0.0078125;

        /*
        * Check if the most significant bit (MSB) is set, indicating a negative value.
        * If set, perform sign extension to handle 2's complement for negative temperatures.
        */
        if (rxBuffer[0] & 0x80)
        {
            temperature |= 0xF000;  // Sign extend the temperature value
        }
    }
    else
    {
        // Display error message if I2C transfer fails
        DISPLAY(snprintf(output, 64, "Error reading temperature sensor (%d)\n\r", i2cTransaction.status));
        DISPLAY(snprintf(output, 64, "Please power cycle your board by unplugging USB and plugging back in.\n\r"));
    }

    return temperature;  // Return the temperature value
}

/*
 *  ======== getAmbientTemperature ========
 *  This function checks the current state and determines if the temperature
 *  should be read from the sensor. It updates the ambient temperature accordingly.
 */
int getAmbientTemperature(int state)
{
    switch (state)
    {
        case TEMPERATURE_SENSOR_INIT:
            state = READ_TEMPERATURE;  // Transition to read temperature state
            break;
        case READ_TEMPERATURE:
            ambientTemperature = readTemp();  // Read and update ambient temperature
            break;
    }

    return state;  // Return updated state
}

/*
 *  ======== setHeatMode ========
 *  This function compares the ambient temperature with the set-point temperature.
 *  It controls heating by turning an LED on or off based on the comparison.
 *  Additionally, it reports the current state to the server.
 */
int setHeatMode(int state)
{
    if (seconds != 0)
    {
        // If the ambient temperature is below the set-point, turn on heating (LED on)
        if (ambientTemperature < setPoint)
        {
            GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON);
            state = HEAT_ON;
        }
        else  // Otherwise, turn off heating (LED off)
        {
            GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);
            state = HEAT_OFF;
        }

        // Send status report to the server with temperature, set point, and state
        DISPLAY(snprintf(output,
                             64,
                             "<%02d,%02d,%d,%04d>\n\r",
                             ambientTemperature,
                             setPoint,
                             state,
                             seconds));
    }

    seconds++;  // Increment time counter

    return state;  // Return updated state
}

/*
 *  ======== mainThread ========
 *  The main application thread that initializes drivers and schedules tasks.
 *  Tasks are executed periodically to check temperature, adjust settings, and update the server.
 */
void *mainThread(void *arg0)
{
    // Create an array of tasks for the system
    task tasks[numTasks] = {
        // Task 1 - Button state check and set-point adjustment
        {
            .state = BUTTON_INIT,
            .period = checkButtonPeriod,
            .elapsedTime = checkButtonPeriod,
            .tickFunction = &adjustSetPointTemperature
        },
        // Task 2 - Read temperature from sensor
        {
            .state = TEMPERATURE_SENSOR_INIT,
            .period = checkTemperaturePeriod,
            .elapsedTime = checkTemperaturePeriod,
            .tickFunction = &getAmbientTemperature
        },
        // Task 3 - Update heat mode and report to server
        {
            .state = HEAT_INIT,
            .period = updateHeatModeAndServerPeriod,
            .elapsedTime = updateHeatModeAndServerPeriod,
            .tickFunction = &setHeatMode
        }
    };

    // Initialize hardware drivers for UART, I2C, GPIO, and Timer
    initUART();
    initI2C();
    initGPIO();
    initTimer();

    // Infinite loop to continuously check and execute tasks
    while (1)
    {
        unsigned int i = 0;
        for (i = 0; i < numTasks; ++i)
        {
            // Execute task if its elapsed time meets the required period
            if ( tasks[i].elapsedTime >= tasks[i].period )
            {
                tasks[i].state = tasks[i].tickFunction(tasks[i].state);  // Call task function
                tasks[i].elapsedTime = 0;  // Reset elapsed time after execution
             }
             tasks[i].elapsedTime += timerPeriod;  // Increment elapsed time for each task
        }

        // Wait for the timer period to expire
        while(!TimerFlag){}
        TimerFlag = 0;  // Reset the timer flag
    }

    return (NULL);  // Return NULL to indicate the thread has completed (not expected to happen)
}
