/***************************************************************************
 *   (c) Copyright 2007, 2008 by Christopher Kilgour                       *
 *   techie |_at_| whiterocker |_dot_| com                                 *
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "log.h"
#include "tms470.h"
#include <string.h>
#include <unistd.h>

int 
tms470_register_commands( struct command_context_s *cmd_ctx );

int 
tms470_flash_bank_command( struct command_context_s *cmd_ctx, 
                           char *cmd, 
                           char **args, 
                           int argc, 
                           struct flash_bank_s *bank );

int 
tms470_erase( struct flash_bank_s *bank, 
              int first, 
              int last );

int 
tms470_protect( struct flash_bank_s *bank, 
                int set, 
                int first, 
                int last );

int 
tms470_write( struct flash_bank_s *bank, 
              u8 *buffer, 
              u32 offset, 
              u32 count );

int 
tms470_probe( struct flash_bank_s *bank );

int 
tms470_auto_probe( struct flash_bank_s *bank );

int 
tms470_erase_check( struct flash_bank_s *bank );

int 
tms470_protect_check( struct flash_bank_s *bank );

int 
tms470_info( struct flash_bank_s *bank, 
             char *buf, 
             int buf_size );

flash_driver_t tms470_flash =
  {
    .name =               "tms470",
    .register_commands =  tms470_register_commands,
    .flash_bank_command = tms470_flash_bank_command,
    .erase =              tms470_erase,
    .protect =            tms470_protect,
    .write =              tms470_write,
    .probe =              tms470_probe,
    .auto_probe =         tms470_auto_probe,
    .erase_check =        tms470_erase_check,
    .protect_check =      tms470_protect_check,
    .info =               tms470_info
  };

/* ---------------------------------------------------------------------- 
                      Internal Support, Helpers
   ---------------------------------------------------------------------- */

const flash_sector_t TMS470R1A256_SECTORS[] =
  {
    { 0x00000000, 0x00002000, -1, -1 },
    { 0x00002000, 0x00002000, -1, -1 },
    { 0x00004000, 0x00002000, -1, -1 },
    { 0x00006000, 0x00002000, -1, -1 },
    { 0x00008000, 0x00008000, -1, -1 },
    { 0x00010000, 0x00008000, -1, -1 },
    { 0x00018000, 0x00008000, -1, -1 },
    { 0x00020000, 0x00008000, -1, -1 },
    { 0x00028000, 0x00008000, -1, -1 },
    { 0x00030000, 0x00008000, -1, -1 },
    { 0x00038000, 0x00002000, -1, -1 },
    { 0x0003A000, 0x00002000, -1, -1 },
    { 0x0003C000, 0x00002000, -1, -1 },
    { 0x0003E000, 0x00002000, -1, -1 },
  };

#define TMS470R1A256_NUM_SECTORS \
  (sizeof(TMS470R1A256_SECTORS)/sizeof(TMS470R1A256_SECTORS[0]))

const flash_sector_t TMS470R1A288_BANK0_SECTORS[] =
  {
    { 0x00000000, 0x00002000, -1, -1 },
    { 0x00002000, 0x00002000, -1, -1 },
    { 0x00004000, 0x00002000, -1, -1 },
    { 0x00006000, 0x00002000, -1, -1 },
  };

#define TMS470R1A288_BANK0_NUM_SECTORS \
  (sizeof(TMS470R1A288_BANK0_SECTORS)/sizeof(TMS470R1A288_BANK0_SECTORS[0]))

const flash_sector_t TMS470R1A288_BANK1_SECTORS[] =
  {
    { 0x00040000, 0x00010000, -1, -1 },
    { 0x00050000, 0x00010000, -1, -1 },
    { 0x00060000, 0x00010000, -1, -1 },
    { 0x00070000, 0x00010000, -1, -1 },
  };

#define TMS470R1A288_BANK1_NUM_SECTORS \
  (sizeof(TMS470R1A288_BANK1_SECTORS)/sizeof(TMS470R1A288_BANK1_SECTORS[0]))

/* ---------------------------------------------------------------------- */

