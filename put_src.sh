#!/bin/bash

SRC_DIR="src"
DST_DIR="../bsp_patch/mscc-brsdk-source-2024.09/output/mybuild/build/linux-custom/drivers/net/ethernet/microchip/lan865x"

FILES=(
    lan865x.c
    lan865x_ptp.c
    lan865x_ptp.h
    Makefile
    microchip_t1s.c
    oa_tc6.c
    oa_tc6.h
)

for file in "${FILES[@]}"; do
    echo "Copying $file from $SRC_DIR to $DST_DIR"
    cp "$SRC_DIR/$file" "$DST_DIR/"
done