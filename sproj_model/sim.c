#define _XOPEN_SOURCE 600
#define __USE_MISC
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#define __USE_MISC
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include "sim.h"
#include "m68k.h"
//#include "osd.h"
typedef unsigned int uint;


/* Read/write macros */
#define READ_BYTE(BASE, ADDR) (BASE)[ADDR]
#define READ_WORD(BASE, ADDR) (((BASE)[ADDR]<<8) |	\
			       (BASE)[(ADDR)+1])
#define READ_LONG(BASE, ADDR) (((BASE)[ADDR]<<24) |	\
			       ((BASE)[(ADDR)+1]<<16) |	\
			       ((BASE)[(ADDR)+2]<<8) |	\
			       (BASE)[(ADDR)+3])

#define WRITE_BYTE(BASE, ADDR, VAL) (BASE)[ADDR] = (VAL)&0xff
#define WRITE_WORD(BASE, ADDR, VAL) (BASE)[ADDR] = ((VAL)>>8) & 0xff;	\
  (BASE)[(ADDR)+1] = (VAL)&0xff
#define WRITE_LONG(BASE, ADDR, VAL) (BASE)[ADDR] = ((VAL)>>24) & 0xff;	\
  (BASE)[(ADDR)+1] = ((VAL)>>16)&0xff;					\
  (BASE)[(ADDR)+2] = ((VAL)>>8)&0xff;					\
  (BASE)[(ADDR)+3] = (VAL)&0xff


/* Data */
unsigned int g_quit = 0;                        /* 1 if we want to quit */


/* Exit with an error message.  Use printf syntax. */
void exit_error(char* fmt, ...)
{
	static int guard_val = 0;
	char buff[100];
	unsigned int pc;
	va_list args;

	if(guard_val)
		return;
	else
		guard_val = 1;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	pc = m68k_get_reg(NULL, M68K_REG_PPC);
	m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
	fprintf(stderr, "At %04x: %s\n", pc, buff);

	exit(EXIT_FAILURE);
}

/******************************************************************************/
/******************************************************************************/
struct sproj_sram {
  unsigned char *base;
};
uint sproj_sram_init(struct sproj_sram *ram);
uint sproj_sram_reset(struct sproj_sram *ram);
uint sproj_sram_read(struct sproj_sram *ram, uint size, uint offset);
uint sproj_sram_write(struct sproj_sram *ram, uint size, uint offset, uint value);

/* Implementations */
uint sproj_sram_init(struct sproj_sram *ram) {
  ram->base = malloc(64*1024);
  return 0;
}

uint sproj_sram_reset(struct sproj_sram *ram) {
  return 0;
}

uint sproj_sram_read(struct sproj_sram *ram, uint size, uint offset) {
  switch(size) {
  case 1: return READ_BYTE(ram->base, offset);
  case 2: return READ_WORD(ram->base, offset);
  case 4: return READ_LONG(ram->base, offset);
  default: exit(2);
  }
}

uint sproj_sram_write(struct sproj_sram *ram, uint size, uint offset, uint value) {
  //printf("calling sram_write with offset %x and size %d\n", offset, size);
  switch(size) {
  case 1: WRITE_BYTE(ram->base, offset, value); return 0;
  case 2: WRITE_WORD(ram->base, offset, value); return 0;
  case 4: WRITE_LONG(ram->base, offset, value); return 0;
  default: exit(3);
  }
}


/******************************************************************************/
/******************************************************************************/
struct sproj_eprom {
  unsigned char *base;
};
uint sproj_eprom_init(struct sproj_eprom *eprom);
uint sproj_eprom_fload(struct sproj_eprom *eprom, const char* fname);
uint sproj_eprom_read(struct sproj_eprom *eprom, uint size, uint offset); // only reads, can read byte or word