int 
tms470_read_part_info( struct flash_bank_s *bank )
{
  tms470_flash_bank_t *tms470_info = bank->driver_priv;
  target_t *target = bank->target;
  u32    device_ident_reg;
  u32    silicon_version;
  u32    technology_family;
  u32    rom_flash;
  u32    part_number;
  char * part_name;

  if (target->state != TARGET_HALTED)
    {
      WARNING( "Cannot communicate... target not halted." );
      return ERROR_TARGET_NOT_HALTED;
    }

  /* read and parse the device identification register */
  target_read_u32( target, 0xFFFFFFF0, &device_ident_reg );

  INFO( "device_ident_reg=0x%08x", device_ident_reg );
  
  if ((device_ident_reg & 7) == 0)
    {
      WARNING( "Cannot identify target as a TMS470 family." );
      return ERROR_FLASH_OPERATION_FAILED;
    }

  silicon_version = (device_ident_reg >> 12) & 0xF;
  technology_family = (device_ident_reg >> 11) & 1;
  rom_flash = (device_ident_reg >> 10) & 1;
  part_number = (device_ident_reg >> 3) & 0x7f;

  /*
   * If the part number is known, determine if the flash bank is valid
   * based on the base address being within the known flash bank
   * ranges.  Then fixup/complete the remaining fields of the flash
   * bank structure.
   */
  switch( part_number )
    {
    case 0x0a:
      part_name = "TMS470R1A256";

      if (bank->base >= 0x00040000)
        {
          ERROR( "No %s flash bank contains base address 0x%08x.", 
                 part_name, bank->base );
          return ERROR_FLASH_OPERATION_FAILED;
        }
      tms470_info->ordinal = 0;
      bank->base           = 0x00000000;
      bank->size           = 256*1024;
      bank->num_sectors    = TMS470R1A256_NUM_SECTORS;
      bank->sectors = malloc( sizeof( TMS470R1A256_SECTORS ) );
      if (!bank->sectors)
        {
          return ERROR_FLASH_OPERATION_FAILED;
        }
      (void) memcpy( bank->sectors,
                     TMS470R1A256_SECTORS,
                     sizeof( TMS470R1A256_SECTORS ) );
      break;

    case 0x2b:
      part_name = "TMS470R1A288";

      if ((bank->base >= 0x00000000) && (bank->base < 0x00008000))
        {
          tms470_info->ordinal = 0;
          bank->base           = 0x00000000;
          bank->size           = 32*1024;
          bank->num_sectors    = TMS470R1A288_BANK0_NUM_SECTORS;
          bank->sectors = malloc( sizeof( TMS470R1A288_BANK0_SECTORS ) );
          if (!bank->sectors)
            {
              return ERROR_FLASH_OPERATION_FAILED;
            }
          (void) memcpy( bank->sectors,
                         TMS470R1A288_BANK0_SECTORS,
                         sizeof( TMS470R1A288_BANK0_SECTORS ) );
        }
      else if ((bank->base >= 0x00040000) && (bank->base < 0x00080000))
        {
          tms470_info->ordinal = 1;
          bank->base           = 0x00040000;
          bank->size           = 256*1024;
          bank->num_sectors    = TMS470R1A288_BANK1_NUM_SECTORS;
          bank->sectors = malloc( sizeof( TMS470R1A288_BANK1_SECTORS ) );
          if (!bank->sectors)
            {
              return ERROR_FLASH_OPERATION_FAILED;
            }
          (void) memcpy( bank->sectors,
                         TMS470R1A288_BANK1_SECTORS,
                         sizeof( TMS470R1A288_BANK1_SECTORS ) );
        }
      else
        {
          ERROR( "No %s flash bank contains base address 0x%08x.", 
                 part_name, bank->base );
          return ERROR_FLASH_OPERATION_FAILED;
        }
      break;

    default:
      WARNING( "Could not identify part 0x%02x as a member of the TMS470 family.", 
               part_number );
      return ERROR_FLASH_OPERATION_FAILED;
    }

  /* turn off memory selects */
  target_write_u32( target, 0xFFFFFFE4, 0x00000000 );
  target_write_u32( target, 0xFFFFFFE0, 0x00000000 );

  bank->chip_width = 32;
  bank->bus_width  = 32;
  
  INFO( "Identified %s, ver=%d, core=%s, nvmem=%s.",
        part_name,
        silicon_version,
        (technology_family ? "1.8v" : "3.3v"),
        (rom_flash ? "rom" : "flash") );

  tms470_info->device_ident_reg  = device_ident_reg;
  tms470_info->silicon_version   = silicon_version;
  tms470_info->technology_family = technology_family;
  tms470_info->rom_flash         = rom_flash;
  tms470_info->part_number       = part_number;
  tms470_info->part_name         = part_name;

  /*
   * Disable reset on address access violation.
   */
  target_write_u32( target, 0xFFFFFFE0, 0x00004007 );

  return ERROR_OK;
}

/* ---------------------------------------------------------------------- */

u32 keysSet = 0;
u32 flashKeys[4];

int 
tms470_handle_flash_keyset_command( struct command_context_s * cmd_ctx, 
                                    char *                     cmd, 
                                    char **                    args, 
                                    int                        argc )
{
  if (argc > 4)
    {
      command_print( cmd_ctx, "tms470 flash_keyset <key0> <key1> <key2> <key3>" );
      return ERROR_INVALID_ARGUMENTS;
    }
  else if (argc == 4)
    {
      int i;

      for( i=0; i<4; i++ )
        {
          int start = (0 == strncmp( args[i], "0x", 2 )) ? 2 : 0;
          if (1 != sscanf( &args[i][start], "%x", &flashKeys[i] ))
            {
              command_print( cmd_ctx, "could not process flash key %s", args[i] );
              ERROR( "could not process flash key %s", args[i] );
              return ERROR_INVALID_ARGUMENTS;
            }
        }

      keysSet = 1;
    }
  else if (argc != 0)
    {
      command_print( cmd_ctx, "tms470 flash_keyset <key0> <key1> <key2> <key3>" );
      return ERROR_INVALID_ARGUMENTS;
    }

  if (keysSet)
    {
      command_print( cmd_ctx, "using flash keys 0x%08x, 0x%08x, 0x%08x, 0x%08x",
                     flashKeys[0], flashKeys[1], flashKeys[2], flashKeys[3] );
    }
  else
    {
      command_print( cmd_ctx, "flash keys not set" );
    }

  return ERROR_OK;
}

const u32 FLASH_KEYS_ALL_ONES[] = { 0xFFFFFFFF, 0xFFFFFFFF, 
                                    0xFFFFFFFF, 0xFFFFFFFF, };

