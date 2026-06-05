# ESP LLAMA

**Low Latency Antenna Module Adapter**

## Dependencies
```
git clone --recursive --branch "v6.0.1" https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32-c2
. ./export.sh
```

## Build
```
cd esp_llama
idf.py build
make binpack
```

## Additional commands
- `idf.py set-target esp32-c2` -- Initial config for c2 chip.
- `idf.py menuconfig` -- Edit configuration via menu.
- `idf.py flash` -- Upload the compiled binary into a connected ESP.
- `idf.py monitor` -- Get logs from a connected ESP running the program.
