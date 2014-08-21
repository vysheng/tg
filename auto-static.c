/* 
    This file is part of tgl-library

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

    Copyright Vitaly Valtman 2014
*/

int skip_double (void) {
  if (in_ptr + 2 <= in_end) {
    in_ptr += 2;
    return 0;
  } else {
    return -1;
  }
}
int skip_long (void) {
  if (in_ptr + 2 <= in_end) {
    in_ptr += 2;
    return 0;
  } else {
    return -1;
  }
}
int skip_int (void) {
  if (in_ptr + 1 <= in_end) {
    in_ptr += 1;
    return 0;
  } else {
    return -1;
  }
}
int skip_string (void) {
  if (in_ptr == in_end) { return -1; }
  unsigned len = *(unsigned char *)in_ptr;
  if (len == 0xff) { return -1; }
  if (len < 0xfe) {
    unsigned size = (len + 4) >> 2;
    if (in_ptr + size <= in_end) {
      in_ptr += size;
      return 0;
    } else {
      return -1;
    }
  } else {
    len = (*(unsigned *)in_ptr) >> 8;
    unsigned size = (len + 7) >> 2;
    if (in_ptr + size <= in_end) {
      in_ptr += size;
      return 0;
    } else {
      return -1;
    }
  }
}
