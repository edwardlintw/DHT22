# DHT22 Sensor Driver

The purpose of this project is to develop a Linux Kernel Driver for `DHT22` humidity and temperature sensor. For more information of `DHT22`, read the followings link:

* Datasheet for [DHT22](http://akizukidenshi.com/download/ds/aosong/AM2302.pdf)

I cross compiled to `RPi3 Model B` running `Linux Kernel 4.8.16-v7+`. The host environment is `Ubuntu 16.04 LTS` (running Linux Kernel 4.4.0-98-generic). I've tested and run the driver on RPi3/B only and never tested compability issues between `DHT22` and `DHT11` (the other cheaper one), other Pi models or other Kernel versions.

Modify `Makefile` first to fit in your development environment before you can build this driver; for either native(on RPi) or cross compilation.

If you prefer cross compilation, the first step is to build your own RPi Linux Kernel (whatever versions) on host environment. 

I'm new to Linux Kernel/Driver programming; whatever wrong you find, please don't hesitate to contact me.

p.s. This is my first Linux Kernel Driver, I've learned and studied [this](https://github.com/Filkolev/DHT22-sensor-driver), then developed this simplified one (with simple algorithm, more comments and without compicated mechanism, like [FSM](https://en.wikipedia.org/wiki/Finite-state_machine)).

## Contents
 1. [About DHT22 Seosor](#about-dht22-sensor)   
   1.1. [DHT22 Spec And Communication](#dht22-spec-and-communication)   
   1.2. [Triggering DHT22](#triggering-dht22)   
   1.3. [Reading And Interpreting Humidity/Temperature/Parity Raw Data](#reading-and-interpreting-humiditytemperatureparity-raw-data)   
   1.4. [Temperature Below 0°C](#temperature-below-0)   
 2. [Using This Driver](#using-this-driver)   
   2.1. [Loading/Unloading The Driver](#loadingunloading-the-driver)   
   2.2. [sysfs Attributes](#sysfs-attributes)   
   2.3. [Some Useful Examples](#some-useful-examples)   

         
## About DHT22 Sensor
[back to top](#dht22-sensor-driver)

### DHT22 Spec And Communication
[back to top](#dht22-sensor-driver)

### Triggering DHT22
[back to top](#dht22-sensor-driver)

The `DHT22` must be manually triggered with a specific signal as the followings:
 1. After power on, the `DHT22` sensor must warm up first; in general, it takes about `2 seconds`. During first 2 seconds, the sensor can't respond to host's request.
 2. The host triggers request to `DHT22` by pulling signal bus to `LOW`, at least `800μs`, typically `1000μs`, then release the bus to `HIGH` to wake up the sensor.
 3. `DHT22` responds to the host by pulling signal bus to `LOW for about 80μs`, then release the signal bus to `HIGH to echo the host`.
 4. `DHT22` sends out `40-bit` data (2-byte `humidity`, 2-byte `temperature` followed by 1-byte `parity check`). `MSB first`; LOW signal followed by HIGH signal; HIGH signal lasts `26-28μs` to represent `bit value 0`, HIGH signal lasts `68-75μs` to represent `bit value 1`.
 5. `DHT22` finally to pull signal bus `LOW` again (50μs, following 40-bit data) to notify the host about end of communication, then release the signal bus to `HIGH`.

### Reading And Interpreting Humidity/Temperature/Parity Raw Data
[back to top](#dht22-sensor-driver)

 1. 40-bit raw data is composed of 2-byte humidity, 2-byte temperature and 1-byte parity check. Raw humidity and temperature are both `big-endian` format and `10 times` of real humidity and temperature. 
 2. For example, raw data `0x03 0x2F 0x01 0x09` means raw humidity is `0x032F` and raw temperature is `0x0109`. HEX `0x032F` is `815(DEC)`, `0x0109` is `265(DEC)`. Since raw data is `10 times` of real data, so the humidity is `81.5%` and temperature is `26.5°C`.
 4. Parity check: The parity check is 1-byte and summation of byte 0 to 3 may overflow, we must `bitwise AND 0x00FF` to mask high byte before checking against to parity byte. Take the above as an example: `(0x003F + 0x002F + 0x001C + 0x0009) & 0x00FF == 0x003C`, the parity check byte must be 0x3C, or the host must ignore this error and re-trigger later.

### Temperature Below 0°C
[back to top](#dht22-sensor-driver)
 1. Bit 0 (MSB) of raw data byte 2 (3rd byte) is a `sign bit` to represent positive temperature or temperature below 0°C; `bit value 1 is negative`. Please note this is `not the two's complement` system.
 2. When calculating real temperature, we must mask MSB of high-byte temperature to get real temperature. So `0x0109` means `26.5°C` and `0x8109` means `negative 0x0109`, that is `-26.5°C`.

## Using This Driver
[back to top](#dht22-sensor-driver)

### Loading/Unloading The Driver
[back to top](#dht22-sensor-driver)
 1. After a success build, you'll get `dht22.ko` file; copy this to wherever that your RPi can read. Load the module by the command (with root permission) 

    > `insmod dht22.ko`

 2. dht22.ko default parameters:
    `gpio`:  Assigned GPIO number of `DHT22` data pin, `default is 4`.
    `autoupdate`: Automatically trigger `DHT22` or not, `default is 1` (turn ON autoupdate); 0 to to turn it OFF. Others are interpreted as ON.
    `autoupdate_sec`: Seconds between two trigger events, default is 10 seconds (int)


The `DHT22` driver will be loaded by default parameters; if you want to assign other values, try this form:
    > `insmod dht22.ko [gpio=<gpio_number>] [autoupdate=<flag>] [autoupdate_sec=<second>]`

    `autoupdate=0` to turn OFF the flag; others rather than 0 turns it ON.
    `autoupdate_sec` must be any positive number between 3 (sec) and 60000 (10 min). The driver ignores any number out of this range. 
   
 3. To unload the driver, simply do this (with root permission). 
    > `rmmod dht22`

 4. To check whether the driver was loaded or not by kernel:
    > `lsmod | grep dht22`

### sysfs Attributes
[back to top](#dht22-sensor-driver)
 
 1. After you `insmod dht22.ko`, the driver will create a directory `dht22` under /sys/kernel. Read or write these sysfs attributes by

    > `cd /sys/kernel/dht22`   
    > `ls -lsa`

    you'll see the followings:

    0 -rw-r--r-- 1 root root 4096 Nov 14 12:05 autoupdate   
    0 -rw-r--r-- 1 root root 4096 Nov 14 12:05 autoupdate_sec   
    0 --w------- 1 root root 4096 Nov 14 12:05 debug   
    0 -r--r--r-- 1 root root 4096 Nov 14 12:05 gpio   
    0 -r--r--r-- 1 root root 4096 Nov 14 12:05 humidity   
    0 -r--r--r-- 1 root root 4096 Nov 14 12:05 temperature   
    0 --w------- 1 root root 4096 Nov 14 12:05 trigger   

 2. The attributes 'debug' and 'trigger' is write only; 'humidity' and 'temperature' are read only; others are both read and write.

 3. Only users with root permission can write value to attributes. This is forbidden by Linux Operating System, not by the driver. To change permission of individual attribute, do chmod with root permission; for example:

    > `chmod 222 trigger`

Thus, any user can 'trigger' DHT22 with permission `222`.


### Some Useful Examples
[back to top](#dht22-sensor-driver)
 
 1. Some useful tips are helpful, especially you are new to Linux Kernel Driver.

 2. To read temperature and humidity (both are read only):
    > `cd /sys/kernel/dht22`   
    > `cat humidity`   
    > `cat temperature`   

 3. To get GPIO number used by DHT22; it's also read only, no one can change GPIO number after the driver is loaded. To change GPIO number, please read [Loading/Unloading The Driver](#loadingunloading-the-driver).
.
    > `cat gpio` 

 4. To get 'autoupdate' flag and update the flag (with root permission)
    to dispable 'autoupdate':
    
    > `echo 0 > autoupdate`

    to enable 'autoupdate'
    
    > `echo 1 > autoupdate`

    or you can read [Loading/Unloading The Driver](#loadingunloading-the-driver) to load driver with 'autoupdate' OFF.

 5. To get 'autoupdate_sec' value and modify time interval between two trigger events; time unit is second, the range must be any positive number between 3 (sec) and 60000 (10 minutes). Any unrecognized input will be ignored by the driver and takes no effect. To read time interval:

    > `cat autoupdate_sec`

    to modify time interval:

    > `echo 60 > autoupdate_sec`

    DHT22 can be automatically triggered only when 'autoupdate' flag is ON.

 6. Trigger DHT22 manually:

    > `echo 1 > trigger`

    this command will trigger sensor to fetch humidity/temperature, no matter `autoupdate` flag is ON or OFF.