/* Implementations */
uint sproj_eprom_init(struct sproj_eprom *eprom) {
  eprom->base = malloc(64*1024);
  return 0;
}
uint sproj_eprom_fload(struct sproj_eprom *eprom, const char* fname) {
  FILE* fhandle;
  if((fhandle = fopen(fname, "rb")) == NULL)
    exit_error("Unable to open %s", fname);

  if(fread(eprom->base, 1, 64*1024, fhandle) <= 0)
    exit_error("Error reading %s", fname);

  return 0;
}
uint sproj_eprom_read(struct sproj_eprom *eprom, uint size, uint offset) {
  switch(size) {
  case 1: return READ_BYTE(eprom->base, offset);
  case 2: {
    uint val = READ_WORD(eprom->base, offset);
    //printf("sproj_eprom_read: %x @ %x\n", val, offset);
    //printf("sproj_eprom_read: %x %x %x %x\n", eprom->base[offset], eprom->base[offset+1], eprom->base[offset+2], eprom->base[offset+3]);
    return val;
  }
  case 4: return READ_LONG(eprom->base, offset);
  default: exit(4);
  }
}

/******************************************************************************/
/******************************************************************************/
struct acia_6850 {
  uint char_read;
  char buf[4];
  int  ptsfd;
};
uint acia_6850_init(struct acia_6850 *acia);
uint acia_6850_reset(struct acia_6850 *acia);
uint acia_6850_read(struct acia_6850 *acia, uint size, uint offset); // only reads 1 byte
uint acia_6850_write(struct acia_6850 *acia, uint size, uint offset, uint value); // only writes 1 byte

/* Implementations */
uint acia_6850_init(struct acia_6850 *acia) {
  // set up psuedoterminal here
  printf("Creating a pseudo-terminal pair\n");
  int ptmfd = posix_openpt(O_RDWR);
  if(ptmfd<0) {printf("Failed to allocate a psuedo-terminal"); return 1;}

  int rc = grantpt(ptmfd);
  if(rc!=0) {printf("Failed to grantpt\n"); return 1;}
  rc = unlockpt(ptmfd);
  if(rc!=0) {printf("Failed to unlockpt\n"); return 1;}

  printf("The psuedo-terminal slave side is named : %s\n", ptsname(ptmfd));
  printf("The psuedo-terminal master fd is %d\n", ptmfd);

  int ptsfd = open(ptsname(ptmfd), O_RDWR | O_NONBLOCK);

  if(fork()) {
    // parent; processing continues below after the else
    close(ptmfd);
  } else {
    // child; exec xterm so execution never continues after this block
    char sopt[30];
    sprintf(sopt, "-S%s/%d", ptsname(ptmfd), ptmfd);
    execl("/usr/bin/xterm", "xterm", sopt, NULL);
    perror("/usr/bin/xterm");
    return 255; // just in case the exec fails
  }

  struct termios raw_term_settings;
  cfmakeraw(&raw_term_settings);
  tcsetattr(ptsfd, TCSANOW, &raw_term_settings);

  int id_to_read = 8;
  char buf[4];
  while(id_to_read) {
    if(0<read(ptsfd, buf, 1)) {id_to_read--;}
  }

  acia->char_read = 0;
  acia->ptsfd = ptsfd;

  return 0;
}
uint acia_6850_reset(struct acia_6850 *acia) {
  return 0;
}
uint acia_6850_read(struct acia_6850 *acia, uint size, uint offset) {
  // Offset 1 is status. bit 0 set means input char waiting? bit 1 set means ready to accept output char
  // Offset 3 is data. Read to get input data.
  //printf("Read 6850 at offset %d\n", offset);
  //return 2;

  // Status register read
  if(offset==1) {
    if(!acia->char_read) { // if no input char is already available, check to see if there is one
      int rc = read(acia->ptsfd, acia->buf, 1);
      if(0<rc) { acia->char_read = 1; } // read a char so set the char_read flag
    }
    
    uint ret = 2; // The return status says it is always OK to write, and OK to read if there is a char available
    if(acia->char_read) {ret |= 1;}
    return ret;

  } else if(offset==3) { // Data register read, just return the last char read
    acia->char_read = 0; // clear the flag since we read data
    return acia->buf[0];

  } else { // Unknown register read
    printf("acia_6850: read at offset %d\n", offset);
    return 0;
  }
}
uint acia_6850_write(struct acia_6850 *acia, uint size, uint offset, uint value) {
  // Offset 1 is control. Write to set up.
  // Offset 3 is data. Write to send output data.
  //printf("Wrote 6850 at offset %d with value %x", offset, value);
  printf("%c", value);
  if(offset==1) {
    // can ignore for now, might need to do something if I implement interrupts
  } else if(offset==3) {
    write(acia->ptsfd, &value, 1);
  } else {
    printf("acia_6850: write at offset %d\n", offset);
  }
  return 0;
}

