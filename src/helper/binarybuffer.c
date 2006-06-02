/***************************************************************************
 *   Copyright (C) 2004, 2005 by Dominic Rath                              *
 *   Dominic.Rath@gmx.de                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "log.h"

#include "binarybuffer.h"

int buf_set_u32(u8* buffer, unsigned int first, unsigned int num, u32 value);
u32 buf_get_u32(u8* buffer, unsigned int first, unsigned int num);
u32 flip_u32(u32 value, unsigned int num);

const unsigned char bit_reverse_table256[] = 
{
  0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0, 
  0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8, 
  0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4, 
  0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC, 
  0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2, 
  0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
  0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6, 
  0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
  0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
  0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9, 
  0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
  0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
  0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3, 
  0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
  0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7, 
  0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};

int buf_set_u32(u8* buffer, unsigned int first, unsigned int num, u32 value)
{
	unsigned int i;
	
	if (!buffer)
		return ERROR_INVALID_ARGUMENTS;

	for (i=first; i<first+num; i++)
	{
		if (((value >> (i-first))&1) == 1)
			buffer[i/8] |= 1 << (i%8);
		else
			buffer[i/8] &= ~(1 << (i%8));
	}
	
	return ERROR_OK;
}

u32 buf_get_u32(u8* buffer, unsigned int first, unsigned int num)
{
	u32 result = 0;
	unsigned int i;
	
	if (!buffer)
	{
		ERROR("buffer not initialized");
		return 0;
	}

	for (i=first; i<first+num; i++)
	{
		if (((buffer[i/8]>>(i%8))&1) == 1)
			result |= 1 << (i-first);
	}
	
	return result;
}

u8* buf_cpy(u8 *from, u8 *to, int size)
{
	int num_bytes = CEIL(size, 8);
	unsigned int i;

	if (from == NULL)
		return NULL;

	for (i = 0; i < num_bytes; i++)
		to[i] = from[i];

	return to;
}

int buf_cmp(u8 *buf1, u8 *buf2, int size)
{
	int num_bytes = CEIL(size, 8);
	int i;

	if (!buf1 || !buf2)
		return 1;

	for (i = 0; i < num_bytes; i++)
	{
		if (buf1[i] != buf2[i])
			return 1;
	}

	return 0;
}

int buf_cmp_mask(u8 *buf1, u8 *buf2, u8 *mask, int size)
{
	int num_bytes = CEIL(size, 8);
	int i;

	for (i = 0; i < num_bytes; i++)
	{
		if ((buf1[i] & mask[i]) != (buf2[i] & mask[i]))
			return 1;
	}

	return 0;
}

u8* buf_set_ones(u8 *buf, int count)
{
	int num_bytes = CEIL(count, 8);
	int i;

	for (i = 0; i < num_bytes; i++)
	{
		if (count >= 8)
			buf[i] = 0xff;
		else
			buf[i] = (1 << count) - 1;
	
		count -= 8;
	}
	
	return buf;
}

u8* buf_set_buf(u8 *src, int src_start, u8 *dst, int dst_start, int len)
{
	int src_idx = src_start, dst_idx = dst_start;
	int i;
	
	for (i = 0; i < len; i++)
	{
		if (((src[src_idx/8] >> (src_idx % 8)) & 1) == 1)
			dst[dst_idx/8] |= 1 << (dst_idx%8);
		else
			dst[dst_idx/8] &= ~(1 << (dst_idx%8));
		dst_idx++;
		src_idx++;
	}

	return dst;
}

u32 flip_u32(u32 value, unsigned int num)
{
	u32 c;
	
	c = (bit_reverse_table256[value & 0xff] << 24) | 
		(bit_reverse_table256[(value >> 8) & 0xff] << 16) | 
		(bit_reverse_table256[(value >> 16) & 0xff] << 8) |
		(bit_reverse_table256[(value >> 24) & 0xff]);

	if (num < 32)
		c = c >> (32 - num);

	return c;
}

char* buf_to_char(u8 *buf, int size)
{
	int char_len = CEIL(size, 8) * 2;
	char *char_buf = malloc(char_len + 1);
	int i;
	int bits_left = size;
	
	char_buf[char_len] = 0;
	
	for (i = 0; i < CEIL(size, 8); i++)
	{
		if (bits_left < 8)
		{
			buf[i] &= ((1 << bits_left) - 1);
		}
		
		if (((buf[i] & 0x0f) >= 0) && ((buf[i] & 0x0f) <= 9))
			char_buf[char_len - 2*i - 1] = '0' + (buf[i] & 0xf);
		else
			char_buf[char_len - 2*i - 1] = 'a' + (buf[i] & 0xf) - 10;
		
		if (((buf[i] & 0xf0) >> 4 >= 0) && ((buf[i] & 0xf0) >> 4 <= 9))
			char_buf[char_len - 2*i - 2] = '0' + ((buf[i] & 0xf0) >> 4);
		else
			char_buf[char_len - 2*i - 2] = 'a' + ((buf[i] & 0xf0) >> 4) - 10;
		
	}

	return char_buf;
}

int char_to_buf(char *buf, int len, u8 *bin_buf, int buf_size)
{
	int bin_len = CEIL(len, 2);
	int i;
	
	if (buf_size < CEIL(bin_len, 8))
		return 0;
	
	if (len % 2)
		return 0;
	
	for (i = 0; i < strlen(buf); i++)
	{
		u32 tmp;
		sscanf(buf + 2*i, "%2x", &tmp);
		bin_buf[i] = tmp & 0xff;
	}
	
	return bin_len * 8;
}

int buf_to_u32_handler(u8 *in_buf, void *priv)
{
	u32 *dest = priv;
	
	*dest = buf_get_u32(in_buf, 0, 32);
	
	return ERROR_OK;
}
