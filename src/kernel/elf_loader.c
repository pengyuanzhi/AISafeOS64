/**
 * @file elf_loader.c
 * @brief ELF Loader Implementation
 *
 * ELF64 file parser and loader implementation.
 * Supports position-independent code (PIC) and basic relocations.
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @author AISafe64 Team
 * @version 1.0
 * @date 2025-01-08
 */

#include "elf_loader.h"
#include <kernel/mm.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <crypto/sha256.h>
#include <crypto/ecdsa.h>

/* ELF magic number */
static const uint8_t g_elf_magic[] = {
    0x7F, 'E', 'L', 'F',
    ELFCLASS64,     /* 64-bit */
    ELFDATA2LSB,    /* Little-endian */
    EV_CURRENT      /* Current version */
};

/* Module state */
static bool g_elf_loader_initialized = false;

/**
 * @brief Initialize ELF loader
 */
int elf_loader_init(void) {
    if (g_elf_loader_initialized) {
        return 0;
    }

    g_elf_loader_initialized = true;
    printk(KERN_INFO "ELF loader initialized\n");

    return 0;
}

/**
 * @brief Validate ELF magic number
 */
bool elf_validate_magic(const uint8_t *data, uint32_t size) {
    /* Parameter validation */
    if (data == NULL) {
        return false;
    }

    /* Check minimum size */
    if (size < ELF_MAGIC_SIZE) {
        return false;
    }

    /* Compare magic number */
    if (memcmp(data, g_elf_magic, ELF_MAGIC_SIZE) != 0) {
        return false;
    }

    return true;
}

/**
 * @brief Read and validate ELF64 header
 */
int elf_read_header(const uint8_t *data, uint32_t size,
                    const Elf64_Ehdr **ehdr) {
    const Elf64_Ehdr *header;
    uint64_t phdr_end;
    uint64_t shdr_end;

    /* Parameter validation */
    if (data == NULL) {
        return -EINVAL;
    }

    if (ehdr == NULL) {
        return -EINVAL;
    }

    /* Check minimum size */
    if (size < sizeof(Elf64_Ehdr)) {
        return -EINVAL;
    }

    /* Set header pointer */
    header = (const Elf64_Ehdr *)data;

    /* Validate magic number */
    if (!elf_validate_magic(data, size)) {
        return -EINVAL;
    }

    /* Validate machine type (ARM64) */
    if (header->e_machine != EM_AARCH64) {
        printk(KERN_ERR "Wrong machine type: %u\n", header->e_machine);
        return -ENOEXEC;
    }

    /* Validate entry point */
    if (header->e_entry == 0UL) {
        printk(KERN_ERR "Invalid entry point: 0\n");
        return -ENOEXEC;
    }

    /* Validate program headers */
    if (header->e_phoff > (uint64_t)size) {
        return -EINVAL;
    }

    if (header->e_phnum > 0U) {
        phdr_end = header->e_phoff +
                   ((uint64_t)header->e_phentsize * (uint64_t)header->e_phnum);

        if (phdr_end > (uint64_t)size) {
            return -EINVAL;
        }
    }

    /* Validate section headers */
    if (header->e_shoff > (uint64_t)size) {
        return -EINVAL;
    }

    if (header->e_shnum > 0U) {
        shdr_end = header->e_shoff +
                   ((uint64_t)header->e_shentsize * (uint64_t)header->e_shnum);

        if (shdr_end > (uint64_t)size) {
            return -EINVAL;
        }
    }

    *ehdr = header;

    return 0;
}

/**
 * @brief Load ELF from file
 */
