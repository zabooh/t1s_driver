# LAN865x 10BASE-T1S MAC-PHY Ethernet Linux Driver

This document describes the procedure for configuring the LAN8650/1 hardware and installing driver. These procedures are tested in **Raspberry Pi 4 with Linux Kernel 6.6.20**.

## Setup the hardware
- Connect the Pi 4 Click Shield board on the 40-pin header in Pi 4.
    - Pi 4 Click Shield buy link: https://www.mikroe.com/pi-4-click-shield
- Connect the LAN865x on the Mikro Bus slots available on the Pi 4 Click Shield.
    - LAN865x Click board buy link: https://www.mikroe.com/two-wire-eth-click
    - For more details please refer to the Microchip LAN865x product website:
        - [LAN8650](https://www.microchip.com/en-us/product/lan8650)
        - [LAN8651](https://www.microchip.com/en-us/product/lan8651)
- Prepare the SD card for Pi 4 with raspian image available in the Pi official website.
	- Link to refer: https://www.raspberrypi.com/software/
## Prerequisites
If the Raspberry Pi OS is freshly installed, then the below prerequisites are mandatory before using the driver package. Please make sure the Raspberry Pi is connected with internet because some dependencies need to be installed for driver package installation script.
- Please make sure the **current date and time** of Raspberry Pi is up to date. Can be checked with the below command,

```
    $ date
```
- If they are not up to date and if you want to set it manually then use the below example and modify the fields for the current date and time,
```
    $ sudo date -s "Friday 24 May 2024 02:05:43 PM IST"
```
Link to refer: https://raspberrytips.com/set-date-time-raspberry-pi/
- Please make sure the **apt-get is up to date**. Run the below command to update apt-get,
```
    $ sudo apt-get update
```
## Get driver files
- Extract the downloaded software package into your local directory using the below command,

```
    $ unzip lan865x-linux-driver-0v4.zip
    $ cd lan865x-linux-driver-0v4/
```
## Configure RPI 4
- Open **config.txt** file which is located into the **/boot/firmware/** directory with **Superuser** access to include **lan865x device tree overlay**.
	- Uncomment the line **#dtparam=spi=on** ---> **dtparam=spi=on**
	- Add the line **dtoverlay=lan865x** after the above line, so the **config.txt** should have the below lines also,

**Command to open the file**,
```
    $ sudo vim /boot/firmware/config.txt
```
**Contents to add in the file**,
```
	dtparam=spi=on
	dtoverlay=lan865x
```
**Note:** A sample **config.txt** file with above settings is available in the **config** directory for the reference.
- Make sure the device tree compiler **dtc** is installed in Pi, if not use the below command to install it,
```
    $ sudo apt-get install device-tree-compiler
```
- Compile the device tree overlay file **lan865x-overlay.dts** to generate **lan865x.dtbo** using the below command,
```
    $ dtc -I dts -O dtb -o lan865x.dtbo dts/lan865x-overlay.dts
```
- Copy the generated **lan865x.dtbo** file into the **/boot/overlays/** directory using the below command,
```
    $ sudo cp lan865x.dtbo /boot/overlays/
```
- Now reboot the Pi.
	
## Compile and load driver modules
- Make sure the **linux headers** are installed in Pi, if not use the below command to install it,
```
    $ sudo apt-get --assume-yes install build-essential cmake subversion libncurses5-dev bc bison flex libssl-dev python2
    $ sudo wget https://raw.githubusercontent.com/RPi-Distro/rpi-source/master/rpi-source -O /usr/local/bin/rpi-source && sudo chmod +x /usr/local/bin/rpi-source && /usr/local/bin/rpi-source -q --tag-update
    $ rpi-source --skip-gcc
```
- Compile the driver using the below command,
```
    $ cd lan865x-linux-driver-0v4/
    $ make
```
- Load the drivers using the below command,
```
    $ sudo insmod microchip_t1s.ko
    $ sudo insmod lan865x_t1s.ko
```
- Now you are ready with your 10BASE-T1S ethernet interfaces **eth1** and **eth2** but they are not yet configured.

## Configure Ethernet devices**
```
    $ sudo ip addr add dev eth1 192.168.10.11/24
    $ sudo ip link set eth1 up
    $ sudo ethtool --set-plca-cfg eth1 enable on node-id 0 node-cnt 8 to-tmr 0x20 burst-cnt 0x0 burst-tmr 0x80
    $ sudo ethtool --get-plca-cfg eth1
    $ sudo ip addr add dev eth2 192.168.20.21/24
    $ sudo ip link set eth2 up
    $ sudo ethtool --set-plca-cfg eth2 enable on node-id 0 node-cnt 8 to-tmr 0x20 burst-cnt 0x0 burst-tmr 0x80
    $ sudo ethtool --get-plca-cfg eth2
```
**Important:** ethtool version should be 6.7 or later. ethtool source needs to be downloaded and compiled to use. ethtool application coming with RPI 4 is 6.1 version and that doesn't have support for PLCA settings. ethtool can be downloaded from the below link,
[https://git.kernel.org/pub/scm/network/ethtool/ethtool.git](https://git.kernel.org/pub/scm/network/ethtool/ethtool.git)

- Use the below command to enable better performance in Pi,
```
    $ echo performance | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor > /dev/null
```
**Note:** 
- A sample **load.sh** file included in the driver package for the reference.
- All the above settings need to be done after every boot.

**Tips:**
- If you don't want to do the above **driver loading**, **setting scaling_governor** and **ip configuration** in every boot then add those commands in the **/etc/rc.local** file so that they will be executed automatically every time when you boot Pi. For that open the **rc.local** file with superuser permission and add the following lines before **exit 0**,

**Command to open the file**,
```
    $ sudo vim /etc/rc.local
```
**Contents to add in the file**,
```
sudo insmod microchip_t1s.ko
sudo insmod lan865x_t1s.ko

sudo ip addr add dev eth1 192.168.5.100/24
sudo ip link set eth1 up
sudo ethtool --set-plca-cfg eth1 enable on node-id 0 node-cnt 8 to-tmr 0x20 burst-cnt 0x0 burst-tmr 0x80

sudo ip addr add dev eth2 192.168.6.100/24
sudo ip link set eth2 up
sudo ethtool --set-plca-cfg eth2 enable on node-id 0 node-cnt 8 to-tmr 0x20 burst-cnt 0x0 burst-tmr 0x80

echo performance | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor > /dev/null
```
**Note:** Provide the correct location of your new ethtool application in the above settings.
## TODO
- Timestamping according to Open Alliance TC6 is to be implemented.
## References
- [OPEN Alliance TC6 - 10BASE-T1x MAC-PHY Serial Interface specification](https://www.opensig.org/Automotive-Ethernet-Specifications)
- [OPEN Alliance TC6 Protocol Driver for LAN8650/1](https://github.com/MicrochipTech/oa-tc6-lib)
