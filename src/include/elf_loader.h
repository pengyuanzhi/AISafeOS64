/**
 * @file elf_loader.h
 * @brief ELF Loader - ELF64 file parser and loader
 *
 * This file defines the interface for loading ELF64 files.
 * Supports position-independent code (PIC) and basic relocations.
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @author AISafe64 Team
 * @version 1.0
 * @date 2025-01-08
 */

#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ELF types */
#define ELF_MAGIC_SIZE 16U

/* ELF class */
#define ELFCLASS_NONE 0
#define ELFCLASS32 1
#define ELFCLASS64 2

/* ELF data encoding (endianness) */
#define ELFDATANONE 0
#define ELFDATA2LSB 1 /* Little-endian */
#define ELFDATA2MSB 2 /* Big-endian */

/* ELF version */
#define EV_CURRENT 1

/* ELF machine types */
#define EM_NONE 0
#define EM_AARCH64 183 /* ARM AArch64 */

/* ELF segment types */
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_SHLIB 5
#define PT_PHDR 6
#define PT_GNU_STACK 0x6474E551
#define PT_GNU_RELRO 0x6474E552

/* ELF segment permissions */
#define PF_X 0x1 /* Execute */
#define PF_W 0x2 /* Write */
#define PF_R 0x4 /* Read */

/* ARM64 AArch64 relocation types */
#define R_AARCH64_NONE 0            /* No relocation */
#define R_AARCH64_ABS64 257         /* Direct 64-bit absolute */
#define R_AARCH64_GLOB_DAT 1025     /* Global data */
#define R_AARCH64_JUMP_SLOT 1026    /* Jump slot */
#define R_AARCH64_RELATIVE 1027     /* Base-relative */
#define R_AARCH64_TLS_DTPREL64 1028 /* TLS dynamic */
#define R_AARCH64_TLS_DTPMOD64 1029 /* TLS module */
#define R_AARCH64_TLS_TPREL64 1030  /* TLS thread */
#define R_AARCH64_TLSDESC 1031      /* TLS descriptor */

/**
 * @brief ELF64 header structure
 *
 * Matches the ELF64 header format defined in the ELF specification.
 * Packed to ensure exact binary layout.
 */
typedef struct __attribute__((packed))
{
    uint8_t e_ident[16];  /* ELF identification */
    uint16_t e_type;      /* Object file type */
    uint16_t e_machine;   /* Machine type */
    uint32_t e_version;   /* Object file version */
    uint64_t e_entry;     /* Entry point virtual address */
    uint64_t e_phoff;     /* Program header table file offset */
    uint64_t e_shoff;     /* Section header table file offset */
    uint32_t e_flags;     /* Processor-specific flags */
    uint16_t e_ehsize;    /* ELF header size */
    uint16_t e_phentsize; /* Program header table entry size */
    uint16_t e_phnum;     /* Program header table entry count */
    uint16_t e_shentsize; /* Section header table entry size */
    uint16_t e_shnum;     /* Section header table entry count */
    uint16_t e_shstrndx;  /* Section header string table index */
} Elf64_Ehdr;

/**
 * @brief ELF64 program header structure
 *
 * Describes a segment or other information the system needs
 * to prepare the program for execution.
 */
typedef struct __attribute__((packed))
{
    uint32_t p_type;   /* Segment type */
    uint32_t p_flags;  /* Segment flags */
    uint64_t p_offset; /* Segment file offset */
    uint64_t p_vaddr;  /* Segment virtual address */
    uint64_t p_paddr;  /* Segment physical address */
    uint64_t p_filesz; /* Segment size in file */
    uint64_t p_memsz;  /* Segment size in memory */
    uint64_t p_align;  /* Segment alignment */
} Elf64_Phdr;

/**
 * @brief ELF64 section header structure
 *
 * Describes a single section in the ELF file.
 */
