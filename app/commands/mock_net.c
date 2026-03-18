#include <stdint.h>
#include <string.h>
#include "shell.h"
#include "esp_at.h"

int cmd_mock_net(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();

    if (argc < 2)
    {
        shellPrint(shell, "Usage: mock_net status    - Show WiFi connection info\n");
        shellPrint(shell, "       mock_net <url>     - HTTP GET and print response\n");
        return -1;
    }

    if (strcmp(argv[1], "status") == 0)
    {
        esp_wifi_info_t info = {0};
        if (!esp_at_get_wifi_info(&info))
        {
            shellPrint(shell, "[NET] WiFi info get failed\n");
            return -1;
        }

        shellPrint(shell, "[NET] Connected: %s\n", info.connected ? "yes" : "no");
        if (info.connected)
        {
            shellPrint(shell, "[NET] SSID   : %s\n", info.ssid);
            shellPrint(shell, "[NET] BSSID  : %s\n", info.bssid);
            shellPrint(shell, "[NET] Channel: %d\n", info.channel);
            shellPrint(shell, "[NET] RSSI   : %d dBm\n", info.rssi);
        }
        return 0;
    }

    const char *url = argv[1];
    shellPrint(shell, "[NET] GET %s\n", url);

    const char *response = esp_at_http_get(url);
    if (response == NULL)
    {
        shellPrint(shell, "[NET] HTTP GET failed\n");
        return -1;
    }

    shellPrint(shell, "[NET] Response:\n%s\n", response);

    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 mock_net, cmd_mock_net, HTTP GET or WiFi status: mock_net <url|status>);
