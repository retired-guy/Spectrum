#!/usr/bin/bash

# Set the pin to PWM mode
gpio -g mode 19 pwm 
# Set the value/brightness 
gpio -g pwm 19 30

#sudo sh -c 'echo "128" > /sys/class/backlight/rpi_backlight/brightness'

