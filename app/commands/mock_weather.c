#include <stdint.h>
#include "shell.h"
#include "esp_at.h"
#include "weather.h"

#define MOCK_WEATHER_URL \
    "https://api.seniverse.com/v3/weather/now.json" \
    "?key=S5uYc8UoqdGOUI1eq&location=WTW3SJ5ZBJUY&language=en&unit=c"

int cmd_mock_weather(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    const char *url = (argc >= 2) ? argv[1] : MOCK_WEATHER_URL;

    shellPrint(shell, "[WEATHER] Fetching: %s\n", url);

    const char *response = esp_at_http_get(url);
    if (response == NULL)
    {
        shellPrint(shell, "[WEATHER] HTTP GET failed\n");
        return -1;
    }

    weather_info_t info = {0};
    if (!parse_seniverse_response(response, &info))
    {
        shellPrint(shell, "[WEATHER] Parse failed\n");
        return -1;
    }

    shellPrint(shell, "[WEATHER] City       : %s\n", info.city);
    shellPrint(shell, "[WEATHER] Location   : %s\n", info.location);
    shellPrint(shell, "[WEATHER] Weather    : %s (code: %d)\n",
               info.weather, info.weather_code);
    shellPrint(shell, "[WEATHER] Temperature: %.1f C\n", info.temperature);

    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 mock_weather, cmd_mock_weather, Fetch and parse live weather data);
