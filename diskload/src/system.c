/*
  Copyright 2009, Ifcaro
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.  
*/

#include "include/usbld.h"
#include "include/system.h"
#include "include/crc16.h"

extern unsigned char imgdrv_irx[];
extern unsigned int size_imgdrv_irx;

extern unsigned char eesync_irx[];
extern unsigned int size_eesync_irx;

extern unsigned char cdvdfsv_irx[];
extern unsigned int size_cdvdfsv_irx;

extern unsigned char cddev_irx[];
extern unsigned int size_cddev_irx;

extern unsigned char eecore_elf[];
extern unsigned int size_eecore_elf;

extern unsigned char alt_eecore_elf[];
extern unsigned int size_alt_eecore_elf;

#define ELF_MAGIC		0x464c457f
#define ELF_PT_LOAD		1

typedef struct {
	u8	ident[16];	// struct definition for ELF object header
	u16	type;
	u16	machine;
	u32	version;
	u32	entry;
	u32	phoff;
	u32	shoff;
	u32	flags;
	u16	ehsize;
	u16	phentsize;
	u16	phnum;
	u16	shentsize;
	u16	shnum;
	u16	shstrndx;
} elf_header_t;

typedef struct {
	u32	type;		// struct definition for ELF program section header
	u32	offset;
	void	*vaddr;
	u32	paddr;
	u32	filesz;
	u32	memsz;
	u32	flags;
	u32	align;
} elf_pheader_t;

typedef struct {
	void *irxaddr;
	int irxsize;
} irxptr_t;

// structs for DEVCTL commands
typedef struct
{
	u32 lba;
	u32 size;
	u8 data[0];
} hddAtaTransfer_t; 

//
// DEVCTL commands
//
#define APA_DEVCTL_ATA_READ		0x00006836// arg  = hddAtaTransfer_t

int sysPcmciaCheck(void) {
	/* fileXioDevctl returns 0 if the DEV9 device is the CXD9566 (PCMCIA), and 1 if it's the CXD9611 (Expansion bay). */
	return(fileXioDevctl("dev9x:", 0x4401, NULL, 0, NULL, 0)==0?1:0);
}

/*
	static inline int IsLogoValid(u32 StartLBA);

	Parameters: StartLBA - LBA of the first sector of the disc image.
	Return values:
		<0	- Error.
		0	- Logo is not valid for use with the console.
		1	- Logo is valid for use with the console.
*/
#if 0
static inline int IsLogoValid(u32 StartLBA){
	int fd, result;
	char *LogoBuffer, RegionCode;
	hddAtaTransfer_t ReadArg ALIGNED(64);
	unsigned int i;
	unsigned short int crc16checksum;

	fd=fileXioOpen("rom0:ROMVER", O_RDONLY, 666);
	fileXioLseek(fd, 4, SEEK_SET);
	fileXioRead(fd, &RegionCode, 1);
	fileXioClose(fd);

	InitCRC16Table();

	LogoBuffer=memalign(64, 2048);

	result=0;
	for(i=0,crc16checksum=CRC16_INITIAL_CHECKSUM; (i<12) && (result>=0); i++){
		ReadArg.lba=StartLBA+i*4;
		ReadArg.size=4;
		result=fileXioDevctl("hdd0:", APA_DEVCTL_ATA_READ, &ReadArg, sizeof(hddAtaTransfer_t), LogoBuffer, ReadArg.size*512);
		crc16checksum=CalculateCRC16(LogoBuffer, 2048, crc16checksum);
	}

	if(result>=0){
		crc16checksum=ReflectAndXORCRC16(crc16checksum);

		switch(RegionCode){
			/* NTSC regions. */
			case 'J':
			case 'A':
			case 'C':
			case 'H':
			case 'M':
				result=(crc16checksum==0xD2BC)?1:0;
				break;
			/* PAL regions. */
			case 'E':
			case 'R':
			case 'O':
				result=(crc16checksum==0x4702)?1:0;
				break;
			default:
				result=0;
		}

		LOG("Console region:\t%c\nLogo checksum:\t0x%04x\nResult:\t%d\n", RegionCode, crc16checksum, result);
	}
	else{
		LOG("Error reading logo: %d\n", result);
	}

	free(LogoBuffer);

	return result;
}
#endif

#ifdef VMC
#define IRX_NUM 11
#else
#define IRX_NUM 10
#endif

