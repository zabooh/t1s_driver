ifndef KDIR
    KDIR=/lib/modules/$(shell uname -r)/build
endif

obj-m += microchip_t1s.o
microchip_t1s-y := lan865x/microchip_t1s.o
obj-m += lan865x_t1s.o
lan865x_t1s-y := lan865x/lan865x.o lan865x/oa_tc6.o lan865x/lan865x_ptp.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
