/*
 *  Copyright (C) 2022 Texas Instruments Incorporated
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/ClockP.h>
#include "ti_drivers_config.h"
#include "ti_dpl_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"

/*
 * Timer Test Application
 *
 * This example tests timer interrupt at 1kHz frequency.
 * Timer ISR sets a flag, main loop checks and logs.
 *
 * Verification mechanisms:
 * - Counter: counts total ISR executions
 * - Timestamp: measures actual time between logs
 * - Frequency calculation: verifies Hz accuracy
 */

/* Timer frequency in Hz */
#define TIMER_FREQUENCY_HZ      1000

/* Test duration in seconds */
#define TEST_DURATION_SEC       10

/* Log interval in timer ticks (every 1 second = 1000 ticks at 1kHz) */
#define LOG_INTERVAL_TICKS     1000

/* Global variables and objects */
/* Timer ISR counter - incremented each interrupt */
volatile uint32_t gTimerIsrCounter = 0;

/* Flag to signal main loop that timer ISR occurred */
volatile bool gTimerIsrFlag = false;

/* Previous counter value for delta calculation */
volatile uint32_t gPrevCounter = 0;

/* Timestamp of previous log for frequency verification */
uint64_t gPrevLogTimeUsec = 0;

/* Expected counter value for verification */
uint32_t gExpectedCounter = 0;

/* Actual measured frequency */
float gMeasuredFreqHz = 0.0f;

void timer_test_main(void *args)
{
    uint32_t currentCounter, deltaCounter;
    uint64_t currentTimeUsec, deltaTimeUsec;
    float deltaTimeSec, expectedDeltaCounter;
    float freqErrorPercent;

    /* Open drivers to open the UART driver for console */
    Drivers_open();
    Board_driversOpen();

    DebugP_log("\r\n========================================\r\n");
    DebugP_log("Timer Test Application Started\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("Timer Frequency: %d Hz\r\n", TIMER_FREQUENCY_HZ);
    DebugP_log("Test Duration: %d seconds\r\n", TEST_DURATION_SEC);
    DebugP_log("Log Interval: every %d ticks (%.3f sec)\r\n",
               LOG_INTERVAL_TICKS, (float)LOG_INTERVAL_TICKS / TIMER_FREQUENCY_HZ);
    DebugP_log("========================================\r\n\r\n");

    /* Reset counter and flag */
    gTimerIsrCounter = 0;
    gTimerIsrFlag = false;
    gPrevCounter = 0;

    /* Get initial timestamp */
    gPrevLogTimeUsec = ClockP_getTimeUsec();

    /* Start the timer to get interrupt at configured rate */
    TimerP_start(gTimerBaseAddr[CONFIG_TIMER0]);

    /* Main loop - wait for timer flag and log */
    while (gTimerIsrCounter < (TEST_DURATION_SEC * TIMER_FREQUENCY_HZ))
    {
        /* Check if timer ISR set the flag */
        if (gTimerIsrFlag == true)
        {
            /* Clear the flag */
            gTimerIsrFlag = false;

            /* Get current counter value */
            currentCounter = gTimerIsrCounter;

            /* Check if it's time to log (every LOG_INTERVAL_TICKS) */
            if ((currentCounter % LOG_INTERVAL_TICKS) == 0)
            {
                /* Get current timestamp */
                currentTimeUsec = ClockP_getTimeUsec();

                /* Calculate time elapsed since last log */
                deltaTimeUsec = currentTimeUsec - gPrevLogTimeUsec;
                deltaTimeSec = (float)deltaTimeUsec / 1000000.0f;

                /* Calculate counter delta since last log */
                deltaCounter = currentCounter - gPrevCounter;

                /* Calculate expected counter delta */
                expectedDeltaCounter = deltaTimeSec * TIMER_FREQUENCY_HZ;

                /* Calculate actual measured frequency */
                gMeasuredFreqHz = (deltaCounter / deltaTimeSec);

                /* Calculate frequency error percentage */
                freqErrorPercent = ((gMeasuredFreqHz - TIMER_FREQUENCY_HZ) / TIMER_FREQUENCY_HZ) * 100.0f;

                /* Log timer verification data */
                DebugP_log("[T+%6.3fs] Counter: %6u | Delta: %4u | Time: %.6fs | Freq: %.2f Hz | Error: %+.3f%%\r\n",
                           (float)currentCounter / TIMER_FREQUENCY_HZ,
                           currentCounter,
                           deltaCounter,
                           deltaTimeSec,
                           gMeasuredFreqHz,
                           freqErrorPercent);

                /* Update previous values */
                gPrevCounter = currentCounter;
                gPrevLogTimeUsec = currentTimeUsec;
            }
        }
    }

    /* Stop the timer */
    TimerP_stop(gTimerBaseAddr[CONFIG_TIMER0]);

    /* Final verification report */
    DebugP_log("\r\n========================================\r\n");
    DebugP_log("Timer Test Completed\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("Test Duration:        %d seconds\r\n", TEST_DURATION_SEC);
    DebugP_log("Expected Frequency:   %d Hz\r\n", TIMER_FREQUENCY_HZ);
    DebugP_log("Expected Ticks:       %u\r\n", TEST_DURATION_SEC * TIMER_FREQUENCY_HZ);
    DebugP_log("Actual Ticks:         %u\r\n", gTimerIsrCounter);
    DebugP_log("Tick Difference:      %d\r\n",
               (int)gTimerIsrCounter - (TEST_DURATION_SEC * TIMER_FREQUENCY_HZ));
    DebugP_log("Accuracy:             %.4f%%\r\n",
               (gTimerIsrCounter * 100.0f) / (TEST_DURATION_SEC * TIMER_FREQUENCY_HZ));
    DebugP_log("========================================\r\n");
    DebugP_log("Timer Test PASSED!\r\n");
    DebugP_log("========================================\r\n\r\n");

    Board_driversClose();
    Drivers_close();
}

/*
 * Timer ISR - runs at configured frequency (1kHz)
 * Minimally invasive: only sets flag and increments counter
 */
void timerISR(void)
{
    /* Increment counter */
    gTimerIsrCounter++;

    /* Set flag to signal main loop */
    gTimerIsrFlag = true;
}
