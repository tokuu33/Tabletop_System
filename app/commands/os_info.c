#include <stdint.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "shell.h"
#include "usart.h"

/* Maximum number of tasks tracked per snapshot.  The system has ~8 tasks;
 * 16 gives comfortable headroom for future additions. */
#define OS_TOP_MAX_TASKS  16

/* Return the one-letter state code used in the State column. */
static char state_char(eTaskState s)
{
    switch (s) {
        case eRunning:   return 'X';
        case eReady:     return 'R';
        case eBlocked:   return 'B';
        case eSuspended: return 'S';
        case eDeleted:   return 'D';
        default:         return '?';
    }
}

/**
 * @brief Show FreeRTOS task list and runtime statistics.
 *
 * Uses uxTaskGetSystemState() directly so that:
 *   - all tasks are captured regardless of output-buffer size,
 *   - Abs Time is shown in milliseconds (DWT cycles / (SystemCoreClock/1000)),
 *   - the % Time column is right-aligned with a fixed field width, and
 *   - state codes are explained in a legend.
 *
 * Requires the following macros to be enabled in FreeRTOSConfig.h:
 *   configUSE_TRACE_FACILITY             1
 *   configGENERATE_RUN_TIME_STATS        1
 */
int cmd_os_info(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();

    /* Allocate exactly as many TaskStatus_t slots as there are live tasks. */
    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = (TaskStatus_t *)pvPortMalloc(n * sizeof(TaskStatus_t));
    if (tasks == NULL) {
        shellPrint(shell, "os_info: out of memory\n");
        return -1;
    }

    uint32_t total_runtime = 0;
    n = uxTaskGetSystemState(tasks, n, &total_runtime);

    /* ------------------------------------------------------------------ */
    /* Task List                                                            */
    /* ------------------------------------------------------------------ */
    shellPrint(shell, "=== Task List ===\n");
    shellPrint(shell, "%-16s  %s  %4s  %5s  %3s\n",
               "Name", "St", "Prio", "HWM", "Num");
    shellPrint(shell, "%-16s  %s  %4s  %5s  %3s\n",
               "----------------", "--", "----", "-----", "---");

    for (UBaseType_t i = 0; i < n; i++) {
        shellPrint(shell, "%-16s  %c   %4u  %5u  %3u\n",
                   tasks[i].pcTaskName,
                   state_char(tasks[i].eCurrentState),
                   (unsigned int)tasks[i].uxCurrentPriority,
                   (unsigned int)tasks[i].usStackHighWaterMark,
                   (unsigned int)tasks[i].xTaskNumber);
    }

    shellPrint(shell, "\n"
               "St legend: X=Running  R=Ready  B=Blocked  S=Suspended  D=Deleted\n"
               "HWM: High Water Mark - minimum free stack space (words); lower = less margin\n"
               "Note: tasks that call vTaskDelete() (e.g. main_init) finish and are removed\n"
               "      before this command runs, so they do not appear in the list above.\n");

    /* ------------------------------------------------------------------ */
    /* Runtime Stats                                                        */
    /* ------------------------------------------------------------------ */
    shellPrint(shell, "\n=== Runtime Stats ===\n");

    /* configCPU_CLOCK_HZ == SystemCoreClock (declared in FreeRTOSConfig.h).
     * Dividing by 1000 gives the number of DWT cycles per millisecond. */
    uint32_t cycles_per_ms = configCPU_CLOCK_HZ / 1000UL;

    shellPrint(shell, "%-16s  %12s  %6s\n", "Name", "Abs Time(ms)", "% Time");
    shellPrint(shell, "%-16s  %12s  %6s\n", "----------------", "------------", "------");

    for (UBaseType_t i = 0; i < n; i++) {
        uint32_t abs_ms = (cycles_per_ms > 0) ? (tasks[i].ulRunTimeCounter / cycles_per_ms) : 0;
        uint32_t pct    = (total_runtime > 0)
                          ? (tasks[i].ulRunTimeCounter * 100UL / total_runtime)
                          : 0;

        if (pct == 0 && tasks[i].ulRunTimeCounter > 0) {
            /* Less than 1 % but the task has consumed some CPU time. */
            shellPrint(shell, "%-16s  %12lu  %5s%%\n",
                       tasks[i].pcTaskName, (unsigned long)abs_ms, "<1");
        } else {
            shellPrint(shell, "%-16s  %12lu  %5lu%%\n",
                       tasks[i].pcTaskName, (unsigned long)abs_ms, (unsigned long)pct);
        }
    }

    vPortFree(tasks);
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 os_info, cmd_os_info, Show FreeRTOS task list and runtime stats);

/**
 * @brief Continuously monitor FreeRTOS task statistics (like top/htop).
 *
 * Usage: os_top [interval_ms]
 *   interval_ms  Refresh interval in milliseconds (default: 2000, min: 500)
 *
 * Each refresh shows:
 *   - Task List: current state, priority, HWM, task number
 *   - CPU Usage: CPU time consumed during the last interval (delta %)
 *     which is more informative than the cumulative % shown by os_info.
 *
 * The display updates in-place using ANSI escape codes.
 * Press any key to stop.
 */