const u32 FLASH_KEYS_ALL_ZEROS[] = { 0x00000000, 0x00000000, 
                                     0x00000000, 0x00000000, };

const u32 FLASH_KEYS_MIX1[] = { 0xf0fff0ff, 0xf0fff0ff,
                                0xf0fff0ff, 0xf0fff0ff };

const u32 FLASH_KEYS_MIX2[] = { 0x0000ffff, 0x0000ffff,
                                0x0000ffff, 0x0000ffff };

/* ---------------------------------------------------------------------- */

int oscMHz = 12;

int
tms470_handle_osc_megahertz_command( struct command_context_s * cmd_ctx, 
                                     char *                     cmd, 
                                     char **                    args, 
                                     int                        argc )
{
  if (argc > 1)
    {
      command_print( cmd_ctx, "tms470 osc_megahertz <MHz>" );
      return ERROR_INVALID_ARGUMENTS;
    }
  else if (argc == 1)
    {
      sscanf( args[0], "%d", &oscMHz );
    }

  if (oscMHz <= 0)
    {
      ERROR( "osc_megahertz must be positive and non-zero!" );
      command_print( cmd_ctx, "osc_megahertz must be positive and non-zero!" );
      oscMHz = 12;
      return ERROR_INVALID_ARGUMENTS;
    }

  command_print( cmd_ctx, "osc_megahertz=%d", oscMHz );

  return ERROR_OK;
}

/* ---------------------------------------------------------------------- */

int plldis = 0;

int
tms470_handle_plldis_command( struct command_context_s * cmd_ctx, 
                              char *                     cmd, 
                              char **                    args, 
                              int                        argc )
{
  if (argc > 1)
    {
      command_print( cmd_ctx, "tms470 plldis <0|1>" );
      return ERROR_INVALID_ARGUMENTS;
    }
  else if (argc == 1)
    {
      sscanf( args[0], "%d", &plldis );
      plldis = plldis ? 1 : 0;
    }

  command_print( cmd_ctx, "plldis=%d", plldis );

  return ERROR_OK;
}

/* ---------------------------------------------------------------------- */

int
tms470_check_flash_unlocked( target_t * target )
{
  u32 fmbbusy;

  target_read_u32( target, 0xFFE89C08, &fmbbusy );
  INFO( "tms470 fmbbusy=0x%08x -> %s", 
         fmbbusy,
         fmbbusy & 0x8000 ? "unlocked" : "LOCKED" );
  return fmbbusy & 0x8000 ? ERROR_OK : ERROR_FLASH_OPERATION_FAILED;
}

/* ---------------------------------------------------------------------- */

int
tms470_try_flash_keys( target_t *  target, 
                       const u32 * key_set )
{
  u32 glbctrl, fmmstat;
  int retval = ERROR_FLASH_OPERATION_FAILED;

  /* set GLBCTRL.4  */
  target_read_u32( target, 0xFFFFFFDC, &glbctrl );
  target_write_u32( target, 0xFFFFFFDC, glbctrl | 0x10 );

  /* only perform the key match when 3VSTAT is clear */
  target_read_u32( target, 0xFFE8BC0C, &fmmstat );
  if (!(fmmstat & 0x08))
    {
      unsigned i;
      u32 fmmac2, fmbptr, fmbac2, fmbbusy, orig_fmregopt;
      
      target_write_u32( target, 0xFFE8BC04, fmmstat & ~0x07 );

      /* wait for pump ready */
      do
        {
          target_read_u32( target, 0xFFE8A814, &fmbptr );
          usleep( 1000 );
        }
      while( !(fmbptr & 0x0200) );

      /* force max wait states */
      target_read_u32( target, 0xFFE88004, &fmbac2 );
      target_write_u32( target, 0xFFE88004, fmbac2 | 0xff );

      /* save current access mode, force normal read mode */
      target_read_u32( target, 0xFFE89C00, &orig_fmregopt );
      target_write_u32( target, 0xFFE89C00, 0x00 );

      for( i=0; i<4; i++ )
        {
          u32 tmp;

          /* There is no point displaying the value of tmp, it is
           * filtered by the chip.  The purpose of this read is to
           * prime the unlocking logic rather than read out the value.
           */
          target_read_u32( target, 0x00001FF0+4*i, &tmp );

          INFO( "tms470 writing fmpkey=0x%08x", key_set[i] );
          target_write_u32( target, 0xFFE89C0C, key_set[i] );
        }

      if (ERROR_OK == tms470_check_flash_unlocked( target ))
        {
          /* 
           * There seems to be a side-effect of reading the FMPKEY
           * register in that it re-enables the protection.  So we
           * re-enable it.
           */
          for( i=0; i<4; i++ )
            {
              u32 tmp;
              target_read_u32( target, 0x00001FF0+4*i, &tmp );
              target_write_u32( target, 0xFFE89C0C, key_set[i] );
            }
          retval = ERROR_OK;
        }

      /* restore settings */
      target_write_u32( target, 0xFFE89C00, orig_fmregopt );
      target_write_u32( target, 0xFFE88004, fmbac2 );
    }

  /* clear config bit */
  target_write_u32( target, 0xFFFFFFDC, glbctrl );

  return retval;
}

/* ---------------------------------------------------------------------- */

