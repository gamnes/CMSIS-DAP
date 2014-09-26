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

#include "mbed_htm.h"
#include "read_uid.h"
#include "firmware_cfg.h"
#include <string.h>

// Pointers to substitution strings
static char const *fw_version = (const char *)FW_BUILD;

static uint32_t unique_id[4] = {0};
static uint32_t auth = 0;
static uint8_t string_web_auth[25 + 4] = {""};
static uint8_t string_uid_wchar[2 + 25 * 2] = {""};

static uint8_t const WebSide[] = {
    "<!-- mbed Microcontroller Website and Authentication Shortcut -->\r\n"
    "<!-- Version: " FW_BUILD " Build: " __DATE__ " " __TIME__ " -->\r\n"
    "<html>\r\n"
    "<head>\r\n"
    "<meta http-equiv=\"refresh\" content=\"0; url=https://mbed.org/handbook/OpenLINK\"/>\r\n"
    "<title>mbed Website Shortcut</title>\r\n"
    "</head>\r\n"
    "<body></body>\r\n"
    "</html>\r\n"
    "\r\n"
};


static void write_byte_ascii_hex( uint8_t b, uint8_t *ch1, uint8_t *ch2 )
{
    static char const nybble_chars[] = "0123456789ABCDEF";
    *ch1 = nybble_chars[ ( b >> 4 ) & 0x0F ];
    *ch2 = nybble_chars[ b & 0x0F ];
}

static uint32_t atoi(uint8_t *str, uint8_t size, uint8_t base)
{
    uint32_t k = 0;
    uint8_t idx = 0;
    uint8_t i = 0;

    for (i = 0; i < size; i++) {
        if (*str >= '0' && *str <= '9') {
            idx = '0';
        } else if (*str >= 'A' && *str <= 'F') {
            idx = 'A' - 10;
        } else if (*str >= 'a' && *str <= 'f') {
            idx = 'a' - 10;
        }

        k = k * base + (*str) - idx;
        str++;
    }

    return k;
}

static void setup_string_web_auth()
{
    uint8_t i = 0;
    uint8_t idx = 0;

    string_web_auth[0] = '$';
    string_web_auth[1] = '$';
    string_web_auth[2] = '$';
    string_web_auth[3] = 24;
    idx += 4;

    // string id
    for (i = 0; i < 4; i++) {
        string_web_auth[idx++] = app.board_id[i];
    }

    for (i = 0; i < 4; i++) {
        string_web_auth[idx++] = fw_version[i];
    }

    // writes 2 bytes in string_web_auth at a time
    for (i = 0; i < 4; i++) {
        write_byte_ascii_hex((unique_id[0] >> 8 * (3 - i)) & 0xff, &string_web_auth[idx + 2 * i], &string_web_auth[idx + 2 * i + 1]);
    }

    idx += 8;

    //string auth (2 bytes in string_web_auth at a time)
    for (i = 0; i < 4; i++) {
        write_byte_ascii_hex((auth >> 8 * (3 - i)) & 0xff, &string_web_auth[idx + 2 * i], &string_web_auth[idx + 2 * i + 1]);
    }

    idx += 8;
    // null terminate
    string_web_auth[idx] = 0;
}

// need to change this to be boardID + target UUID
//  every board would become a new instance to the host but
//  would not require updating USB drivers when OpenLINK app firmware is updated
//  do we need to know the firmware version of the OpenLINK firmware?? I dont think so
//  because any host could query the MSC and parse mbed.htm for this info
static void setup_string_usb_descriptor()
{
    uint8_t i = 0;
    uint8_t idx = 0;
    uint8_t len = 0;

    len = strlen((const char *)(string_web_auth + 4));
    string_uid_wchar[0] = len * 2 + 2;
    string_uid_wchar[1] = 3;
    idx += 2;

    // convert char to wchar for usb
    for (i = 0; i < len * 2; i++) {
        if ((i % 2) == 0) {
            string_uid_wchar[idx + i] = string_web_auth[4 + i / 2];
        } else {
            string_uid_wchar[idx + i] = 0;
        }
    }

    idx += len * 2;

    string_uid_wchar[idx] = 0;
}


uint8_t *get_web_auth_string(void)
{
    return string_web_auth;
}

uint8_t get_uid_string_len(void)
{
    return sizeof(string_uid_wchar);
}

uint8_t *get_uid_string(void)
{
    return string_uid_wchar;
}

static void compute_auth()
{
    uint32_t id = 0;
    uint32_t fw = 0;
    uint32_t sec = 0;

    id = atoi((uint8_t *)app.board_id , 4, 16);
    fw = atoi((uint8_t *)fw_version, 4, 16);
    auth = (id) | (fw << 16);
    auth ^= unique_id[0];   // only using lower 32 bits :(
    sec = atoi((uint8_t *)(app.secret), 8, 16);
    auth ^= sec;
}

// HTML character reader context
typedef struct {
    uint8_t *phtml;        // Pointer to current position in HTML data in flash
    uint8_t substitute;    // TRUE if characters should be read from a substitution string, otherwise read from HTML data.
} HTMLCTX;

HTMLCTX const html_ctx_init = {'\0', 0};

static void init_html(HTMLCTX *h, uint8_t *ptr)
{
    h->substitute = 0;
    h->phtml = ptr;
    return;
}

static uint8_t get_html_character(HTMLCTX *h)
{
    // Returns the next character from the HTML data at h->phtml.
    // Substitutes special sequences @V etc. with variable text.

    uint8_t c = 0;
    uint8_t s = 0;                  // Character from HTML data
    static uint8_t *sptr = '\0';    // Pointer to substitution string data
    uint8_t valid = 0;              // Set to false if we need to read an additional character

    do {
        valid = 1;

        if (h->substitute) {
            // Check next substitution character
            if (*sptr == '\0') {
                // End of substituted string
                h->substitute = 0;
            }
        }

        if (!h->substitute) {
            // Get next HTML character
            c = *h->phtml++;

            // Indicates substitution
            if (c == '@') {
                // Check next HTML character
                s = *h->phtml;

                switch (s)  {

                    case 'A':
                        sptr = (uint8_t *)(string_web_auth + 4);           // auth string
                        h->substitute = 1;
                        break;

                        // Add any additional substitutions here

                    default:
                        break;
                }

                if (h->substitute) {
                    // If a vaild substitution sequence was found then discard this character
                    // Increment HTML pointer
                    h->phtml++;

                    // Check for the case of the substitution string being zero characters length
                    if (*sptr == '\0') {
                        // Effectively the substitution is already completed
                        h->substitute = 0;

                        // Must read another character
                        valid = 0;
                    }
                }
            }
        }

        if (h->substitute) {
            // Get next substitution character
            c = *sptr++;
        }

    } while (!valid);

    return c;
}

void unique_string_auth_config(void)
{
    read_uuid(unique_id);
    compute_auth();
    setup_string_web_auth();
    setup_string_usb_descriptor();
}

uint8_t update_html_file (uint8_t *buffer, uint32_t size)
{
    // Update a file containing the version information for this firmware
    // This assumes exclusive access to the file system (i.e. USB not enabled at this time)

    HTMLCTX html = html_ctx_init; // HTML reader context
    uint8_t c = 0;                // Current character from HTML reader
    uint32_t i = 0;

    // Write file
    init_html(&html, (uint8_t *)WebSide);

    do {
        c = get_html_character(&html);

        if (c != '\0') {
            buffer[i++] = c;
        }
    } while (c != '\0');

    memset(buffer + i, ' ', size - i);
    return 1;  // Success
}
