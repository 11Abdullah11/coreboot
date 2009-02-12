/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2002 Linux Networx
 * (Written by Eric Biederman <ebiederman@lnxi.com> for Linux Networx)
 * Copyright (C) 2007 Ronald G. Minnich <rminnich@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA, 02110-1301 USA
 */

/* elfboot -- boot elf images */

/* This code is modified from the coreboot V2 version as follows:
 * great simplified
 * checksum removed -- lar can do that
 * can run from read-only FLASH
 * no calls to malloc
 */

#include <types.h>
#include <string.h>
#include <console.h>
#include <tables.h>
#include <elf.h>
#include <elf_boot.h>

static int valid_area(struct lb_memory *mem, 
	u64 start, u64 len)
{
	/* Check through all of the memory segments and ensure
	 * the segment that was passed in is completely contained
	 * in RAM.
	 */
	int i;
	u64 end = start + len;
	unsigned long mem_entries = (mem->size - sizeof(*mem))/sizeof(mem->map[0]);

	/* Walk through the table of valid memory ranges and see if I
	 * have a match.
	 */
	for(i = 0; i < mem_entries; i++) {
		u64 mstart, mend;
		u32 mtype;
		mtype = mem->map[i].type;
		mstart = unpack_lb64(mem->map[i].start);
		mend = mstart + unpack_lb64(mem->map[i].size);
		if ((mtype == LB_MEM_RAM) && (start < mend) && (end > mstart)) {
			break;
		}
	}
	if (i == mem_entries) {
		printk(BIOS_ERR, "No matching RAM area found for range:\n");
		printk(BIOS_ERR, "  [0x%016llx, 0x%016llx)\n", start, end);
		printk(BIOS_ERR, "RAM areas\n");
		for(i = 0; i < mem_entries; i++) {
			u64 mstart, mend;
			u32 mtype;
			mtype = mem->map[i].type;
			mstart = unpack_lb64(mem->map[i].start);
			mend = mstart + unpack_lb64(mem->map[i].size);
			printk(BIOS_ERR, "  [0x%016llx, 0x%016llx) %s\n",
				mstart, mend, (mtype == LB_MEM_RAM)?"RAM":"Reserved");
			
		}
		return 0;
	}
	return 1;
}

static int load_elf_segments(struct lb_memory *mem,unsigned char *header, int headers)
{
        Elf_ehdr *ehdr;
        Elf_phdr *phdr;

        ehdr = (Elf_ehdr *)header;
        phdr = (Elf_phdr *)(&header[ehdr->e_phoff]);

	printk(BIOS_DEBUG, "%s: header %p #headers %d\n", __func__, header, headers);
	int i;
	int size;
	for(i = 0; i < headers; i++) {
		/* Ignore data that I don't need to handle */
		if (phdr[i].p_type != PT_LOAD) {
			printk(BIOS_DEBUG, "Dropping non PT_LOAD segment\n");
			continue;
		}
		if (phdr[i].p_memsz == 0) {
			printk(BIOS_DEBUG, "Dropping empty segment\n");
			continue;
		}
		printk(BIOS_DEBUG, "New segment addr 0x%x size 0x%x offset 0x%x filesize 0x%x\n",
			phdr[i].p_paddr, phdr[i].p_memsz, phdr[i].p_offset, phdr[i].p_filesz);
		/* Clean up the values */
		size = phdr[i].p_filesz;
		if (size > phdr[i].p_memsz)  {
			size = phdr[i].p_memsz;
			printk(BIOS_DEBUG, "(cleaned up) New segment addr 0x%x size 0x%x offset 0x%x\n",
			    phdr[i].p_paddr, size, phdr[i].p_offset);
		}

		/* Verify the memory addresses in the segment are valid */
		if (!valid_area(mem, phdr[i].p_paddr, size)) 
			goto out;
		/* ok, copy it out */
		printk(BIOS_INFO, "Copy to %p from %p for %d bytes\n", (unsigned char *)phdr[i].p_paddr, &header[phdr[i].p_offset], size);
		memcpy((unsigned char *)phdr[i].p_paddr, &header[phdr[i].p_offset], size);
		if (size < phdr[i].p_memsz) {
		        printk(BIOS_INFO, "Set %p to 0 for %d bytes\n", (unsigned char *)phdr[i].p_paddr, phdr[i].p_memsz-size);
		        memset((unsigned char *)phdr[i].p_paddr+size, 0, phdr[i].p_memsz-size);
		}
	}
	return 1;
 out:
	return 0;
}



