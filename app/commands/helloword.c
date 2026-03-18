#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "shell.h"

int cmd_helloworld(int argc, char *argv[])
{
    shellPrint(shellGetCurrent(), "%s\n", "hello world");
    shellWriteString(shellGetCurrent(), "hello world\n");

    shellPrint(shellGetCurrent(), "we have %d parameters\n", argc);

    for (uint32_t i = 0; i < 0xffffff; i++);

    for (int i = 0; i < argc; i++)
    {
        shellPrint(shellGetCurrent(), "argv[%d]: %s\n", i, argv[i]);
    }

    return 0;
}