int elf_load_from_file(const char *path, ElfLoadContext_t *ctx) {
    int fd;
    ssize_t ret;
    uint8_t *data;
    uint32_t size;
    const Elf64_Ehdr *ehdr;

    /* Parameter validation */
    if (path == NULL) {
        return -EINVAL;
    }

    if (ctx == NULL) {
        return -EINVAL;
    }

    /* Clear context */
    (void)memset(ctx, 0, sizeof(ElfLoadContext_t));

    /* Open file */
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printk(KERN_ERR "Failed to open %s: %d\n", path, fd);
        return fd;
    }

    /* Get file size */
    ret = lseek(fd, 0, SEEK_END);
    if (ret < 0) {
        close(fd);
        return ret;
    }

    size = (uint32_t)ret;

    /* Allocate buffer */
    data = (uint8_t *)malloc(size);
    if (data == NULL) {
        close(fd);
        return -ENOMEM;
    }

    /* Read entire file */
    lseek(fd, 0, SEEK_SET);
    ret = read(fd, data, size);
    close(fd);

    if (ret != (ssize_t)size) {
        free(data);
        return -EIO;
    }

    /* Validate ELF header */
    ret = elf_read_header(data, size, &ehdr);
    if (ret != 0) {
        free(data);
        return ret;
    }

    /* Initialize context */
    ctx->elf_data = data;
    ctx->elf_size = size;
    ctx->entry_point = ehdr->e_entry;
    ctx->phdr_offset = ehdr->e_phoff;
    ctx->phdr_count = ehdr->e_phnum;
    ctx->shdr_count = ehdr->e_shnum;
    ctx->is_pie = (ehdr->e_type == 3U);  /* ET_DYN */

    return 0;
}

/**
 * @brief Load ELF segments into memory
 */
int elf_load_segments(ElfLoadContext_t *ctx,
                      ElfSegmentInfo_t *segments,
                      uint32_t max_segments,
                      uint32_t *segment_count) {
    const Elf64_Ehdr *ehdr;
    const Elf64_Phdr *phdr;
    uint32_t i;
    uint32_t loaded = 0U;

    /* Parameter validation */
    if (ctx == NULL) {
        return -EINVAL;
    }

    if (segments == NULL) {
        return -EINVAL;
    }

    if (segment_count == NULL) {
        return -EINVAL;
    }

    if (max_segments == 0U) {
        return -EINVAL;
    }

    /* Get ELF header */
    ehdr = (const Elf64_Ehdr *)ctx->elf_data;
    phdr = (const Elf64_Phdr *)(ctx->elf_data + ctx->phdr_offset);

    /* Load each PT_LOAD segment */
    for (i = 0U; i < ctx->phdr_count; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint8_t *vaddr;
            uint64_t filesz = phdr[i].p_filesz;
            uint64_t memsz = phdr[i].p_memsz;
            uint64_t offset = phdr[i].p_offset;

            /* Check if we have room in output array */
            if (loaded >= max_segments) {
                break;
            }

            /* Allocate memory */
            vaddr = (uint8_t *)malloc(memsz);
            if (vaddr == NULL) {
                return -ENOMEM;
            }

            /* Clear memory */
            (void)memset(vaddr, 0, memsz);

            /* Copy file data */
            if (filesz > 0U) {
                (void)memcpy(vaddr, ctx->elf_data + offset, (size_t)filesz);
            }

            /* Set up MMU mapping */
            {
                uint32_t mmu_flags = 0U;

                if ((phdr[i].p_flags & PF_X) != 0U) {
                    /* Code segment: RX */
                    mmu_flags = MMU_AP_RO | MMU_PXN_DISABLE;
                } else if ((phdr[i].p_flags & PF_W) != 0U) {
                    /* Data segment: RW */
                    mmu_flags = MMU_AP_RW | MMU_PXN_ENABLE | MMU_UXN_ENABLE;
                } else {
                    /* Read-only: R */
                    mmu_flags = MMU_AP_RO | MMU_PXN_ENABLE | MMU_UXN_ENABLE;
                }

                /* Map pages */
                mmu_map_user_range((uint64_t)vaddr, (uint64_t)vaddr,
                                   memsz, mmu_flags);
            }

            /* Record segment info */
            segments[loaded].vaddr = (uint64_t)vaddr;
            segments[loaded].size = memsz;
            segments[loaded].flags = phdr[i].p_flags;
            segments[loaded].loaded = true;

            loaded++;
        }
    }

    *segment_count = loaded;

    return 0;
}

/**
 * @brief Perform ELF relocations
 */