int elfload(struct lb_memory *mem, unsigned char *header, unsigned long header_size)
{
	Elf_ehdr *ehdr;
	void *entry;

	ehdr = (Elf_ehdr *)header;
	entry = (void *)(ehdr->e_entry);

	/* Load the segments */
	if (!load_elf_segments(mem, header, header_size))
		goto out;

	printk(BIOS_SPEW, "Loaded segments\n");
	
	/* Reset to booting from this image as late as possible */
	/* what the hell is boot_successful? */
	//boot_successful();

	printk(BIOS_DEBUG, "Jumping to boot code at %p\n", entry);
	post_code(POST_ELFBOOT_JUMPING_TO_BOOTCODE);

	/* Jump to kernel */
	/* most of the time, jmp_to_elf_entry is just a call. But this hook gives us 
	  * a handy way to get architecture-dependent operations done, if needed 
	  * jmp_to_elf_entry is in arch/<architecture>/archelfboot.c
	  */
	jmp_to_elf_entry(entry);
	return 1;

 out:
	return 0;
}

int elfboot_mem(struct lb_memory *mem, void *where, int size)
{
	Elf_ehdr *ehdr;
	unsigned char *header = where;
	int header_offset;
	int i, result;

	result = 0;
	printk(BIOS_INFO, "ELF loader started.\n");
	post_code(POST_ELFBOOT_LOADER_STARTED);

	/* Scan for an elf header */
	header_offset = -1;
	for(i = 0; i < ELF_HEAD_SIZE - (sizeof(Elf_ehdr) + sizeof(Elf_phdr)); i+=16) {
		ehdr = (Elf_ehdr *)(&header[i]);
		if (memcmp(ehdr->e_ident, ELFMAG, 4) != 0) {
			printk(BIOS_SPEW, "No header at %d\n", i);
			continue;
		}
		printk(BIOS_DEBUG, "Found ELF candidate at offset %d\n", i);
			header_offset = i;
			break;
		/* Sanity check the elf header */
		if ((ehdr->e_type == ET_EXEC) &&
			elf_check_arch(ehdr) &&
			(ehdr->e_ident[EI_VERSION] == EV_CURRENT) &&
			(ehdr->e_version == EV_CURRENT) &&
			(ehdr->e_ehsize == sizeof(Elf_ehdr)) &&
			(ehdr->e_phentsize = sizeof(Elf_phdr)) &&
			(ehdr->e_phoff < (ELF_HEAD_SIZE - i)) &&
			((ehdr->e_phoff + (ehdr->e_phentsize * ehdr->e_phnum)) <= 
				(ELF_HEAD_SIZE - i))) {
			header_offset = i;
			break;
		}
		ehdr = 0;
	}
	printk(BIOS_SPEW, "header_offset is %d\n", header_offset);
	if (header_offset == -1) {
		goto out;
	}

	printk(BIOS_SPEW, "Try to load at offset 0x%x %d phdr\n", header_offset, ehdr->e_phnum);
	result = elfload(mem, header,  ehdr->e_phnum);
 out:
	if (!result) {

		printk(BIOS_ERR, "Cannot load ELF image\n");

		post_code(POST_ELFBOOT_LOADER_IMAGE_FAILED);
	}
	return 0;
}	
