/*
    This file is part of VK/KittenPHP-DB-Engine Library.

    VK/KittenPHP-DB-Engine Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    VK/KittenPHP-DB-Engine Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with VK/KittenPHP-DB-Engine Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2009-2012 Vkontakte Ltd
              2009-2012 Nikolai Durov
              2009-2012 Andrei Lopatin
                   2012 Anton Maydell
*/

#ifndef __CRC32_H__
#define __CRC32_H__


#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int crc32_table[256];
unsigned crc32_partial (const void *data, int len, unsigned crc);
  //unsigned crc32_partial_fast (const void *data, int len, unsigned crc);
  //unsigned crc32_partial_fastest (const void *data, int len, unsigned crc);
unsigned compute_crc32 (const void *data, int len);
unsigned compute_crc32_combine (unsigned crc1, unsigned crc2, int len2);

extern unsigned long long crc64_table[256];
unsigned long long crc64_partial (const void *data, int len, unsigned long long crc);
unsigned long long crc64 (const void *data, int len);

//unsigned gf32_matrix_times (unsigned *matrix, unsigned vector);

/* crc32_check_and_repair returns
   0 : Cyclic redundancy check is ok
   1 : Cyclic redundancy check fails, but we fix one bit in input
   2 : Cyclic redundancy check fails, but we fix one bit in input_crc32
  -1 : Cyclic redundancy check fails, no repair possible. 
       In this case *input_crc32 will be equal crc32 (input, l)

  Case force_exit == 1 (case 1, 2: kprintf call, case -1: assert fail).
*/
int crc32_check_and_repair (void *input, int l, unsigned *input_crc32, int force_exit);

#ifdef __cplusplus
}
#endif

#endif
