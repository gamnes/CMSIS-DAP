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

#ifndef VALIDATE_APPLICATION_H
#define VALIDATE_APPLICATION_H

#include "stdint.h"

/**
 @addtogroup
 @{
 */

/**
 Validate the memory locations in the common part of ARM NVIC table
 Would still be good to write a CRC or checksum of the program flash somewhere
 @param  none
 @return 1 on success and 0 otherwise
*/
uint32_t validate_application(void);

/**
 @}
 */

#endif