typedef struct __attribute__((packed))
{
    uint32_t sh_name;      /* Section name */
    uint32_t sh_type;      /* Section type */
    uint64_t sh_flags;     /* Section flags */
    uint64_t sh_addr;      /* Section virtual address */
    uint64_t sh_offset;    /* Section file offset */
    uint64_t sh_size;      /* Section size in bytes */
    uint32_t sh_link;      /* Section link */
    uint32_t sh_info;      /* Section information */
    uint64_t sh_addralign; /* Section alignment */
    uint64_t sh_entsize;   /* Section entry size */
} Elf64_Shdr;

/**
 * @brief ELF64 symbol structure
 *
 * Describes a single symbol in the symbol table.
 */
typedef struct __attribute__((packed))
{
    uint32_t st_name;  /* Symbol name */
    uint8_t st_info;   /* Symbol type and binding */
    uint8_t st_other;  /* Symbol visibility */
    uint16_t st_shndx; /* Section index */
    uint64_t st_value; /* Symbol value */
    uint64_t st_size;  /* Symbol size */
} Elf64_Sym;

/**
 * @brief ELF64 relocation entry with addend
 *
 * Describes a single relocation operation.
 */
typedef struct __attribute__((packed))
{
    uint64_t r_offset; /* Address where to apply relocation */
    uint64_t r_info;   /* Relocation type and symbol index */
    int64_t r_addend;  /* Addend */
} Elf64_Rela;

/**
 * @brief ELF loading context
 *
 * Contains all state information during ELF loading.
 */
typedef struct
{
    const uint8_t *elf_data; /* Pointer to ELF file data */
    uint32_t elf_size;       /* ELF file size */
    uint64_t entry_point;    /* Entry point address */
    uint64_t phdr_offset;    /* Program header offset */
    uint16_t phdr_count;     /* Program header count */
    uint16_t shdr_count;     /* Section header count */
    bool is_pie;             /* Position-independent executable */
} ElfLoadContext_t;

/**
 * @brief ELF segment information
 *
 * Describes a loaded segment in memory.
 */
typedef struct
{
    uint64_t vaddr; /* Virtual address */
    uint64_t paddr; /* Physical address */
    uint64_t size;  /* Size in memory */
    uint64_t flags; /* MMU flags */
    bool loaded;    /* Whether segment is loaded */
} ElfSegmentInfo_t;

/**
 * @brief Validate ELF magic number
 *
 * Checks if the given data starts with a valid ELF magic number.
 *
 * @param data Pointer to ELF file data
 * @param size Size of ELF file data
 *
 * @return true if valid ELF magic, false otherwise
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameter
 *   - Rule 21.3: Validate size parameter
 *   - Rule 18.1: Bounded memory comparison
 *
 * @warning data must be at least ELF_MAGIC_SIZE bytes
 * @warning size must be >= ELF_MAGIC_SIZE
 */
bool elf_validate_magic(const uint8_t *data, uint32_t size);

/**
 * @brief Read and validate ELF64 header
 *
 * Reads the ELF header from the given data and validates
 * all fields.
 *
 * @param data Pointer to ELF file data
 * @param size Size of ELF file data
 * @param ehdr Output: ELF header pointer
 *
 * @return 0 on success, negative error code on failure
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameters
 *   - Rule 21.3: Validate size parameter
 *   - Rule 18.4: Pointer arithmetic safety
 *
 * @note Validates:
 *   - Magic number
 *   - Class (64-bit)
 *   - Endianness
 *   - Machine type (ARM64)
 *   - Entry point address
 *   - Program header offset and count
 *   - Section header offset and count
 *
 * @warning ehdr output points into data buffer
 * @warning Must check return value
 */
int elf_read_header(const uint8_t *data, uint32_t size, const Elf64_Ehdr **ehdr);

/**
 * @brief Load ELF segments into memory
 *
 * Loads all PT_LOAD segments from the ELF file into memory,
 * allocates memory for each segment, and sets up MMU mappings.
 *
 * @param ctx ELF loading context
 * @param segments Output: array of loaded segment information
 * @param max_segments Maximum number of segments in output array
 * @param segment_count Output: actual number of segments loaded
 *
 * @return 0 on success, negative error code on failure
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameters
 *   - Rule 21.3: Validate array bounds
 *   - Rule 20.5: No unsigned integer overflow
 *   - Rule 18.1: Bounded memory operations
 *
 * @note Process:
 *   1. Iterate through program headers
 *   2. For each PT_LOAD segment:
 *      a. Allocate memory
 *      b. Copy segment data
 *      c. Zero BSS portion
 *      d. Set up MMU mapping with correct permissions
 *
 * @warning Memory allocated by this function must be freed
 * @warning MMU mappings are set up for user space
 */
