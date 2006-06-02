/***************************************************************************
 *   Copyright (C) 2005 by Dominic Rath                                    *
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
#ifndef EMBEDDED_ICE_H
#define EMBEDDED_ICE_H

#include "target.h"
#include "register.h"
#include "arm_jtag.h"

enum
{
	EICE_DBG_CTRL = 0,
	EICE_DBG_STAT = 1,
	EICE_COMMS_CTRL = 2,
	EICE_COMMS_DATA = 3,
	EICE_W0_ADDR_VALUE = 4,
	EICE_W0_ADDR_MASK = 5,
	EICE_W0_DATA_VALUE  = 6,
	EICE_W0_DATA_MASK = 7,
	EICE_W0_CONTROL_VALUE = 8,
	EICE_W0_CONTROL_MASK = 9,
	EICE_W1_ADDR_VALUE = 10,
	EICE_W1_ADDR_MASK = 11,
	EICE_W1_DATA_VALUE = 12,
	EICE_W1_DATA_MASK = 13,
	EICE_W1_CONTROL_VALUE = 14,
	EICE_W1_CONTROL_MASK = 15
};

enum
{
	EICE_DBG_CONTROL_INTDIS = 2,
	EICE_DBG_CONTROL_DBGRQ = 1,
	EICE_DBG_CONTROL_DBGACK = 0,
};

enum
{
	EICE_DBG_STATUS_ITBIT = 4,
	EICE_DBG_STATUS_SYSCOMP = 3,
	EICE_DBG_STATUS_IFEN = 2,
	EICE_DBG_STATUS_DBGRQ = 1,
	EICE_DBG_STATUS_DBGACK = 0
};

enum
{
	EICE_W_CTRL_ENABLE = 0x100,
	EICE_W_CTRL_RANGE = 0x80,
	EICE_W_CTRL_CHAIN = 0x40,
	EICE_W_CTRL_EXTERN = 0x20,
	EICE_W_CTRL_nTRANS = 0x10,
	EICE_W_CTRL_nOPC = 0x8,
	EICE_W_CTRL_MAS = 0x6,
	EICE_W_CTRL_ITBIT = 0x2,
	EICE_W_CTRL_nRW = 0x1
};

typedef struct embeddedice_reg_s
{
	int addr;
	arm_jtag_t *jtag_info;
} embeddedice_reg_t;

extern reg_cache_t* embeddedice_build_reg_cache(target_t *target, arm_jtag_t *jtag_info, int extra_reg);
extern int embeddedice_read_reg(reg_t *reg);
extern int embeddedice_write_reg(reg_t *reg, u32 value);
extern int embeddedice_read_reg_w_check(reg_t *reg, u8* check_value, u8* check_mask);
extern int embeddedice_store_reg(reg_t *reg);
extern int embeddedice_set_reg(reg_t *reg, u32 value);
extern int embeddedice_set_reg_w_exec(reg_t *reg, u32 value);

#endif /* EMBEDDED_ICE_H */
