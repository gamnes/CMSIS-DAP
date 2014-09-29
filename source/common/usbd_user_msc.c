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
 
 //TODO: still needs cleanup, and error code improvement. Use binary check for start of 
 // bin file and validate the FLASH and RAM values as done at startup.
 // config funcs needed and also another good comb through.
 // validating the NVIC table should also allow us to ignore the file extensions...
 
#include "RTL.h"
#include "rl_usb.h"

#include <string.h>

//#include "target_flash.h"
//#include "target_reset.h"
//#include "DAP_config.h"
//#include "dap.h"

// needs LED flashing mehtods and USB STATE... Maybe do without USB state?!?
#include "main.h"

#include "tasks.h"
//#include "semihost.h"
//#include "version.h"
//#include "swd_host.h"
//#include "usb_buf.h"
#include "flash_erase_read_write.h"
#include "mbed_htm.h"
//#include "board.h"

//#if defined(DBG_LPC1768)
//#   define WANTED_SIZE_IN_KB                        (512)
//#elif defined(DBG_KL02Z)
//#   define WANTED_SIZE_IN_KB                        (32)
//#elif defined(DBG_KL05Z)
//#   define WANTED_SIZE_IN_KB                        (32)
//#elif defined(DBG_KL25Z)
//#   define WANTED_SIZE_IN_KB                        (128)
//#elif defined(DBG_KL26Z)
//#   define WANTED_SIZE_IN_KB                        (128)
//#elif defined(DBG_KL46Z)
//#   define WANTED_SIZE_IN_KB                        (256)
//#elif defined(DBG_K20D50M)
//#   define WANTED_SIZE_IN_KB                        (128)
//#elif defined(DBG_K64F)
//#   define WANTED_SIZE_IN_KB                        (1024)
//#elif defined(DBG_LPC812)
//#   define WANTED_SIZE_IN_KB                        (16)
//#elif defined(DBG_LPC1114)
//#   define WANTED_SIZE_IN_KB                        (32)
//#else
//#   define WANTED_SIZE_IN_KB                        (128)
//#warning target not defined 1024
//#endif

#include "virtual_fs.h"  

extern USB_CONNECT usb_state;

static uint32_t size;
static uint32_t nb_sector;
static uint32_t current_sector;
static uint8_t sector_received_first;
static uint8_t root_dir_received_first;
static uint32_t jtag_flash_init;
static uint32_t flashPtr;
static uint8_t need_restart_usb;
static uint8_t flash_started;
static uint32_t start_sector;
static uint32_t theoretical_start_sector = 7;
static uint8_t msc_event_timeout;
static uint8_t good_file;
static uint8_t program_page_error;
static uint8_t maybe_erase;
static uint32_t previous_sector;
static uint32_t begin_sector;
static uint8_t listen_msc_isr = 1;
static uint8_t drag_success = 1;
static uint8_t reason = 0;
static uint32_t flash_addr_offset = 0;

#define MSC_TIMEOUT_SPLIT_FILES_EVENT   (0x1000)
#define MSC_TIMEOUT_START_EVENT         (0x2000)
#define MSC_TIMEOUT_STOP_EVENT          (0x4000)
#define MSC_TIMEOUT_RESTART_EVENT       (0x8000)

// 30 s timeout
#define TIMEOUT_S 3000

// Reference to the msc task
static OS_TID msc_valid_file_timeout_task_id;

static void init(uint8_t jtag);
static void initDisconnect(uint8_t success);

// this task is responsible to check
// when we receive a root directory where there
// is a valid .bin file and when we have received
// all the sectors that we don't receive new valid sectors
// after a certain timeout
__task void msc_valid_file_timeout_task(void) {
    uint32_t flags = 0;
    OS_RESULT res;
    uint32_t start_timeout_time = 0, time_now = 0;
    uint8_t timer_started = 0;
    msc_valid_file_timeout_task_id = os_tsk_self();
    while (1) {
        res = os_evt_wait_or(MSC_TIMEOUT_SPLIT_FILES_EVENT | MSC_TIMEOUT_START_EVENT | MSC_TIMEOUT_STOP_EVENT | MSC_TIMEOUT_RESTART_EVENT, 100);

        if (res == OS_R_EVT) {

            flags = os_evt_get();

            if (flags & MSC_TIMEOUT_SPLIT_FILES_EVENT) {
                msc_event_timeout = 1;
                os_dly_wait(50);

                if (msc_event_timeout == 1) {
                    // if the program reaches this point -> it means that no sectors have been received in the meantime
                    initDisconnect(1);
                    msc_event_timeout = 0;
                }
            }

            if (flags & MSC_TIMEOUT_START_EVENT) {
                start_timeout_time = os_time_get();
                timer_started = 1;
            }

            if (flags & MSC_TIMEOUT_STOP_EVENT) {
                timer_started = 0;
            }

            if (flags & MSC_TIMEOUT_RESTART_EVENT) {
                if (timer_started) {
                    start_timeout_time = os_time_get();
                }
            }

        } else {
            if (timer_started) {
                time_now = os_time_get();
                // timeout
                if ((time_now - start_timeout_time) > TIMEOUT_S) {
                    timer_started = 0;
                    reason = TIMEOUT;
                    initDisconnect(0);
                }
            }
        }
    }
}

