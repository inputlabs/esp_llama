# ESP LLAMA

**Low Latency Antenna Module Adapter**

## Dependencies
```
git clone --recursive https://github.com/espressif/esp-idf.git
cd ~/esp/esp-idf
./install.sh esp32-c2
. ./export.sh
```

## Build
```
idf.py set-target esp32-c2
idf.py menuconfig
idf.py build
idf.py flash
idf.py monitor
```
