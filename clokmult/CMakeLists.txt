add_executable(clokmult
        clokmult.cc
        )
        
# allow multichar constants
target_compile_options(clokmult PRIVATE -Wno-multichar)

#target_link_libraries(clokmult pico_stdlib hardware_i2c hardware_flash hardware_sync)
target_link_libraries(clokmult pico_stdlib)

# enable usb output, disable uart output
pico_enable_stdio_usb(clokmult 1)
pico_enable_stdio_uart(clokmult 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(clokmult)