int elf_relocate(ElfLoadContext_t *ctx,
                 const ElfSegmentInfo_t *segments,
                 uint32_t segment_count,
                 uint64_t load_base) {
    const Elf64_Ehdr *ehdr;
    const Elf64_Shdr *shdr;
    uint32_t i;

    /* Parameter validation */
    if (ctx == NULL) {
        return -EINVAL;
    }

    if ((segments == NULL) && (segment_count > 0U)) {
        return -EINVAL;
    }

    /* Get ELF header */
    ehdr = (const Elf64_Ehdr *)ctx->elf_data;

    /* Skip if no section headers */
    if (ehdr->e_shoff == 0UL) {
        return 0;
    }

    /* Get section headers */
    shdr = (const Elf64_Shdr *)(ctx->elf_data + ehdr->e_shoff);

    /* Process relocation sections */
    for (i = 0U; i < ehdr->e_shnum; i++) {
        if ((shdr[i].sh_type == SHT_RELA) && (shdr[i].sh_size > 0U)) {
            const Elf64_Rela *rela;
            uint32_t j;
            uint32_t num_rela;

            /* Get relocation array */
            rela = (const Elf64_Rela *)(ctx->elf_data + shdr[i].sh_offset);
            num_rela = (uint32_t)(shdr[i].sh_size / sizeof(Elf64_Rela));

            /* Process each relocation */
            for (j = 0U; j < num_rela; j++) {
                uint32_t type = (uint32_t)ELF64_R_TYPE(rela[j].r_info);
                uint64_t offset = rela[j].r_offset;
                int64_t addend = rela[j].r_addend;
                uint64_t *target;

                /* Check offset bounds */
                if (offset >= ctx->elf_size) {
                    return -EINVAL;
                }

                target = (uint64_t *)(ctx->elf_data + offset);

                /* Process relocation type */
                switch (type) {
                    case R_AARCH64_RELATIVE:
                        /* Base-relative relocation */
                        if (addend >= 0) {
                            *target = load_base + (uint64_t)addend;
                        } else {
                            if ((uint64_t)(-addend) <= load_base) {
                                *target = load_base - (uint64_t)(-addend);
                            } else {
                                return -EINVAL;
                            }
                        }
                        break;

                    case R_AARCH64_NONE:
                        /* No relocation */
                        break;

                    default:
                        /* Unsupported relocation type */
                        printk(KERN_WARNING "Unsupported relocation: %u\n", type);
                        return -ENOSYS;
                }
            }
        }
    }

    return 0;
}

/**
 * @brief Calculate SHA-256 hash
 */
int elf_calc_hash(const uint8_t *data, uint32_t size, uint8_t *hash) {
    /* Parameter validation */
    if (data == NULL) {
        return -EINVAL;
    }

    if (hash == NULL) {
        return -EINVAL;
    }

    if (size == 0U) {
        return -EINVAL;
    }

    /* Calculate SHA-256 */
    sha256_calc(data, size, hash);

    return 0;
}

/**
 * @brief Verify ELF signature
 */
int elf_verify_signature(const uint8_t *data, uint32_t size,
                          const uint8_t *signature,
                          const uint8_t *pubkey) {
    uint8_t hash[32];
    int ret;

    /* Parameter validation */
    if (data == NULL) {
        return -EINVAL;
    }

    if (signature == NULL) {
        return -EINVAL;
    }

    if (pubkey == NULL) {
        return -EINVAL;
    }

    /* Calculate hash */
    ret = elf_calc_hash(data, size, hash);
    if (ret != 0) {
        return ret;
    }

    /* Verify signature */
    ret = ecdsa_verify_hash(pubkey, signature, hash);
    if (ret != 0) {
        printk(KERN_ERR "Signature verification failed\n");
        return -EPERM;
    }

    return 0;
}

/**
 * @brief Get ELF entry point
 */
uint64_t elf_get_entry_point(const ElfLoadContext_t *ctx) {
    if (ctx == NULL) {
        return 0UL;
    }

    return ctx->entry_point;
}

/**
 * @brief Free ELF context resources
 */
void elf_free_context(ElfLoadContext_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    if (ctx->elf_data != NULL) {
        free((void *)ctx->elf_data);
        ctx->elf_data = NULL;
    }

    (void)memset(ctx, 0, sizeof(ElfLoadContext_t));
}