int
tms470_unlock_flash( struct flash_bank_s * bank )
{
  tms470_flash_bank_t * tms470_info = bank->driver_priv;
  target_t *            target =      bank->target;
  const u32 *           p_key_sets[5];
  unsigned              i, key_set_count;

  if (keysSet)
    {
      p_key_sets[0] = flashKeys;
      p_key_sets[1] = FLASH_KEYS_ALL_ONES;
      p_key_sets[2] = FLASH_KEYS_ALL_ZEROS;
      p_key_sets[3] = FLASH_KEYS_MIX1;
      p_key_sets[4] = FLASH_KEYS_MIX2;
    }
  else
    {
      key_set_count = 4;
      p_key_sets[0] = FLASH_KEYS_ALL_ONES;
      p_key_sets[1] = FLASH_KEYS_ALL_ZEROS;
      p_key_sets[2] = FLASH_KEYS_MIX1;
      p_key_sets[3] = FLASH_KEYS_MIX2;
    }

  for( i=0; i<key_set_count; i++ )
    {
      if (tms470_try_flash_keys( target, p_key_sets[i] ) == ERROR_OK)
        {
          INFO( "tms470 flash is unlocked" );
          return ERROR_OK;
        }
    }

  WARNING( "tms470 could not unlock flash memory protection level 2" );
  return ERROR_FLASH_OPERATION_FAILED;
}

/* ---------------------------------------------------------------------- */

int
tms470_flash_initialize_internal_state_machine( struct flash_bank_s * bank )
{
  u32 fmmac2, fmmac1, fmmaxep, k, delay, glbctrl, sysclk;
  target_t *target = bank->target;
  tms470_flash_bank_t * tms470_info = bank->driver_priv;
  int result = ERROR_OK;

  /*
   * Select the desired bank to be programmed by writing BANK[2:0] of
   * FMMAC2.
   */
  target_read_u32( target, 0xFFE8BC04, &fmmac2 );
  fmmac2 &= ~0x0007;
  fmmac2 |= (tms470_info->ordinal & 7);
  target_write_u32( target, 0xFFE8BC04, fmmac2 );
  DEBUG( "set fmmac2=0x%04x", fmmac2 );

  /*
   * Disable level 1 sector protection by setting bit 15 of FMMAC1.
   */
  target_read_u32( target, 0xFFE8BC00, &fmmac1 );
  fmmac1 |= 0x8000;
  target_write_u32( target, 0xFFE8BC00, fmmac1 );
  DEBUG( "set fmmac1=0x%04x", fmmac1 );

  /*
   * FMTCREG=0x2fc0;
   */
  target_write_u32( target, 0xFFE8BC10, 0x2fc0 );
  DEBUG( "set fmtcreg=0x2fc0" );

  /*
   * MAXPP=50
   */
  target_write_u32( target, 0xFFE8A07C, 50 );
  DEBUG( "set fmmaxpp=50" );

  /*
   * MAXCP=0xf000+2000
   */
  target_write_u32( target, 0xFFE8A084, 0xf000+2000 );
  DEBUG( "set fmmaxcp=0x%04x", 0xf000+2000 );

    /*
   * configure VHV
   */
  target_read_u32( target, 0xFFE8A080, &fmmaxep );
  if (fmmaxep == 0xf000) 
    {
      fmmaxep = 0xf000+4095;
      target_write_u32( target, 0xFFE8A80C, 0x9964 );
      DEBUG( "set fmptr3=0x9964" );
    }
  else
    {
      fmmaxep = 0xa000+4095;
      target_write_u32( target, 0xFFE8A80C, 0x9b64 );
      DEBUG( "set fmptr3=0x9b64" );
    }
  target_write_u32( target, 0xFFE8A080, fmmaxep );
  DEBUG( "set fmmaxep=0x%04x", fmmaxep );

  /*
   * FMPTR4=0xa000
   */
  target_write_u32( target, 0xFFE8A810, 0xa000 );
  DEBUG( "set fmptr4=0xa000" );

  /*
   * FMPESETUP, delay parameter selected based on clock frequency.
   *
   * According to the TI App Note SPNU257 and flashing code, delay is
   * int((sysclk(MHz) + 1) / 2), with a minimum of 5.  The system
   * clock is usually derived from the ZPLL module, and selected by
   * the plldis global.
   */
  target_read_u32( target, 0xFFFFFFDC, &glbctrl );
  sysclk = (plldis ? 1 : (glbctrl & 0x08) ? 4 : 8 ) * oscMHz / (1 + (glbctrl & 7));
  delay = (sysclk > 10) ? (sysclk + 1) / 2 : 5;
  target_write_u32( target, 0xFFE8A018, (delay<<4)|(delay<<8) );
  DEBUG( "set fmpsetup=0x%04x", (delay<<4)|(delay<<8) );

  /*
   * FMPVEVACCESS, based on delay.
   */
  k = delay|(delay<<8);
  target_write_u32( target, 0xFFE8A05C, k );
  DEBUG( "set fmpvevaccess=0x%04x", k );
  
  /*
   * FMPCHOLD, FMPVEVHOLD, FMPVEVSETUP, based on delay.
   */
  k <<= 1;
  target_write_u32( target, 0xFFE8A034, k );
  DEBUG( "set fmpchold=0x%04x", k );
  target_write_u32( target, 0xFFE8A040, k );
  DEBUG( "set fmpvevhold=0x%04x", k );
  target_write_u32( target, 0xFFE8A024, k );
  DEBUG( "set fmpvevsetup=0x%04x", k );

  /*
   * FMCVACCESS, based on delay.
   */
  k = delay*16;
  target_write_u32( target, 0xFFE8A060, k );
  DEBUG( "set fmcvaccess=0x%04x", k );

  /*
   * FMCSETUP, based on delay.
   */
  k = 0x3000 | delay*20;
  target_write_u32( target, 0xFFE8A020, k );
  DEBUG( "set fmcsetup=0x%04x", k );

  /*
   * FMEHOLD, based on delay.
   */
  k = (delay*20) << 2;
  target_write_u32( target, 0xFFE8A038, k );
  DEBUG( "set fmehold=0x%04x", k );

  /*
   * PWIDTH, CWIDTH, EWIDTH, based on delay.
   */
  target_write_u32( target, 0xFFE8A050, delay*8 );
  DEBUG( "set fmpwidth=0x%04x", delay*8 );
  target_write_u32( target, 0xFFE8A058, delay*1000 );
  DEBUG( "set fmcwidth=0x%04x", delay*1000 );
  target_write_u32( target, 0xFFE8A054, delay*5400 );
  DEBUG( "set fmewidth=0x%04x", delay*5400 );

  return result;
}
                                                