void init(uint8_t jtag) {
    size = 0;
    nb_sector = 0;
    current_sector = 0;
    if (jtag) {
        jtag_flash_init = 0;
        theoretical_start_sector = (drag_success) ? 7 : 8;
        good_file = 0;
        program_page_error = 0;
        maybe_erase = 0;
        previous_sector = 0;
    }
    begin_sector = 0;
    flashPtr = 0;   // load the offset for the application
    sector_received_first = 0;
    root_dir_received_first = 0;
    need_restart_usb = 0;
    flash_started = 0;
    start_sector = 0;
    msc_event_timeout = 0;
    listen_msc_isr = 1;
    flash_addr_offset = 0;
}

void failSWD() {
    reason = SWD_ERROR;
    initDisconnect(0);
}

/*DAP*///extern DAP_Data_t DAP_Data;  // DAP_Data.debug_port

static void initDisconnect(uint8_t success) {
    drag_success = success;
#if 0       // reset and run target
    if (success) {
        swd_set_target_state(RESET_RUN);
    }
#endif
    main_blink_msd_led(0);
    init(1);
    isr_evt_set(MSC_TIMEOUT_STOP_EVENT, msc_valid_file_timeout_task_id);
    // event to disconnect the usb
    main_usb_disconnect_event();
/*DAP*///    semihost_enable();
}

extern uint32_t SystemCoreClock;

int jtag_init() {
/*DAP*///    if (DAP_Data.debug_port != DAP_PORT_DISABLED) {
/*DAP*///        need_restart_usb = 1;
/*DAP*///    }

    /*DAP*///if ((jtag_flash_init != 1) && (DAP_Data.debug_port == DAP_PORT_DISABLED)) {
    if (jtag_flash_init != 1) {
        if (need_restart_usb == 1) {
            reason = SWD_PORT_IN_USE;
            initDisconnect(0);
            return 1;
        }

/*DAP*///        semihost_disable();

/*DAP*///        PORT_SWD_SETUP();

/*DAP*///        target_set_state(RESET_PROGRAM);
        if (!dnd_flash_init(SystemCoreClock)) {
            failSWD();
            return 1;
        }

        jtag_flash_init = 1;
    }
    return 0;
}


static const FILE_TYPE_MAPPING file_type_infos[] = {
    { BIN_FILE, {'B', 'I', 'N'}, 0x00000000 },
    { BIN_FILE, {'b', 'i', 'n'}, 0x00000000 },
    { PAR_FILE, {'P', 'A', 'R'}, 0x00000000 },//strange extension on win IE 9...
    { DOW_FILE, {'D', 'O', 'W'}, 0x00000000 },//strange extension on mac...
    { CRD_FILE, {'C', 'R', 'D'}, 0x00000000 },//strange extension on linux...
    { UNSUP_FILE, {0,0,0},     0            },//end of table marker
};

static FILE_TYPE get_file_type(const FatDirectoryEntry_t* pDirEnt, uint32_t* pAddrOffset) {
    int i;
    char e0 = pDirEnt->filename[8];
    char e1 = pDirEnt->filename[9];
    char e2 = pDirEnt->filename[10];
    char f0 = pDirEnt->filename[0];
    for (i = 0; file_type_infos[i].type != UNSUP_FILE; i++) {
        if ((e0 == file_type_infos[i].extension[0]) &&
            (e1 == file_type_infos[i].extension[1]) &&
            (e2 == file_type_infos[i].extension[2])) {
            *pAddrOffset = file_type_infos[i].flash_offset;
            return file_type_infos[i].type;
        }
    }

    // Now test if the file has a valid extension and a valid name.
    // This is to detect correct but unsupported 8.3 file names.
    if (( ((e0 >= 'a') && (e0 <= 'z')) || ((e0 >= 'A') && (e0 <= 'Z')) ) &&
        ( ((e1 >= 'a') && (e1 <= 'z')) || ((e1 >= 'A') && (e1 <= 'Z')) || (e1 == 0x20) ) &&
        ( ((e2 >= 'a') && (e2 <= 'z')) || ((e2 >= 'A') && (e2 <= 'Z')) || (e2 == 0x20) ) &&
        ( ((f0 >= 'a') && (f0 <= 'z')) || ((f0 >= 'A') && (f0 <= 'Z')) ) &&
           (f0 != '.' &&
           (f0 != '_')) ) {
        *pAddrOffset = 0;
        return UNSUP_FILE;
    }

    *pAddrOffset = 0;
    return SKIP_FILE;
}

