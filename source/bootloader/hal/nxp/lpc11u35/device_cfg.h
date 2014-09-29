/* CMSIS-DAP Interface Firmware
 * Copyright (c) 2009-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#ifndef DEVICE_CFG_H
#define DEVICE_CFG_H

/**
 This file is for the NXP LPC11U35 bootloader
 Each device (bootloader) or target (CMSIS-DAP app)
 Should have a file that defines the following
 */

/*!< BOARD_ID is an unique identifier for the bootloader or CMSIS-DAP application */
#define BOARD_ID            "fffe"

/*!< BOARD_SECRET is mangaged by mbed.org To request a BOARD_SECRET email support@mbed.org Do not version this */
#define BOARD_SECRET        "00000000"

/*!< APP_START_ADR is the execution address for an application */
#define APP_START_ADR       

/*!< SECTOR_SIZE is the number of bytes in a sector used by flash erase and write and usb_buffer sizing */
#define SECTOR_SIZE         

/*!< NUM_OF_SECTORS is the integer representation of sectors in flash. Devices with multiple sizes should
        implement SECTOR_SIZE_A, SECTOR_SIZE_B etc and generate accordingly */
#define NUM_OF_SECTORS      (64)

/*!< FLASH_SIZE_KB is the size of flash for a given application */
#define FLASH_SIZE_KB       (NUM_OF_SECTORS-(APP_START_ADR/1024))

/*!< START_FLASH is the lowest ROM address executable code can be placed. Usually after the NVIC table 
        and is used to validate an image by looking at the NVIC entries */
#define START_FLASH	        

/*!< END_FLASH used to verify images in the NVIC table. Calculate accordingly if multiple sector sizes exist */
#define END_FLASH           (FLASH_SIZE_KB*SECTOR_SIZE)

/*!< START_RAM used to verify images in the NVIC table. Defined by the linker control file */
#define START_RAM           

/*!< END_RAM used to verify images in the NVIC table. Defined by the linker control file */
#define END_RAM	            

#endif