/* ---------------------------------------------------------------------- */

int
tms470_flash_status( struct flash_bank_s * bank )
{
  target_t *target = bank->target;
  int result = ERROR_OK;
  u32 fmmstat;

  target_read_u32( target, 0xFFE8BC0C, &fmmstat );
  DEBUG( "set fmmstat=0x%04x", fmmstat );

  if (fmmstat & 0x0080)
    {
      WARNING( "tms470 flash command: erase still active after busy clear." );
      result = ERROR_FLASH_OPERATION_FAILED;
    }

  if (fmmstat & 0x0040)
    {
      WARNING( "tms470 flash command: program still active after busy clear." );
      result = ERROR_FLASH_OPERATION_FAILED;
    }

  if (fmmstat & 0x0020)
    {
      WARNING( "tms470 flash command: invalid data command." );
      result = ERROR_FLASH_OPERATION_FAILED;
    }

  if (fmmstat & 0x0010)
    {
      WARNING( "tms470 flash command: program, erase or validate sector failed." );
      result = ERROR_FLASH_OPERATION_FAILED;
    }

  if (fmmstat & 0x0008)
    {
      WARNING( "tms470 flash command: voltage instability detected." );
      result = ERROR_FLASH_OPERATION_FAILED;
    }

  if (fmmstat & 0x0006)
    {
      WARNING( "tms470 flash command: command suspend detected." );
      result = ERROR_FLASH_OPERATION_FAILED;
    }

  if (fmmstat & 0x0001)
    {
      WARNING( "tms470 flash command: sector was locked." );
      result = ERROR_FLASH_OPERATION_FAILED;
    }

  return result;
}

/* ---------------------------------------------------------------------- */

int
tms470_erase_sector( struct flash_bank_s * bank, 
                     int                   sector )
{
  u32 glbctrl, orig_fmregopt, fmbsea, fmbseb, fmmstat;
  target_t *target = bank->target;
  u32 flashAddr = bank->base + bank->sectors[sector].offset;
  int result = ERROR_OK;

  /* 
   * Set the bit GLBCTRL4 of the GLBCTRL register (in the System
   * module) to enable writing to the flash registers }.
   */
  target_read_u32( target, 0xFFFFFFDC, &glbctrl );
  target_write_u32( target, 0xFFFFFFDC, glbctrl | 0x10 );
  DEBUG( "set glbctrl=0x%08x", glbctrl | 0x10 );

  /* Force normal read mode. */
  target_read_u32( target, 0xFFE89C00, &orig_fmregopt );
  target_write_u32( target, 0xFFE89C00, 0 );
  DEBUG( "set fmregopt=0x%08x", 0 );

  (void) tms470_flash_initialize_internal_state_machine( bank );
  
  /*
   * Select one or more bits in FMBSEA or FMBSEB to disable Level 1
   * protection for the particular sector to be erased/written.
   */
  if (sector < 16)
    {
      target_read_u32( target, 0xFFE88008, &fmbsea );
      target_write_u32( target, 0xFFE88008, fmbsea | (1<<sector) );
      DEBUG( "set fmbsea=0x%04x", fmbsea | (1<<sector) );
    }
  else
    {
      target_read_u32( target, 0xFFE8800C, &fmbseb );
      target_write_u32( target, 0xFFE8800C, fmbseb | (1<<(sector-16)) );
      DEBUG( "set fmbseb=0x%04x", fmbseb | (1<<(sector-16)) );
    }
  bank->sectors[sector].is_protected = 0;

  /* 
   * clear status regiser, sent erase command, kickoff erase 
   */
  target_write_u16( target, flashAddr, 0x0040 );
  DEBUG( "write *(u16 *)0x%08x=0x0040", flashAddr );
  target_write_u16( target, flashAddr, 0x0020 );
  DEBUG( "write *(u16 *)0x%08x=0x0020", flashAddr );
  target_write_u16( target, flashAddr, 0xffff );
  DEBUG( "write *(u16 *)0x%08x=0xffff", flashAddr );

  /*
   * Monitor FMMSTAT, busy until clear, then check and other flags for
   * ultimate result of the operation.
   */
  do
    {
      target_read_u32( target, 0xFFE8BC0C, &fmmstat );
      if (fmmstat & 0x0100)
        {
          usleep( 1000 );
        }
    }
  while( fmmstat & 0x0100 );

  result = tms470_flash_status( bank );

  if (sector < 16)
    {
      target_write_u32( target, 0xFFE88008, fmbsea );
      DEBUG( "set fmbsea=0x%04x", fmbsea );
      bank->sectors[sector].is_protected = 
        fmbsea & (1<<sector) ? 0 : 1;
    }
  else
    {
      target_write_u32( target, 0xFFE8800C, fmbseb );
      DEBUG( "set fmbseb=0x%04x", fmbseb );
      bank->sectors[sector].is_protected = 
        fmbseb & (1<<(sector-16)) ? 0 : 1;
    }
  target_write_u32( target, 0xFFE89C00, orig_fmregopt );
  DEBUG( "set fmregopt=0x%08x", orig_fmregopt );
  target_write_u32( target, 0xFFFFFFDC, glbctrl );
  DEBUG( "set glbctrl=0x%08x", glbctrl );

  if (result == ERROR_OK)
    {
      bank->sectors[sector].is_erased = 1;
    }

  return result;
}

