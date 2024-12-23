/*
 * Copyright (c) 2020, Texas Instruments Incorporated
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
 *  ======== uart2echo.c ========
 */
#include <stdint.h>
#include <stddef.h>

/* Driver Header files */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/UART2.h>

/* Driver configuration */
#include "ti_drivers_config.h"

/* State machine states */
typedef enum {
    STATE_IDLE, // Waiting for the first character ('O')
    STATE_O,    // Received 'O', waiting for 'N' or 'F'
    STATE_ON,   // "ON" command recognized
    STATE_F,    // Received 'F', waiting for another 'F'
    STATE_OFF   // "OFF" command recognized
} State;

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    char input;                        // Single-byte buffer for UART input
    unsigned char state = STATE_IDLE;  // State machine state (1 byte of RAM)
    const char prompt[] = "Type 'ON' or 'OFF':\r\n";
    UART2_Handle uart;
    UART2_Params uartParams;
    size_t bytesRead;
    size_t bytesWritten = 0;
    uint32_t status;

    /* Call driver init functions */
    GPIO_init();

    /* Configure the LED pin */
    GPIO_setConfig(CONFIG_GPIO_LED_0, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);

    /* Create a UART where the default read and write mode is BLOCKING */
    UART2_Params_init(&uartParams);
    uartParams.baudRate = 115200;

    uart = UART2_open(CONFIG_UART2_0, &uartParams);
    if (uart == NULL) {
        /* UART2_open() failed */
        while (1) {}
    }

    /* prompt user */
    UART2_write(uart, prompt, sizeof(prompt), &bytesWritten); // Display user instruction

    /* main loop*/
    while (1) {
        // Read a single character from the UART
        bytesRead = 0;
        status = UART2_read(uart, &input, 1, &bytesRead);

        if (status != UART2_STATUS_SUCCESS) {
            /* UART2_read() failed */
            while (1) {}
        }

        // State machine logic
        switch (state) {
            case STATE_IDLE:
                // Initial state: waiting for 'O' to start a command
                if (input == 'O') {
                    state = STATE_O;  // Transition to waiting for "ON" or "OFF"
                } else {
                    state = STATE_IDLE;  // Stay idle on unexpected input
                }
                break;

            case STATE_O:
                // Received 'O', now waiting for 'N' or 'F'
                if (input == 'N') {
                    // "ON" detected, turn on the LED
                    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON);
                    UART2_write(uart, "LED ON\r\n", 8, &bytesWritten);
                    state = STATE_IDLE;  // Return to idle state
                } else if (input == 'F') {
                    state = STATE_F;  // Possible "OFF" command
                } else {
                    state = STATE_IDLE;  // Reset state
                }
                break;

            case STATE_F:
                // Received 'F', waiting for another 'F' to confirm "OFF"
                if (input == 'F') {
                    // "OFF" detected, turn off the LED
                    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);
                    UART2_write(uart, "LED OFF\r\n", 9, &bytesWritten);
                    state = STATE_IDLE;  // Return to idle state
                } else {
                    state = STATE_IDLE;  // Reset state
                }
                break;

            default:
                state = STATE_IDLE;  // Default state reset
                break;
        }
    }
}

/*----------------------------------------------------------------------------------------------*/

/*
 *       bytesWritten = 0;
 *       while (bytesWritten == 0)
 *       {
 *           status = UART2_write(uart, &input, 1, &bytesWritten);
 *
 *          if (status != UART2_STATUS_SUCCESS)
 *          {
 *                //UART2_write() failed
 *              while (1) {}
 *          }
 *      }
 *  }
 *}
 */
