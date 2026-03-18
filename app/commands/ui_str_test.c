#include <stdint.h>
#include <stdlib.h>
#include "shell.h"
#include "ui.h"
#include "font.h"

int cmd_ui_test_str(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();

    if (argc < 4)
    {
        shellPrint(shell, "Usage: ui_test_str <x> <y> <string>\n");
        return -1;
    }

    int x = atoi(argv[1]);
    int y = atoi(argv[2]);
    const char *str = argv[3];

    ui_write_string((uint16_t)x, (uint16_t)y, str,
                    mkcolor(255, 255, 255), mkcolor(0, 0, 0),
                    &font20_maple_bold);

    shellPrint(shell, "[UI] Injected: (%d,%d) \"%s\"\n", x, y, str);

    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 ui_test_str, cmd_ui_test_str, Inject string to UI queue: ui_test_str <x> <y> <str>);