/* ---------------------------------------------------------------------- 
              Implementation of Flash Driver Interfaces
   ---------------------------------------------------------------------- */

int 
tms470_register_commands( struct command_context_s *cmd_ctx )
{
  command_t *tms470_cmd = register_command( cmd_ctx, 
                                            NULL, 
                                            "tms470", 
                                            NULL, 
                                            COMMAND_ANY, 
                                            "applies to TI tms470 family" );

  register_command( cmd_ctx, 
                    tms470_cmd, 
                    "flash_keyset", 
                    tms470_handle_flash_keyset_command, 
                    COMMAND_ANY,
                   "tms470 flash_keyset <key0> <key1> <key2> <key3>" );

  register_command( cmd_ctx,
                    tms470_cmd,
                    "osc_megahertz",
                    tms470_handle_osc_megahertz_command,
                    COMMAND_ANY,
                    "tms470 osc_megahertz <MHz>" );

  register_command( cmd_ctx,
                    tms470_cmd,
                    "plldis",
                    tms470_handle_plldis_command,
                    COMMAND_ANY,
                    "tms470 plldis <0/1>" );
  
  return ERROR_OK;
}

/* ---------------------------------------------------------------------- */

int 
tms470_erase( struct flash_bank_s * bank, 
              int                   first, 
              int                   last )
{
  tms470_flash_bank_t *tms470_info = bank->driver_priv;
  target_t *target = bank->target;
  int sector, result = ERROR_OK;

  if (!tms470_info->device_ident_reg)
    {
      tms470_read_part_info( bank );
    }

  if ((first < 0) || 
      (first >= bank->num_sectors) ||
      (last < 0) ||
      (last >= bank->num_sectors) ||
      (first > last))
    {
      ERROR( "Sector range %d to %d invalid.", first, last );
      return ERROR_FLASH_SECTOR_INVALID;
    }

  result = tms470_unlock_flash( bank );
  if (result != ERROR_OK)
    {
      return result;
    }

  for( sector=first; sector<=last; sector++ )
    {
      INFO( "Erasing tms470 bank %d sector %d...",
            tms470_info->ordinal, sector );

      result = tms470_erase_sector( bank, sector );

      if (result != ERROR_OK)
        {
          ERROR( "tms470 could not erase flash sector." );
          break;
        }
      else
        {
          INFO( "sector erased successfully." );
        }
    }
  
  return result;
}

/* ---------------------------------------------------------------------- */

int 
tms470_protect( struct flash_bank_s * bank, 
                int                   set, 
                int                   first, 
                int                   last )
{
  tms470_flash_bank_t *tms470_info = bank->driver_priv;
  target_t *target = bank->target;
  u32 fmmac2, fmbsea, fmbseb;
  int sector;

  if (!tms470_info->device_ident_reg)
    {
      tms470_read_part_info( bank );
    }

  if ((first < 0) || 
      (first >= bank->num_sectors) ||
      (last < 0) ||
      (last >= bank->num_sectors) ||
      (first > last))
    {
      ERROR( "Sector range %d to %d invalid.", first, last );
      return ERROR_FLASH_SECTOR_INVALID;
    }

  /* enable the appropriate bank */
  target_read_u32( target, 0xFFE8BC04, &fmmac2 );
  target_write_u32( target, 0xFFE8BC04, 
                    (fmmac2 & ~7) | tms470_info->ordinal );

  /* get the original sector proection flags for this bank */
  target_read_u32( target, 0xFFE88008, &fmbsea );
  target_read_u32( target, 0xFFE8800C, &fmbseb );
  
  for( sector=0; sector<bank->num_sectors; sector++ )
    {
      if (sector < 16)
        {
          fmbsea = set ? fmbsea & ~(1<<sector) : 
                         fmbsea | (1<<sector);
          bank->sectors[sector].is_protected = set ? 1 : 0;
        }
      else
        {
          fmbseb = set ? fmbseb & ~(1<<(sector-16)) : 
                         fmbseb | (1<<(sector-16));
          bank->sectors[sector].is_protected = set ? 1 : 0;
        }
    }

  /* update the protection bits */
  target_write_u32( target, 0xFFE88008, fmbsea );
  target_write_u32( target, 0xFFE8800C, fmbseb );

  return ERROR_OK;
}

