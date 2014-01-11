/*
    This file is part of telegram-client.

    Telegram-client is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Telegram-client is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this telegram-client.  If not, see <http://www.gnu.org/licenses/>.

    Copyright Vitaly Valtman 2013
*/

#ifndef __TOOLS_H__
#define __TOOLS_H__

void *talloc (size_t size);
void *trealloc (void *ptr, size_t old_size, size_t size);
void *talloc0 (size_t size);
char *tstrdup (const char *s);
int tinflate (void *input, int ilen, void *output, int olen);
void ensure (int r);
void ensure_ptr (void *p);

void tfree (void *ptr, int size);
void tfree_str (void *ptr);
void tfree_secure (void *ptr, int size);


int tsnprintf (char *buf, int len, const char *format, ...) __attribute__ ((format (printf, 3, 4)));
int tasprintf (char **res, const char *format, ...) __attribute__ ((format (printf, 2, 3)));

#ifdef DEBUG
void tcheck (void);
#endif
#endif
