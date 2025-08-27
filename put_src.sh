#!/bin/bash

SRC_DIR1="src"
DST_DIR1="../bsp_patch/mscc-brsdk-source-2024.09/output/mybuild/build/linux-custom/drivers/net/ethernet/microchip/lan865x"

FILES1=(
    lan865x.c
    lan865x.h
    lan865x_ptp.c
    lan865x_ptp.h
    microchip_t1s.c
    oa_tc6.c
    oa_tc6.h
)

for file in "${FILES1[@]}"; do
    echo "Copying $file from $SRC_DIR1 to $DST_DIR1"
    cp "$SRC_DIR1/$file" "$DST_DIR1/"
done


SRC_DIR2="."
DST_DIR2="../bsp_patch/mscc-brsdk-source-2024.09/output/mybuild/build/linux-custom/drivers/net/ethernet/microchip"

FILES2=(
    driver.md
    load.sh
    Makefile
    ptp_todo.md
    README.md
    release_notes.md
)

for file in "${FILES2[@]}"; do
    echo "Copying $file from $SRC_DIR2 to $DST_DIR2"
    cp "$SRC_DIR2/$file" "$DST_DIR2/"
done

