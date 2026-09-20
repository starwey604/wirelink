# udev rules

`99-wirelink.rules` grants the active desktop session access to the Espressif
USB Serial/JTAG port and the common USB-UART bridges used to flash and monitor
ESP32 boards, so `west flash` and `benchmarks/zephyr/capture_serial.py` run
without root.

Install once and replug the board:

```sh
sudo cp 99-wirelink.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger
```

On Arch Linux you can instead add your user to the `uucp` group, then log out
and back in:

```sh
sudo usermod -aG uucp "$USER"
```

Add your board's VID/PID to the rule if it uses a different USB-UART bridge.