// take a look here: http://cs.nyu.edu/~gottlieb/courses/os/kholodov-fat.html
// to have info on fat file system
int search_bin_file(uint8_t * root, uint8_t sector) {
    // idx is a pointer inside the root dir
    // we begin after all the existing entries
    int idx = 0;
    uint8_t found = 0;
    uint32_t i = 0;
    uint32_t move_sector_start = 0, nb_sector_to_move = 0;
    FILE_TYPE file_type;
    uint8_t hidden_file = 0, adapt_th_sector = 0;
    uint32_t offset = 0;

    FatDirectoryEntry_t* pDirEnts = (FatDirectoryEntry_t*)root;

    if (sector == SECTORS_ROOT_IDX) {
        // move past known existing files in the root dir
        idx = (drag_success == 1) ? 12 : 13;
    }

    // first check that we did not receive any directory
    // if we detect a directory -> disconnect / failed
    for (i = idx; i < DIRENTS_PER_SECTOR; i++) {
        if (pDirEnts[i].attributes & 0x10) {
            reason = BAD_EXTENSION_FILE;
            initDisconnect(0);
            return -1;
        }
    }

    // now do the real search for a valid .bin file
    for (i = idx; i < DIRENTS_PER_SECTOR; i++) {

        // Determine file type and get the flash offset
        file_type = get_file_type(&pDirEnts[i], &offset);

        if (file_type == BIN_FILE || file_type == PAR_FILE ||
            file_type == DOW_FILE || file_type == CRD_FILE || file_type == SPI_FILE) {

            hidden_file = (pDirEnts[i].attributes & 0x02) ? 1 : 0;

            // compute the size of the file
            size = pDirEnts[i].filesize;

            if (size == 0) {
              // skip empty files
                continue;
            }

            // read the cluster number where data are stored (ignoring the
            // two high bytes in the cluster number)
            //
            // Convert cluster number to sector number by moving past the root
            // dir and fat tables.
            //
            // The cluster numbers start at 2 (0 and 1 are never used).
            begin_sector = (pDirEnts[i].first_cluster_low_16 - 2) * WANTED_SECTORS_PER_CLUSTER + SECTORS_FIRST_FILE_IDX;

            // compute the number of sectors
            nb_sector = (size + MBR_BYTES_PER_SECTOR - 1) / MBR_BYTES_PER_SECTOR;

            if ( (pDirEnts[i].filename[0] == '_') ||
                 (pDirEnts[i].filename[0] == '.') ||
                 (hidden_file && ((pDirEnts[i].filename[0] == '_') || (pDirEnts[i].filename[0] == '.'))) ||
                 ((pDirEnts[i].filename[0] == 0xE5) && (file_type != CRD_FILE) && (file_type != PAR_FILE))) {
                if (theoretical_start_sector == begin_sector) {
                    adapt_th_sector = 1;
                }
                size = 0;
                nb_sector = 0;
                continue;
            }

            // if we receive a file with a valid extension
            // but there has been program page error previously
            // we fail / disconnect usb
            if ((program_page_error == 1) && (maybe_erase == 0) && (start_sector >= begin_sector)) {
                reason = RESERVED_BITS;
                initDisconnect(0);
                return -1;
            }

            adapt_th_sector = 0;

            // on mac, with safari, we receive all the files with some more sectors at the beginning
            // we have to move the sectors... -> 2x slower
            if ((start_sector != 0) && (start_sector < begin_sector) && (current_sector - (begin_sector - start_sector) >= nb_sector)) {

                // we need to copy all the sectors
                // we don't listen to msd interrupt
                listen_msc_isr = 0;

                move_sector_start = (begin_sector - start_sector)*MBR_BYTES_PER_SECTOR;
                nb_sector_to_move = (nb_sector % 2) ? nb_sector/2 + 1 : nb_sector/2;
                for (i = 0; i < nb_sector_to_move; i++) {
                    if (!dnd_read_memory(move_sector_start + (i*app.sector_size), (uint8_t *)usb_buffer, app.sector_size)) {
                        failSWD();
                        return -1;
                    }
                    if (!dnd_erase_sector(i)) {
                        failSWD();
                        return -1;
                    }
                    if (!dnd_program_page((i*app.sector_size), (uint8_t *)usb_buffer, app.sector_size)) {
                        failSWD();
                        return -1;
                    }
                }
                initDisconnect(1);
                return -1;
            }

            found = 1;
            idx = i; // this is the file we want
            good_file = 1;
            flash_addr_offset = offset;
            break;
        }
        // if we receive a new file which does not have the good extension
        // fail and disconnect usb
        else if (file_type == UNSUP_FILE) {
            reason = BAD_EXTENSION_FILE;
            initDisconnect(0);
            return -1;
        }
    }

    if (adapt_th_sector) {
        theoretical_start_sector += nb_sector;
        init(0);
    }
    return (found == 1) ? idx : -1;
}