/* ---------------------------------------------------------------------- */

int 
tms470_write( struct flash_bank_s * bank, 
              u8 *                  buffer, 
              u32                   offset, 
              u32                   count )
{
  target_t *target = bank->target;
  tms470_flash_bank_t *tms470_info = bank->driver_priv;
  u32 glbctrl, fmbac2, orig_fmregopt, fmbsea, fmbseb, fmmaxpp, fmmstat;
  int i, result = ERROR_OK;

  if (!tms470_info->device_ident_reg)
    {
      tms470_read_part_info( bank );
    }

  INFO( "Writing %d bytes starting at 0x%08x",
        count, bank->base + offset );

  /* set GLBCTRL.4  */
  target_read_u32( target, 0xFFFFFFDC, &glbctrl );
  target_write_u32( target, 0xFFFFFFDC, glbctrl | 0x10 );

  (void) tms470_flash_initialize_internal_state_machine( bank );

  /* force max wait states */
  target_read_u32( target, 0xFFE88004, &fmbac2 );
  target_write_u32( target, 0xFFE88004, fmbac2 | 0xff );

  /* save current access mode, force normal read mode */
  target_read_u32( target, 0xFFE89C00, &orig_fmregopt );
  target_write_u32( target, 0xFFE89C00, 0x00 );

  /*
   * Disable Level 1 protection for all sectors to be erased/written.
   */
  target_read_u32( target, 0xFFE88008, &fmbsea );
  target_write_u32( target, 0xFFE88008, 0xffff );
  target_read_u32( target, 0xFFE8800C, &fmbseb );
  target_write_u32( target, 0xFFE8800C, 0xffff );

  /* read MAXPP */
  target_read_u32( target, 0xFFE8A07C, &fmmaxpp );

  for( i=0; i<count; i+=2 )
    {
      u32 addr = bank->base + offset + i;
      u16 word = (((u16) buffer[i]) << 8) | (u16) buffer[i+1];

      if (word != 0xffff)
        {
          INFO( "writing 0x%04x at 0x%08x", word, addr );

          /* clear status register */
          target_write_u16( target, addr, 0x0040 );
          /* program flash command */
          target_write_u16( target, addr, 0x0010 );
          /* burn the 16-bit word (big-endian) */
          target_write_u16( target, addr, word );

          /*
           * Monitor FMMSTAT, busy until clear, then check and other flags
           * for ultimate result of the operation.
           */
          do
            {
              target_read_u32( target, 0xFFE8BC0C, &fmmstat );
              if (fmmstat & 0x0100)
                {
                  usleep( 1000 );
                }
            }
          while( fmmstat & 0x0100 );

          if (fmmstat & 0x3ff)
            {
              ERROR( "fmstat=0x%04x", fmmstat );
              ERROR( "Could not program word 0x%04x at address 0x%08x.",
                     word, addr );
              result = ERROR_FLASH_OPERATION_FAILED;
              break;
            }
        }
      else
        {
          INFO( "skipping 0xffff at 0x%08x", addr );
        }
    }

  /* restore */
  target_write_u32( target, 0xFFE88008, fmbsea );
  target_write_u32( target, 0xFFE8800C, fmbseb );
  target_write_u32( target, 0xFFE88004, fmbac2 );
  target_write_u32( target, 0xFFE89C00, orig_fmregopt );
  target_write_u32( target, 0xFFFFFFDC, glbctrl );

  return result;
}

/* ---------------------------------------------------------------------- */

int 
tms470_probe( struct flash_bank_s * bank )
{
  tms470_flash_bank_t * tms470_info = bank->driver_priv;

  tms470_info->probed = 0;
  
  if (!tms470_info->device_ident_reg)
    {
      tms470_read_part_info( bank );
    }
  
  tms470_info->probed = 1;

  return ERROR_OK;
}

int 
tms470_auto_probe( struct flash_bank_s * bank )
{
	tms470_flash_bank_t * tms470_info = bank->driver_priv;
	if (tms470_info->probed)
		return ERROR_OK;
	return tms470_probe(bank);
}
/* ---------------------------------------------------------------------- */

