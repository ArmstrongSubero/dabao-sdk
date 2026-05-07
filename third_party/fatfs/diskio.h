/*-----------------------------------------------------------------------
/  Low level disk interface module include file   (C)ChaN, 2014
/  Ported for Baochip-1x / Dabao Board
/-----------------------------------------------------------------------*/

#include "ff.h"
#ifndef _DISKIO_DEFINED
#define _DISKIO_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

#define _USE_WRITE	1
#define _USE_IOCTL	1

typedef BYTE	DSTATUS;

typedef enum {
	RES_OK = 0,
	RES_ERROR,
	RES_WRPRT,
	RES_NOTRDY,
	RES_PARERR
} DRESULT;

DSTATUS disk_initialize (BYTE pdrv);
DSTATUS disk_status (BYTE pdrv);
DRESULT disk_read (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
#if	_USE_WRITE
DRESULT disk_write (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
#endif
#if	_USE_IOCTL
DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void* buff);
#endif

#define STA_NOINIT		0x01
#define STA_NODISK		0x02
#define STA_PROTECT		0x04

#define CTRL_SYNC			0
#define GET_SECTOR_COUNT	1
#define GET_SECTOR_SIZE		2
#define GET_BLOCK_SIZE		3
#define CTRL_TRIM			4

#define CT_MMC		0x01
#define CT_SD1		0x02
#define CT_SD2		0x04
#define CT_SDC		(CT_SD1|CT_SD2)
#define CT_BLOCK	0x08

#ifdef __cplusplus
}
#endif

#endif
