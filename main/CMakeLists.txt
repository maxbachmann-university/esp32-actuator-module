set(COMPONENT_SRCS "app_main.c"
                   "interrupt_task.c"
                   "motor_control_task.c"
                   "mqtts_task.c"
                   "wifi_task.c"
                   "position_queue.c"
                   "nvs_flash_initialize.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")

register_component()