static inline void sendIrxKernelRAM(int size_cdvdman_irx, void *cdvdman_irx) { // Send IOP modules that core must use to Kernel RAM
	void *irxtab = (void *)0x80033010;
	void *irxptr = (void *)0x80033100;
	irxptr_t irxptr_tab[IRX_NUM];
	void *irxsrc[IRX_NUM];
	int i;
	u32 irxsize, curIrxSize;

	irxptr_tab[0].irxsize = size_imgdrv_irx;
	irxptr_tab[1].irxsize = size_eesync_irx;
	irxptr_tab[2].irxsize = size_cdvdman_irx;
	irxptr_tab[3].irxsize = size_cdvdfsv_irx;
	irxptr_tab[4].irxsize = size_cddev_irx;
	irxptr_tab[5].irxsize = 0;	//usbd
	irxptr_tab[6].irxsize = 0;	//smsmap
	irxptr_tab[7].irxsize = 0;	//udptty
	irxptr_tab[8].irxsize = 0;	//ioptrap
	irxptr_tab[9].irxsize = 0;	//smstcpip
#ifdef VMC
	irxptr_tab[10].irxsize = 0;	//mcemu
#endif

	irxsrc[0] = (void *)imgdrv_irx;
	irxsrc[1] = (void *)eesync_irx;
	irxsrc[2] = (void *)cdvdman_irx;
	irxsrc[3] = (void *)cdvdfsv_irx;
	irxsrc[4] = (void *)cddev_irx;
	irxsrc[5] = NULL;
	irxsrc[6] = NULL;
	irxsrc[7] = NULL;
	irxsrc[8] = NULL;
	irxsrc[9] = NULL;
#ifdef VMC
	irxsrc[10] = NULL;
#endif

	irxsize = 0;

	DIntr();
	ee_kmode_enter();

	*(void**)0x80033000 = irxtab;

	for (i = 0; i < IRX_NUM; i++) {
		curIrxSize = irxptr_tab[i].irxsize;
		irxptr_tab[i].irxaddr = irxptr;

		if (curIrxSize > 0) {
	/*		ee_kmode_exit();
			EIntr();
			LOG("irx addr start: %p end: %p\n", irxptr_tab[i].irxaddr, irxptr_tab[i].irxaddr+curIrxSize);
			DIntr();
			ee_kmode_enter(); */

			if(curIrxSize>0) memcpy((void *)irxptr_tab[i].irxaddr, (void *)irxsrc[i], curIrxSize);

			irxptr += curIrxSize;
			irxsize += curIrxSize;
		}
	}

	memcpy(irxtab, irxptr_tab, sizeof(irxptr_tab));

	ee_kmode_exit();
	EIntr();
}

void sysLaunchLoaderElf(unsigned long int StartLBA, char *filename, char *mode_str, int size_cdvdman_irx, void *cdvdman_irx, int compatflags, int alt_ee_core) {
	u8 *boot_elf = NULL;
	elf_header_t *eh;
	elf_pheader_t *eph;
	void *pdata;
	int i, NumArgs;
	char *argv[6];
	char config_str[128];
	char FullELFPath[22];	/* cdrom0:\\SXXX_YYY.ZZ;1" */

	if (gExitPath[0] == '\0')
		strncpy(gExitPath, "Browser", 32);

	sendIrxKernelRAM(size_cdvdman_irx, cdvdman_irx);

	// NB: LOADER.ELF is embedded
	if (alt_ee_core)
		boot_elf = (u8 *)&alt_eecore_elf;
	else
		boot_elf = (u8 *)&eecore_elf;
	eh = (elf_header_t *)boot_elf;
	if (_lw((u32)&eh->ident) != ELF_MAGIC)
		while (1);

	eph = (elf_pheader_t *)(boot_elf + eh->phoff);

	// Scan through the ELF's program headers and copy them into RAM, then
	// zero out any non-loaded regions.
	for (i = 0; i < eh->phnum; i++) {
		if (eph[i].type != ELF_PT_LOAD)
		continue;

		pdata = (void *)(boot_elf + eph[i].offset);
		memcpy(eph[i].vaddr, pdata, eph[i].filesz);

		if (eph[i].memsz > eph[i].filesz)
			memset(eph[i].vaddr + eph[i].filesz, 0, eph[i].memsz - eph[i].filesz);
	}

	// Let's go.
	sprintf(config_str, "%s %d %s %d %d %d.%d.%d.%d %d.%d.%d.%d %d.%d.%d.%d", mode_str, gDisableDebug, gExitPath, 0, gHDDSpindown, \
		0, 0, 0, 0, \
		0, 0, 0, 0, \
		0, 0, 0, 0);

	char cmask[10];
	snprintf(cmask, 10, "%d", compatflags);
	argv[0] = config_str;	
	argv[1] = filename;
	argv[2] = cmask;

	filename[11]=0x00; // fix for 8+3 filename.
	sprintf(FullELFPath, "cdrom0:\\%s;1", filename);

	printf("Starting EE core.\n");

/*	if(IsLogoValid(StartLBA)==1){
		argv[3] = "rom0:PS2LOGO";
		argv[4] = FullELFPath;
		argv[5] = NULL;
		NumArgs=5;
	}
	else{ */
		argv[3] = FullELFPath;
		argv[4] = NULL;
		NumArgs=4;
//	}

	fileXioStop();
	SifExitRpc();

	ExecPS2((void *)eh->entry, 0, NumArgs, argv);
}