static int programPage() {
    //The timeout task's timer is resetted every 256kB that is flashed.
    if ((flashPtr >= 0x40000) && ((flashPtr & 0x3ffff) == 0)) {
        isr_evt_set(MSC_TIMEOUT_RESTART_EVENT, msc_valid_file_timeout_task_id);
    }

    // if we have received two sectors, write into flash
    if (!dnd_program_page((flashPtr+flash_addr_offset+app.flash_start), (uint8_t *)usb_buffer, FLASH_PROGRAM_PAGE_SIZE)) {
        // even if there is an error, adapt flashptr
        flashPtr += FLASH_PROGRAM_PAGE_SIZE;
        return 1;
    }

    // if we just wrote the last sector -> disconnect usb
    if (current_sector == nb_sector) {
        initDisconnect(1);
        return 0;
    }

    flashPtr += FLASH_PROGRAM_PAGE_SIZE;

    return 0;
}

void usbd_msc_init ()
{    
    USBD_MSC_MemorySize = MBR_NUM_NEEDED_SECTORS * MBR_BYTES_PER_SECTOR;
    USBD_MSC_BlockSize  = 512;  // need a define here
    USBD_MSC_BlockGroup = 1;    // and here
    USBD_MSC_BlockCount = USBD_MSC_MemorySize / USBD_MSC_BlockSize;
    USBD_MSC_BlockBuf   = (uint8_t *)usb_buffer;
    USBD_MSC_MediaReady = __TRUE;
}

void usbd_msc_read_sect (uint32_t block, uint8_t *buf, uint32_t num_of_blocks) {
    if ((usb_state != USB_CONNECTED) || (listen_msc_isr == 0))
        return;

    if (USBD_MSC_MediaReady) {
        // blink led not permanently
        main_blink_msd_led(0);
        memset(buf, 0, 512);

        // Handle MBR, FAT1 sectors, FAT2 sectors, root1, root2 and mac file
        if (block <= SECTORS_FIRST_FILE_IDX) {
            memcpy(buf, sectors[block].sect, sectors[block].length);

            // add new entry in FAT
            if ((block == 1) && (drag_success == 0)) {
                buf[9] = 0xff;
                buf[10] = 0x0f;
            } else if ((block == SECTORS_ROOT_IDX) && (drag_success == 0)) {
                /* Appends a new directory entry at the end of the root file system.
                    The entry is a copy of "fail[]" and the size is updated to match the
                    length of the error reason string. The entry's set to point to cluster
                    4 which is the first after the mbed.htm file."
                */
                memcpy(buf + sectors[block].length, &fail, sizeof(fail));
                // adapt size of file according fail reason
                buf[sectors[block].length + 28] = strlen((const char *)reason_array[reason]);
                buf[sectors[block].length + 26] = 6;
            }
        }
        // send System Volume Information
        else if (block == SECTORS_SYSTEM_VOLUME_INFORMATION) {
            memcpy(buf, sect6, sect6_size);
        }
        // send System Volume Information/IndexerVolumeGuid
        else if (block == SECTORS_INDEXER_VOLUME_GUID) {
            memcpy(buf, sect7, sect7_size);
        }
        // send mbed.html
        else if (block == SECTORS_MBED_HTML_IDX) {
            update_html_file((uint8_t *)usb_buffer, 512);
        }
        // send error message file
        else if (block == SECTORS_ERROR_FILE_IDX) {
            memcpy(buf, reason_array[reason], strlen((const char *)reason_array[reason]));
        }
    }
}

