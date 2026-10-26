idf_component_register(
    SRCS main.c iodigital.c ianalogica.c pwm.c temporizador.c memoria.c wifi.c web.c tcp.c telnet.c microrl.c rgb.c uner.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_gptimer esp_driver_uart esp_adc esp_driver_mcpwm
                  esp_wifi esp_netif esp_event nvs_flash esp_http_server spiffs lwip esp_timer
)

# Graba la carpeta www/ en la particion "storage" (SPIFFS) al hacer idf.py flash
spiffs_create_partition_image(storage ../www FLASH_IN_PROJECT)