/******************************************************************************/
/******************************************************************************/
struct ptm_6840 {
  uint status;
};
uint ptm_6840_init(struct ptm_6840 *ptm);
uint ptm_6840_reset(struct ptm_6840 *ptm);
uint ptm_6840_read(struct ptm_6840 *ptm, uint size, uint offset);
uint ptm_6840_write(struct ptm_6840 *ptm, uint size, uint offset, uint value);

/* Implementations */
uint ptm_6840_init(struct ptm_6840 *ptm) {
  return 0;
}
uint ptm_6840_reset(struct ptm_6840 *ptm) {
  return 0;
}
uint ptm_6840_read(struct ptm_6840 *ptm, uint size, uint offset) {
  return 0;
}
uint ptm_6840_write(struct ptm_6840 *ptm, uint size, uint offset, uint value) {
  return 0;
}

/******************************************************************************/
/******************************************************************************/
struct sproj_sys {
  struct sproj_sram *ram;
  struct sproj_eprom *rom;
  struct acia_6850 *acia0;
  struct acia_6850 *acia1;
  struct ptm_6840 *ptm;
} sys;
uint sproj_sys_init(struct sproj_sys *sys);
uint sproj_sys_reset(struct sproj_sys *sys);
uint sproj_sys_read(struct sproj_sys *sys, uint size, uint addr);
void sproj_sys_write(struct sproj_sys *sys, uint size, uint addr, uint value);

/* Implementations */
uint sproj_sys_init(struct sproj_sys *sys) {
  sys->ram = malloc(sizeof(struct sproj_sram));
  sys->rom = malloc(sizeof(struct sproj_eprom));
  sys->acia0 = malloc(sizeof(struct acia_6850));
  sys->acia1 = malloc(sizeof(struct acia_6850));
  sys->ptm = malloc(sizeof(struct ptm_6840));
  sproj_sram_init(sys->ram);
  sproj_eprom_init(sys->rom);
  acia_6850_init(sys->acia0);
  acia_6850_init(sys->acia1);
  ptm_6840_init(sys->ptm);
  return 0;
}

uint sproj_sys_reset(struct sproj_sys *sys) {
  return 0;
}

uint sproj_sys_read(struct sproj_sys *sys, uint size, uint addr) {
  //printf("Reading addr %x, size %d\n", addr, size);
  if(addr >= 0 && addr < 0x10000) {
    return sproj_eprom_read(sys->rom, size, addr); // rom
  } else if(addr >= 0x10000 && addr < 0x20000) {
    return sproj_sram_read(sys->ram, size, addr-0x10000); // ram
  } else if(addr >= 0x200000 && addr < 0x210000) {
    return acia_6850_read(sys->acia0, size, addr-0x200000); // acia0
  } else if(addr >= 0x210000 && addr < 0x220000) {
    return acia_6850_read(sys->acia1, size, addr-0x210000); // acia1
  } else if(addr >= 0x220000 && addr < 0x230000) {
    return ptm_6840_read(sys->ptm, size, addr-0x220000); // ptm
  }
  return 0;
}