void usbd_msc_write_sect (uint32_t block, uint8_t *buf, uint32_t num_of_blocks) {
    int idx_size = 0;

    if ((usb_state != USB_CONNECTED) || (listen_msc_isr == 0))
        return;

    // we recieve the root directory
    if ((block == SECTORS_ROOT_IDX) || (block == (SECTORS_ROOT_IDX+1))) {
        // try to find a .bin file in the root directory
        //  not the best idean since OS's send files with different extensions. 
        //  Chris R suggested looking for the valid NVIC table, maybe both
        idx_size = search_bin_file(buf, block);

        // .bin file exists
        if (idx_size != -1) {

            if (sector_received_first == 0) {
                root_dir_received_first = 1;
            }

            // this means that we have received the sectors before root dir (linux)
            // we have to flush the last page into the target flash
            if ((sector_received_first == 1) && (current_sector == nb_sector) && (jtag_flash_init == 1)) {
                if (msc_event_timeout == 0) {
                    msc_event_timeout = 1;
                    isr_evt_set(MSC_TIMEOUT_SPLIT_FILES_EVENT, msc_valid_file_timeout_task_id);
                }
                return;
            }

            // means that we are receiving additional sectors
            // at the end of the file ===> we ignore them
            if ((sector_received_first == 1) && (start_sector == begin_sector) && (current_sector > nb_sector) && (jtag_flash_init == 1)) {
                initDisconnect(1);
                return;
            }
        }
    }
    if (block >= SECTORS_ERROR_FILE_IDX) {

        main_usb_busy_event();

        if (root_dir_received_first == 0) {
            sector_received_first = 1;
        }

        // if we don't receive consecutive sectors
        // set maybe erase in case we receive other sectors
        if ((previous_sector != 0) && ((previous_sector + 1) != block)) {
            maybe_erase = 1;
            return;
        }

        // bootloader in win8.1 is triggering this state... over and over...
        //  trying to write something like this {533683ac-37bf-4c61-b340-7a41379959d3}
        if (!flash_started && (block > theoretical_start_sector)) {
            theoretical_start_sector = block;
        }

        // init jtag if needed
        if (jtag_init() == 1) {
            return;
        }

        if (jtag_flash_init == 1) {

            main_blink_msd_led(1);

            // We erase the chip if we received unrelated data before (mac compatibility)
            if (maybe_erase && (block == theoretical_start_sector)) {
                // avoid erasing the internal flash if only the external flash will be updated
                if (flash_addr_offset == 0) {
                    if (!dnd_erase_chip()) {
                        return;
                    }
                }
                maybe_erase = 0;
                program_page_error = 0;
            }

            // drop block < theoretical_sector
            if (theoretical_start_sector > block) {
                return;
            }

            // this is triggering on win8.1... dont follow the logic yet.
            if ((flash_started == 0) && (theoretical_start_sector == block)) {
                flash_started = 1;
                isr_evt_set(MSC_TIMEOUT_START_EVENT, msc_valid_file_timeout_task_id);
                start_sector = block;
            }

            // at the beginning, we need theoretical_start_sector == block
            if ((flash_started == 0) && (theoretical_start_sector != block)) {
                reason = BAD_START_SECTOR;
                initDisconnect(0);
                return;
            }

            // not consecutive sectors detected
            if ((flash_started == 1) && (maybe_erase == 0) && (start_sector != block) && (block != (start_sector + current_sector))) {
                reason = NOT_CONSECUTIVE_SECTORS;
                initDisconnect(0);
                return;
            }

            // if we receive a new sector
            // and the msc thread has been started (we thought that the file has been fully received)
            // we kill the thread and write the sector in flash
            if (msc_event_timeout == 1) {
                msc_event_timeout = 0;
            }

            if (flash_started && (block == theoretical_start_sector)) {
                // avoid erasing the internal flash if only the external flash will be updated
                if (flash_addr_offset == 0) {
                    if (!dnd_erase_chip()) {
                        return;
                    }
                }
                maybe_erase = 0;
                program_page_error = 0;
            }

            previous_sector = block;
            current_sector++;
            if (programPage() == 1) {
                if (good_file) {
                    reason = RESERVED_BITS;
                    initDisconnect(0);
                    return;
                }
                program_page_error = 1;
                return;
            }
        }
    }
}

