/**
 * @file log_reader_task.h
 * @brief Log Reader Task for Core 0
 *
 * @author CCU Multicore Project
 * @date 2026-03-19
 */

#ifndef LOG_READER_TASK_H_
#define LOG_READER_TASK_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create log reader task
 *
 * Creates a FreeRTOS task that periodically reads logs from shared memory
 * written by Core 1 and outputs them via UART.
 *
 * @return 0 on success, -1 on error
 */
int log_reader_task_create(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_READER_TASK_H_ */