int 
tms470_erase_check( struct flash_bank_s * bank )
{
  target_t *target = bank->target;
  tms470_flash_bank_t * tms470_info = bank->driver_priv;
  int sector, result = ERROR_OK;
  u32 fmmac2, fmbac2, glbctrl, orig_fmregopt;
  static u8 buffer[64*1024];

  if (!tms470_info->device_ident_reg)
    {
      tms470_read_part_info( bank );
    }

  /* set GLBCTRL.4  */
  target_read_u32( target, 0xFFFFFFDC, &glbctrl );
  target_write_u32( target, 0xFFFFFFDC, glbctrl | 0x10 );

  /* save current access mode, force normal read mode */
  target_read_u32( target, 0xFFE89C00, &orig_fmregopt );
  target_write_u32( target, 0xFFE89C00, 0x00 );

  /* enable the appropriate bank */
  target_read_u32( target, 0xFFE8BC04, &fmmac2 );
  target_write_u32( target, 0xFFE8BC04, 
                    (fmmac2 & ~7) | tms470_info->ordinal );

  /* TCR=0 */
  target_write_u32( target, 0xFFE8BC10, 0x2fc0 );

  /* clear TEZ in fmbrdy */
  target_write_u32( target, 0xFFE88010, 0x0b );

  /* save current wait states, force max */
  target_read_u32( target, 0xFFE88004, &fmbac2 );
  target_write_u32( target, 0xFFE88004, fmbac2 | 0xff );

  /* 
   * The TI primitives inspect the flash memory by reading one 32-bit
   * word at a time.  Here we read an entire sector and inspect it in
   * an attempt to reduce the JTAG overhead.
   */
  for( sector=0; sector<bank->num_sectors; sector++ )
    {
      if (bank->sectors[sector].is_erased != 1)
        {
          u32 i, addr = bank->base + bank->sectors[sector].offset;
          
          INFO( "checking flash bank %d sector %d",
                tms470_info->ordinal,
                sector );

          target_read_buffer( target, 
                              addr,
                              bank->sectors[sector].size,
                              buffer );

          bank->sectors[sector].is_erased = 1;
          for( i=0; i<bank->sectors[sector].size; i++ )
            {
              if (buffer[i] != 0xff)
                {
                  WARNING( "tms470 bank %d, sector %d, not erased.",
                           tms470_info->ordinal,
                           sector );
                  WARNING( "at location 0x%08x: flash data is 0x%02x.",
                           addr+i, 
                           buffer[i] );
                           
                  bank->sectors[sector].is_erased = 0;
                  break;
                }
            }
        }
      if (bank->sectors[sector].is_erased != 1)
        {
          result = ERROR_FLASH_SECTOR_NOT_ERASED;
          break;
        }
      else
        {
          INFO( "sector erased" );
        }
    }

  /* reset TEZ, wait states, read mode, GLBCTRL.4 */
  target_write_u32( target, 0xFFE88010, 0x0f );
  target_write_u32( target, 0xFFE88004, fmbac2 );
  target_write_u32( target, 0xFFE89C00, orig_fmregopt );
  target_write_u32( target, 0xFFFFFFDC, glbctrl );

  return result;
}

/* ---------------------------------------------------------------------- */

int 
tms470_protect_check( struct flash_bank_s * bank )
{
  target_t *target = bank->target;
  tms470_flash_bank_t * tms470_info = bank->driver_priv;
  int sector, result = ERROR_OK;
  u32 fmmac2, fmbsea, fmbseb;

  if (!tms470_info->device_ident_reg)
    {
      tms470_read_part_info( bank );
    }

  /* enable the appropriate bank */
  target_read_u32( target, 0xFFE8BC04, &fmmac2 );
  target_write_u32( target, 0xFFE8BC04, 
                    (fmmac2 & ~7) | tms470_info->ordinal );

  target_read_u32( target, 0xFFE88008, &fmbsea );
  target_read_u32( target, 0xFFE8800C, &fmbseb );
  
  for( sector=0; sector<bank->num_sectors; sector++ )
    {
      int protected;

      if (sector < 16)
        {
          protected = fmbsea & (1<<sector) ? 0 : 1;
          bank->sectors[sector].is_protected = protected;
        }
      else
        {
          protected = fmbseb & (1<<(sector-16)) ? 0 : 1;
          bank->sectors[sector].is_protected = protected;
        }

      DEBUG( "bank %d sector %d is %s",
             tms470_info->ordinal,
             sector,
             protected ? "protected" : "not protected" );
    }

  return result;
}

/* ---------------------------------------------------------------------- */

int 
tms470_info( struct flash_bank_s * bank, 
             char *                buf, 
             int                   buf_size )
{
  int used = 0;
  tms470_flash_bank_t * tms470_info = bank->driver_priv;

  if (!tms470_info->device_ident_reg)
    {
      tms470_read_part_info( bank );
    }

  if (!tms470_info->device_ident_reg)
    {
      (void) snprintf(buf, buf_size, "Cannot identify target as a TMS470\n");
      return ERROR_FLASH_OPERATION_FAILED;
    }

  used += snprintf( buf, buf_size, 
                    "\ntms470 information: Chip is %s\n",
                    tms470_info->part_name );
  buf += used;
  buf_size -= used;

  used += snprintf( buf, buf_size,
                    "Flash protection level 2 is %s\n",
                    tms470_check_flash_unlocked( bank->target ) == ERROR_OK ? "disabled" : "enabled" );
  buf += used;
  buf_size -= used;

  return ERROR_OK;
}

/* ---------------------------------------------------------------------- */

/*
 * flash bank tms470 <base> <size> <chip_width> <bus_width> <target>
 * [options...]
 */

int 
tms470_flash_bank_command( struct command_context_s *cmd_ctx, 
                           char *cmd, 
                           char **args, 
                           int argc, 
                           struct flash_bank_s *bank )
{
  bank->driver_priv = malloc( sizeof( tms470_flash_bank_t ) );

  if (!bank->driver_priv)
    {
      return ERROR_FLASH_OPERATION_FAILED;
    }

  (void) memset( bank->driver_priv, 0, sizeof( tms470_flash_bank_t ) );

  return ERROR_OK;
}