int elf_load_segments(ElfLoadContext_t *ctx, ElfSegmentInfo_t *segments, uint32_t max_segments,
                      uint32_t *segment_count);

/**
 * @brief Perform ELF relocations
 *
 * Processes all relocation entries in the ELF file and applies
 * them to the loaded image.
 *
 * @param ctx ELF loading context
 * @param segments Loaded segment information
 * @param segment_count Number of loaded segments
 * @param load_base Load base address
 *
 * @return 0 on success, negative error code on failure
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameters
 *   - Rule 21.3: Validate array bounds
 *   - Rule 20.5: No unsigned integer overflow
 *   - Rule 18.4: Safe pointer arithmetic
 *
 * @note Supported relocation types:
 *   - R_AARCH64_RELATIVE: Base-relative relocation
 *   - R_AARCH64_ABS64: 64-bit absolute (requires symbol table)
 *   - R_AARCH64_GLOB_DAT: Global data (requires symbol table)
 *   - R_AARCH64_JUMP_SLOT: Jump slot (requires symbol table)
 *
 * @warning Only position-independent code is fully supported
 * @warning Symbol-based relocations require full symbol table
 */
int elf_relocate(ElfLoadContext_t *ctx, const ElfSegmentInfo_t *segments, uint32_t segment_count,
                 uint64_t load_base);

/**
 * @brief Calculate SHA-256 hash of ELF file
 *
 * Computes the SHA-256 hash of the entire ELF file data.
 *
 * @param data Pointer to ELF file data
 * @param size Size of ELF file data
 * @param hash Output: 256-bit hash (32 bytes)
 *
 * @return 0 on success, negative error code on failure
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameters
 *   - Rule 21.3: Validate size parameter
 *   - Rule 20.5: No unsigned integer overflow
 *   - Rule 18.1: Bounded memory operations
 *
 * @warning hash buffer must be at least 32 bytes
 * @warning Uses hardware acceleration if available
 */
int elf_calc_hash(const uint8_t *data, uint32_t size, uint8_t *hash);

/**
 * @brief Verify ELF file signature
 *
 * Verifies the ECDSA-P256 signature of the ELF file.
 *
 * @param data Pointer to ELF file data
 * @param size Size of ELF file data
 * @param signature ECDSA signature (64 bytes)
 * @param pubkey ECDSA public key (64 bytes)
 *
 * @return 0 on success (signature valid), negative error code on failure
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameters
 *   - Rule 21.3: Validate size parameter
 *   - Rule 18.1: Bounded memory operations
 *
 * @note Process:
 *   1. Calculate SHA-256 hash of ELF data
 *   2. Verify ECDSA-P256 signature against hash
 *   3. Return result
 *
 * @warning signature must be exactly 64 bytes
 * @warning pubkey must be exactly 64 bytes
 * @warning Signature verification failure must reject the file
 */
int elf_verify_signature(const uint8_t *data, uint32_t size, const uint8_t *signature,
                         const uint8_t *pubkey);

/**
 * @brief Get ELF entry point
 *
 * Returns the entry point address from the ELF header.
 *
 * @param ctx ELF loading context
 *
 * @return Entry point address, or 0 if invalid
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameter
 *
 * @warning Entry point must be non-zero for valid applications
 */
uint64_t elf_get_entry_point(const ElfLoadContext_t *ctx);

/* ELF identification indices */
#define EI_CLASS 4      /* File class */
#define EI_DATA 5       /* Data encoding */
#define EI_VERSION 6    /* File version */
#define EI_OSABI 7      /* OS/ABI identification */
#define EI_ABIVERSION 8 /* ABI version */

/* Helper macros for ELF64 relocation info */
#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((uint32_t)(i))

#endif /* ELF_LOADER_H */