int cmd_os_top(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();

    /* Parse optional interval argument. */
    uint32_t interval_ms = 2000;
    if (argc >= 2) {
        /* Use atoi() so that leading whitespace (which letter-shell may leave
         * at the start of a token) is skipped, consistent with how the rest
         * of the codebase (e.g. ui_str_test.c) parses numeric arguments. */
        int v = atoi(argv[1]);
        if (v >= 500) {
            interval_ms = (uint32_t)v;
        } else {
            shellPrint(shell, "os_top: interval must be >= 500 ms\n");
            return -1;
        }
    }

    /* Re-entrancy guard: the static snapshot arrays are shared state, so only
     * one invocation of os_top may run at a time. */
    static volatile uint8_t running = 0;
    if (running) {
        shellPrint(shell, "os_top: already running\n");
        return -1;
    }
    running = 1;

    /* Allocate two fixed-size snapshot buffers to avoid heap fragmentation. */
    static TaskStatus_t snap_a[OS_TOP_MAX_TASKS];
    static TaskStatus_t snap_b[OS_TOP_MAX_TASKS];

    uint32_t total_a = 0, total_b = 0;
    UBaseType_t n_a, n_b;

    /* Take initial snapshot before the first delay. */
    n_a = uxTaskGetSystemState(snap_a, OS_TOP_MAX_TASKS, &total_a);

    /* Clear screen once so the first refresh lands at a known position. */
    shellPrint(shell, "\033[2J\033[H");

    while (1) {
        /* Check for keypress at the top of the loop for immediate response. */
        char ch;
        if (usart_try_read(&ch) > 0) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(interval_ms));

        /* Check again after the delay to catch keypresses that arrived
         * while the task was sleeping. */
        if (usart_try_read(&ch) > 0) {
            break;
        }

        /* Take current snapshot. */
        n_b = uxTaskGetSystemState(snap_b, OS_TOP_MAX_TASKS, &total_b);

        uint32_t total_delta  = total_b - total_a;
        uint32_t cycles_per_ms = configCPU_CLOCK_HZ / 1000UL;

        /* Move cursor to top-left and clear to end of screen for in-place update. */
        shellPrint(shell, "\033[H\033[J");

        shellPrint(shell, "=== os_top  interval: %lums | press any key to stop ===\n",
                   (unsigned long)interval_ms);

        /* ---- Task List ---- */
        shellPrint(shell, "\n--- Task List ---\n");
        shellPrint(shell, "%-16s  %s  %4s  %5s  %3s\n",
                   "Name", "St", "Prio", "HWM", "Num");
        shellPrint(shell, "%-16s  %s  %4s  %5s  %3s\n",
                   "----------------", "--", "----", "-----", "---");

        for (UBaseType_t i = 0; i < n_b; i++) {
            shellPrint(shell, "%-16s  %c   %4u  %5u  %3u\n",
                       snap_b[i].pcTaskName,
                       state_char(snap_b[i].eCurrentState),
                       (unsigned int)snap_b[i].uxCurrentPriority,
                       (unsigned int)snap_b[i].usStackHighWaterMark,
                       (unsigned int)snap_b[i].xTaskNumber);
        }

        shellPrint(shell, "\nSt: X=Running  R=Ready  B=Blocked  S=Suspended\n");

        /* ---- CPU Usage (delta over the last interval) ---- */
        shellPrint(shell, "\n--- CPU Usage (last %lums) ---\n",
                   (unsigned long)interval_ms);
        shellPrint(shell, "%-16s  %12s  %6s\n", "Name", "Delta(ms)", "% CPU");
        shellPrint(shell, "%-16s  %12s  %6s\n", "----------------", "---------", "------");

        for (UBaseType_t i = 0; i < n_b; i++) {
            /* Find the matching task in the previous snapshot by task number. */
            uint32_t counter_a = 0;
            for (UBaseType_t j = 0; j < n_a; j++) {
                if (snap_a[j].xTaskNumber == snap_b[i].xTaskNumber) {
                    counter_a = snap_a[j].ulRunTimeCounter;
                    break;
                }
            }

            /* Unsigned subtraction handles 32-bit counter wrap-around correctly. */
            uint32_t delta_cycles = snap_b[i].ulRunTimeCounter - counter_a;
            uint32_t delta_ms     = (cycles_per_ms > 0) ? (delta_cycles / cycles_per_ms) : 0;
            uint32_t pct          = (total_delta > 0)
                                    ? (delta_cycles * 100UL / total_delta)
                                    : 0;

            if (pct == 0 && delta_cycles > 0) {
                shellPrint(shell, "%-16s  %12lu  %5s%%\n",
                           snap_b[i].pcTaskName, (unsigned long)delta_ms, "<1");
            } else {
                shellPrint(shell, "%-16s  %12lu  %5lu%%\n",
                           snap_b[i].pcTaskName, (unsigned long)delta_ms, (unsigned long)pct);
            }
        }

        /* Roll the current snapshot into the 'previous' slot for next round. */
        for (UBaseType_t i = 0; i < n_b; i++) {
            snap_a[i] = snap_b[i];
        }
        total_a = total_b;
        n_a     = n_b;
    }

    running = 0;
    shellPrint(shell, "\033[2J\033[H");
    shellPrint(shell, "os_top stopped.\n");
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 os_top, cmd_os_top, Continuously monitor FreeRTOS tasks (press any key to stop));
