idf_component_register(
    SRCS main.c iodigital.c ianalogica.c rgb.c memoria.c wifi.c tcp.c microrl.c uner.c serie.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_adc
                  esp_wifi esp_netif esp_event nvs_flash spiffs lwip esp_timer
)

# Graba la carpeta www/ en la particion "storage" (SPIFFS) al hacer idf.py flash
spiffs_create_partition_image(storage ../www FLASH_IN_PROJECT)
