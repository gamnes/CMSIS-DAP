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

#include "LPC11Uxx.h"
#include "vector_table.h"
#include "device_cfg.h"

// Target specific defines
#define NVIC_NUM_VECTORS            // CORE + MCU Peripherals
#define NVIC_RAM_VECTOR_ADDRESS     // Vectors positioned at start of RAM

void relocate_vector_table()
{
    uint32_t *vectors;
    uint32_t i;

    // Copy and switch to dynamic vectors if the first time called
    if (SCB->VTOR != NVIC_RAM_VECTOR_ADDRESS) {
        uint32_t *old_vectors = (uint32_t *)APP_START_ADR;
        vectors = (uint32_t *)NVIC_RAM_VECTOR_ADDRESS;

        for (i = 0; i < NVIC_NUM_VECTORS; i++) {
            vectors[i] = old_vectors[i];
        }

        SCB->VTOR = (uint32_t)NVIC_RAM_VECTOR_ADDRESS;
    }
}