void sproj_sys_write(struct sproj_sys *sys, uint size, uint addr, uint value) {
  //printf("Writing addr %x, size %d, value %x\n", addr, size, value);
  if(addr >= 0 && addr < 0x10000) {
    printf("WARNING: attempted write to eprom\n");
  } else if(addr >= 0x10000 && addr < 0x20000) {
    sproj_sram_write(sys->ram, size, addr-0x10000, value); // ram
  } else if(addr >= 0x200000 && addr < 0x210000) {
    acia_6850_write(sys->acia0, size, addr-0x200000, value); // acia0
  } else if(addr >= 0x210000 && addr < 0x220000) {
    acia_6850_write(sys->acia1, size, addr-0x210000, value); // acia1
  } else if(addr >= 0x220000 && addr < 0x230000) {
    ptm_6840_write(sys->ptm, size, addr-0x220000, value); // ptm
  }
}

// For the 68k shouldn't read_memory_32 always be an error?
// set up mode to dump machine state after every instruction
// Can I attach gdb to an emulator like this???

/******************************************************************************/
/******************************************************************************/
unsigned int m68k_read_memory_8(unsigned int address) {
  return sproj_sys_read(&sys, 1, address);
}
unsigned int m68k_read_memory_16(unsigned int address) {
  return sproj_sys_read(&sys, 2, address);
}
unsigned int m68k_read_memory_32(unsigned int address) {
  return sproj_sys_read(&sys, 4, address);
}
void m68k_write_memory_8(unsigned int address, unsigned int value) {
  sproj_sys_write(&sys, 1, address, value);
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  sproj_sys_write(&sys, 2, address, value);
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  sproj_sys_write(&sys, 4, address, value);
}

unsigned int m68k_read_disassembler_16(unsigned int address) {
  uint foo = sproj_sys_read(&sys, 2, address);
  //printf("m68k_read_disassembler: %x @ %x\n", foo, address);
  return foo;
}
unsigned int m68k_read_disassembler_32(unsigned int address) {
  return sproj_sys_read(&sys, 4, address);
}


/* Disassembler */
void make_hex(char* buff, unsigned int pc, unsigned int length)
{
	char* ptr = buff;

	for(;length>0;length -= 2)
	{
		sprintf(ptr, "%04x", m68k_read_disassembler_16(pc));
		pc += 2;
		ptr += 4;
		if(length > 2)
			*ptr++ = ' ';
	}
}

void disassemble_program()
{
	unsigned int pc;
	unsigned int instr_size;
	char buff[100];
	char buff2[100];

	pc = m68k_read_disassembler_32(4);
	//pc = 0xc0;

	while(pc <= 0x9c6)
	{
		instr_size = m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
		make_hex(buff2, pc, instr_size);
		printf("%03x: %-20s: %s\n", pc, buff2, buff);
		pc += instr_size;
	}
	fflush(stdout);
}

void cpu_instr_callback()
{
  printf("cpu_instr_callback\n");
/* The following code would print out instructions as they are executed */
	static char buff[100];
	static char buff2[100];
	static unsigned int pc;
	static unsigned int instr_size;

	pc = m68k_get_reg(NULL, M68K_REG_PC);
	instr_size = m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
	make_hex(buff2, pc, instr_size);
	printf("E %03x: %-20s: %s\n", pc, buff2, buff);
	fflush(stdout);
}



/* The main loop */
int main(int argc, const char* argv[])
{
  if(argc != 2)
    {
      printf("Usage: sim <program file>\n");
      exit(-1);
    }

  sproj_sys_init(&sys);
  sproj_eprom_fload(sys.rom, argv[1]);

  //disassemble_program();

  m68k_init();
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);
  m68k_pulse_reset();

  g_quit = 0;
  while(!g_quit)
    {
      // Our loop requires some interleaving to allow us to update the
      // input, output, and nmi devices.

      //get_user_input();

      // Values to execute determine the interleave rate.
      // Smaller values allow for more accurate interleaving with multiple
      // devices/CPUs but is more processor intensive.
      // 100000 is usually a good value to start at, then work from there.

      // Note that I am not emulating the correct clock speed!
      m68k_execute(100000);
      }

  return 0;
}

