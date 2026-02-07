/* crc.h
Copyright 2021 Carl John Kugler III

Licensed under the Apache License, Version 2.0 (the License); you may not use 
this file except in compliance with the License. You may obtain a copy of the 
License at

   http://www.apache.org/licenses/LICENSE-2.0 
Unless required by applicable law or agreed to in writing, software distributed 
under the License is distributed on an AS IS BASIS, WITHOUT WARRANTIES OR 
CONDITIONS OF ANY KIND, either express or implied. See the License for the 
specific language governing permissions and limitations under the License.
*/
/* Derived from:
 * SD/MMC File System Library
 * Copyright (c) 2016 Neil Thiessen
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
 /**
 Changelog:
   - added picocom_ prefix to avoid ns conflicts with user code (and sdcard fs lib)
 */

#ifndef SD_CRC_H
#define SD_CRC_H


#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
    
char picocom_crc7(const char* data, int length);
unsigned short picocom_crc16(const char* data, int length);
void picocom_update_crc16(unsigned short *pCrc16, const char data[], size_t length);

#ifdef __cplusplus
}
#endif


#endif

/* [] END OF FILE */
