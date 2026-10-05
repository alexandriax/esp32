idf_component_register(
    SRCS "main.c" "board_controls.c"
    INCLUDE_DIRS "."
    REQUIRES display doom nvs_flash driver esp_timer esp_partition app_update
)

# The companion is installed by scripts/flash-doom.py. `idf.py flash` would
# target the factory address and overwrite Moss, so never register a flash step.
