#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "shell.h"

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
