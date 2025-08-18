sudo rmmod lan865x_t1s
sudo rmmod microchip_t1s

sudo insmod microchip_t1s.ko
sudo insmod lan865x_t1s.ko

sudo ip addr add dev eth1 192.168.5.100/24
sudo ip link set eth1 up
sudo ethtool --set-plca-cfg eth1 enable on node-id 0 node-cnt 8 to-tmr 0x20 burst-cnt 0x0 burst-tmr 0x80
sudo ethtool --get-plca-cfg eth1

sudo ip addr add dev eth2 192.168.6.100/24
sudo ip link set eth2 up
sudo ethtool --set-plca-cfg eth2 enable on node-id 0 node-cnt 8 to-tmr 0x20 burst-cnt 0x0 burst-tmr 0x80
sudo ethtool --get-plca-cfg eth2

echo performance | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor > /dev/null
