#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <limits.h>

#include "../include/libbootimg.h"

#define DEFAULT_PAGE_SIZE 2048

static inline unsigned align_size(unsigned size, unsigned page_size)
{
    return ((size + page_size - 1)/page_size)*page_size;
}

static int translate_errnum(int errnum)
{
    switch (errnum)
    {
        case EIO:     return LIBBOOTIMG_ERROR_IO;
        case EACCES:  return LIBBOOTIMG_ERROR_ACCESS;
        case ENOENT:  return LIBBOOTIMG_ERROR_NOT_FOUND;

        default:      return LIBBOOTIMG_ERROR_OTHER;
    }
}

static int translate_fread_error(FILE *f)
{
    if(ferror(f))
        return LIBBOOTIMG_ERROR_IO;
    else if(feof(f))
        return LIBBOOTIMG_ERROR_IMG_EOF;
    else
        return LIBBOOTIMG_ERROR_OTHER;
}

// 32bit FNV-1a hash algorithm
// http://isthe.com/chongo/tech/comp/fnv/#FNV-1a
static uint32_t calc_fnv_hash(void *data, unsigned len)
{
    const uint32_t FNV_prime = 16777619U;
    const uint32_t offset_basis = 2166136261U;

    uint32_t *d = (uint32_t*)data;
    uint32_t i, max;
    uint32_t hash = offset_basis;

    max = len/4;

    // 32 bit data
    for(i = 0; i < max; ++i)
    {
        hash ^= d[i];
        hash *= FNV_prime;
    }

    // last bits
    for(i *= 4; i < len; ++i)
    {
        hash ^= (uint32_t) ((uint8_t*)data)[i];
        hash *= FNV_prime;
    }
    return hash;
}

static void fill_id_hashes(struct bootimg *b)
{
    int i = 0;

    // hash blobs
    for (; i < LIBBOOTIMG_BLOB_CNT ; ++i)
    {
        if (b->blobs[i].size != NULL)
        {
            b->hdr.id[i] = calc_fnv_hash(b->blobs[i].data, *b->blobs[i].size);
        }
    }

    // hash kernel, ramdisk and second _addr and _size together
    b->hdr.id[i++] = calc_fnv_hash(&b->hdr.kernel_size, sizeof(uint32_t)*6);

    // hash tags_addr, page_size, dt_size and unused together
    b->hdr.id[i++] = calc_fnv_hash(&b->hdr.tags_addr, sizeof(uint32_t)*4);

    // cmdline is directly after name, so hash them together
    b->hdr.id[i++] = calc_fnv_hash(b->hdr.name, BOOT_NAME_SIZE + strlen((char*)b->hdr.cmdline));
}



void libbootimg_init_new(struct bootimg *img)
{
    memset(img, 0, sizeof(struct bootimg));
    memcpy(img->hdr.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE);
    img->hdr.page_size = DEFAULT_PAGE_SIZE;
    img->hdr_info = malloc(sizeof(struct boot_img_elf_info));
    memset(img->hdr_info, sizeof(struct boot_img_elf_info), 0);
    img->is_elf = 0;

    img->blobs[LIBBOOTIMG_BLOB_KERNEL].size = &img->hdr.kernel_size;
    img->blobs[LIBBOOTIMG_BLOB_RAMDISK].size = &img->hdr.ramdisk_size;
    img->blobs[LIBBOOTIMG_BLOB_SECOND].size = &img->hdr.second_size;
    img->blobs[LIBBOOTIMG_BLOB_DTB].size = &img->hdr.dt_size;
}

int libbootimg_read_blob(int64_t addr, struct bootimg_blob* blob, FILE* f)
{
    int res = 0;
    if (fseek(f, addr, SEEK_SET) < 0)
    {
        if (errno == EINVAL)
        {
            res = LIBBOOTIMG_ERROR_IMG_EOF;
        }
        else
        {
            res = translate_errnum(errno);
        }
        return res;
    }
    blob->data = malloc(*blob->size);
    if (fread(blob->data, *blob->size, 1, f) != 1)
    {
        res = translate_fread_error(f);
        return res;
    }
    return res;
}

int libbootimg_init_load(struct bootimg *img, const char *path, int load_blob_mask)
{
    int i;
    FILE *f;
    int res = 0;
    int64_t addr;
    struct bootimg_blob *blob;

    libbootimg_init_new(img);

    res = libbootimg_load_headers(&img->hdr, img->hdr_info, &img->is_elf, path);
    if(res < 0)
    {
        libbootimg_destroy(img);
        return res;
    }

    img->start_offset = res;

    f = fopen(path, "r");
    if(!f)
        return translate_errnum(errno);

    addr = img->start_offset + img->hdr.page_size;

    for (i = 0; i < LIBBOOTIMG_BLOB_CNT; ++i)
    {
        blob = &img->blobs[i];

        if (img->is_elf)
        {
            struct boot_img_elf_prog_hdr *cur_prog_hdr = get_elf_proc_hdr_of(img->hdr_info, i);
            if (cur_prog_hdr != NULL)
            {
                res = libbootimg_read_blob(cur_prog_hdr->offset, blob, f);
            }
        }
        else if ((load_blob_mask & (1 << i)) && *blob->size != 0)
        {
            res = libbootimg_read_blob(addr, blob, f);
        }

        if (!img->is_elf)
        {
            addr += align_size(*blob->size, img->hdr.page_size);
        }
    }

    fclose(f);
    return 0;

fail:
    libbootimg_destroy(img);
    fclose(f);
    return res;
}

void libbootimg_destroy(struct bootimg *b)
{
    struct bootimg_blob *blob = b->blobs;
    struct bootimg_blob * const blobs_end = blob + LIBBOOTIMG_BLOB_CNT;
    free(b->hdr_info);
    for(; blob != blobs_end; ++blob)
    {
        free(blob->data);
        blob->data = NULL;
    }
}

int libbootimg_load_header(struct boot_img_hdr *hdr, const char *path)
{
    struct boot_img_elf_info hdr_elf;
    return libbootimg_load_headers(hdr, &hdr_elf, NULL, path);
}

int libbootimg_load_headers(struct boot_img_hdr *hdr,
        struct boot_img_elf_info *hdr_info, uint8_t *is_elf, const char *path)
{
    int res = 0;
    FILE *f;
    size_t i;
    uint32_t cmd_len;
    static const int known_magic_pos[] = {
        0x0,   // default
        0x100, // HTC signed boot images
    };

    f= fopen(path, "r");
    if (!f)
        return translate_errnum(errno);

    res = LIBBOOTIMG_ERROR_INVALID_MAGIC;
    for (i = 0; i < sizeof(known_magic_pos)/sizeof(known_magic_pos[0]); ++i)
    {
        fseek(f, known_magic_pos[i], SEEK_SET);
        if (fread(hdr, sizeof(struct boot_img_hdr), 1, f) == 1)
        {
            if (memcmp(hdr->magic, BOOT_MAGIC, BOOT_MAGIC_SIZE) == 0)
            {
                if (is_elf != NULL)
                {
                    *is_elf = 0;
                }
                res = known_magic_pos[i];
                break;
            }
            else if (hdr_info != NULL && memcmp(hdr->magic + 1, BOOT_MAGIC_ELF,
                    BOOT_MAGIC_ELF_SIZE) == 0)
            {
                fseek(f, 0, SEEK_SET);
                if (fread(&hdr_info->hdr, sizeof(struct boot_img_elf_hdr), 1, f) == 1)
                {
                    hdr->id[0] = '\0';

                    libbootimg_get_elf_version(hdr_info);

                    res = known_magic_pos[i];
                    if (is_elf != NULL)
                    {
                        *is_elf = 1;
                        print_elf_hdr_to_log(hdr_info);
                    }

                    res = libbootimg_load_elf_prog_header(hdr_info, f);
                    if (res != 1)
                    {
                        break;
                    }

                    // Update the standard boot image meta-information with
                    // the information found in the elf file
                    struct boot_img_elf_prog_hdr *kernel_hdr = get_elf_proc_hdr_of(hdr_info,
                            LIBBOOTIMG_BLOB_KERNEL);
                    hdr->kernel_size = kernel_hdr->size;
                    hdr->kernel_addr = kernel_hdr->offset;

                    struct boot_img_elf_prog_hdr *ramdisk_hdr = get_elf_proc_hdr_of(hdr_info,
                            LIBBOOTIMG_BLOB_RAMDISK);
                    hdr->ramdisk_size = ramdisk_hdr->size;
                    hdr->ramdisk_addr = ramdisk_hdr->offset;

                    struct boot_img_elf_prog_hdr *second_hdr = get_elf_proc_hdr_of(hdr_info,
                            LIBBOOTIMG_BLOB_SECOND);
                    if (second_hdr != NULL)
                    {
                        hdr->second_size = second_hdr->size;
                        hdr->second_addr = second_hdr->offset;
                    }
                    else
                    {
                        hdr->second_size = 0;
                        hdr->second_addr = 0;
                    }

                    struct boot_img_elf_prog_hdr *dtb_hdr = get_elf_proc_hdr_of(hdr_info,
                            LIBBOOTIMG_BLOB_DTB);
                    if (dtb_hdr != NULL)
                    {
                        hdr->dt_size = dtb_hdr->size;
                        hdr->tags_addr = dtb_hdr->offset;
                    }
                    else
                    {
                        hdr->dt_size = 0;
                        hdr->tags_addr = 0;
                    }

                    // ELF files version 1 are page aligned, version 2 are not
                    if (hdr_info->elf_version == VER_ELF_1)
                    {
                        hdr->page_size = hdr->kernel_addr;
                    }
                    else
                    {
                        hdr->page_size = DEFAULT_PAGE_SIZE;
                    }

                    if (hdr_info->elf_version == VER_ELF_1)
                    {
                        res = libbootimg_load_elf_misc_header(hdr_info, hdr->page_size, f);
                        if (res != 1)
                        {
                            break;
                        }
                        memcpy(hdr->name, hdr_info->misc->name, BOOT_NAME_SIZE);
                    }
                    else if (hdr_info->elf_version == VER_ELF_2)
                    {
                        res = libbootimg_load_elf_sect_header(hdr_info, f);
                        if (res != 1)
                        {
                            break;
                        }
                    }

                    libbootimg_read_cmdline(hdr, hdr_info, f);

                    print_hdr_to_log(hdr);
                    break;
                }
            }
        }
        else
        {
            res = translate_fread_error(f);
            break;
        }
    }

    fclose(f);
    return res;
}

void libbootimg_get_elf_version(struct boot_img_elf_info *hdr_info)
{
    switch (hdr_info->hdr.phnum)
    {
        case 3:
            // Version 2 (found in Z2, 8960) has 3 program headers + 1 section header
            hdr_info->elf_version = VER_ELF_2;
            // Also the bootloader does not load modified elf images
            // Output to standard Android container format
            hdr_info->elf_out_format = OUT_AND;
            break;
        case 4:
        default:
            // Version 1 (found in SP, 8960) has 4 program headers
            hdr_info->elf_version = VER_ELF_1;
            hdr_info->elf_out_format = OUT_ELF;
            break;
    }
}

int libbootimg_load_elf_prog_header(struct boot_img_elf_info *hdr_info, FILE *f)
{
    int res = 0;
    int prog_entry_size = sizeof(struct boot_img_elf_prog_hdr);
    struct boot_img_elf_prog_hdr *prog_entry;
    prog_entry = malloc(prog_entry_size * hdr_info->hdr.phnum);
    hdr_info->prog = prog_entry;

    int prog_entry_idx = 0;
    for (; prog_entry_idx < hdr_info->hdr.phnum; ++prog_entry_idx)
    {
        LOG_DBG("Reading program header %u/%u.\n",
                prog_entry_idx + 1, hdr_info->hdr.phnum);
        res = fread(&prog_entry[prog_entry_idx], prog_entry_size, 1, f);
        if (res == 1)
        {
            LOG_DBG("Program header %u/%u successfully read.\n",
                    prog_entry_idx + 1, hdr_info->hdr.phnum);
            print_elf_prog_hdr_to_log(&hdr_info->prog[prog_entry_idx]);
        }
        else
        {
            LOG_DBG("Program header %u/%u read failed.\n",
                    prog_entry_idx + 1, hdr_info->hdr.phnum);
            break;
        }
    }
    return res;
}

int libbootimg_load_elf_sect_header(struct boot_img_elf_info *hdr_info, FILE *f)
{
    int res = 0;
    int sect_entry_size = sizeof(struct boot_img_elf_sect_hdr);
    struct boot_img_elf_sect_hdr *sect_entry;

    fseek(f, hdr_info->hdr.shoff, SEEK_SET);
    sect_entry = malloc(hdr_info->hdr.shentsize * hdr_info->hdr.shnum);
    hdr_info->sect = sect_entry;

    int sect_entry_idx = 0;
    for (; sect_entry_idx < hdr_info->hdr.shnum; ++sect_entry_idx)
    {
        LOG_DBG("Reading section header %u/%u.\n", sect_entry_idx + 1, hdr_info->hdr.shnum);
        res = fread(&sect_entry[sect_entry_idx], sect_entry_size, 1, f);

        if (res == 1)
        {
            LOG_DBG("Section header %u/%u successfully read.\n",
                    sect_entry_idx + 1, hdr_info->hdr.shnum);
            print_elf_sect_hdr_to_log(&hdr_info->sect[sect_entry_idx]);
        }
        else
        {
            LOG_DBG("Section header %u/%u read failed.\n",
                    sect_entry_idx + 1, hdr_info->hdr.shnum);
            break;
        }
    }

    return res;
}

int libbootimg_load_elf_misc_header(struct boot_img_elf_info *hdr_info, uint32_t page_size, FILE *f)
{
    if (hdr_info->elf_version != VER_ELF_1)
    {
        return -1;
    }

    int res = 0;
    int misc_entry_size = sizeof(struct boot_img_elf_misc_hdr);
    struct boot_img_elf_misc_hdr *misc;

    int misc_offset = hdr_info->hdr.phoff + hdr_info->hdr.phnum * hdr_info->hdr.phentsize;

    fseek(f, misc_offset, SEEK_SET);
    misc = malloc(misc_entry_size);
    hdr_info->misc = misc;

    misc->data_size = (page_size - misc_offset - sizeof(misc->name)) *
            sizeof(uint8_t);
    misc->data = malloc(misc->data_size);

    res = fread(misc->data, misc->data_size, 1, f);
    if (res == 1)
    {
        res = fread(misc->name, sizeof(misc->name), 1, f);
    }

    LOG_DBG("Misc data size: %u / %u\n", misc->data_size, page_size);
    LOG_DBG("Misc boot name: %s\n", misc->name);

    return res;
}

void libbootimg_read_cmdline(struct boot_img_hdr *hdr, struct boot_img_elf_info *elf_info, FILE *f)
{
    unsigned char buf[BOOT_ARGS_SIZE];
    int cmdline_start_pos = 0;
    int cmd_len_max = 0;

    if (elf_info->elf_version == VER_ELF_1)
    {
        cmdline_start_pos = elf_info->prog[ELF_PROG_CMD].offset;
        elf_info->cmdline_size = elf_info->prog[ELF_PROG_CMD].size;
        cmd_len_max = elf_info->cmdline_size;
    }
    else if (elf_info->elf_version == VER_ELF_2)
    {
        cmdline_start_pos = elf_info->sect[ELF_SECT_CMD].offset;
        elf_info->cmdline_size = elf_info->sect[ELF_SECT_CMD].size;
        cmd_len_max = elf_info->cmdline_size;
    }
    cmd_len_max = cmd_len_max < BOOT_ARGS_SIZE ? cmd_len_max : BOOT_ARGS_SIZE;

    memset(&hdr->cmdline, '\0', BOOT_ARGS_SIZE);
    memset(&buf, '\0', BOOT_ARGS_SIZE);
    fseek(f, cmdline_start_pos, SEEK_SET);
    fread(buf, cmd_len_max, 1, f);

    int buf_offset = 0;
    if (elf_info->elf_version == VER_ELF_2)
    {
        buf_offset = 8;
    }

    int buf_idx = 0;
    for (; buf_idx < cmd_len_max; ++buf_idx)
    {
        hdr->cmdline[buf_idx] = buf[buf_idx + buf_offset];
        if (buf[buf_idx + buf_offset] == '\0')
        {
            break;
        }
    }
    LOG_DBG("Cmd line: %s\n", hdr->cmdline);
}

struct boot_img_elf_prog_hdr* get_elf_proc_hdr_of(struct boot_img_elf_info *elf_info, int type)
{
    switch (type)
    {
        case LIBBOOTIMG_BLOB_KERNEL:
            return &elf_info->prog[ELF_PROG_KER];
        case LIBBOOTIMG_BLOB_RAMDISK:
            return &elf_info->prog[ELF_PROG_RAM];
        case LIBBOOTIMG_BLOB_SECOND:
            if (elf_info->elf_version == VER_ELF_1)
            {
                return &elf_info->prog[ELF_PROG_RPM];
            }
            break;
        case LIBBOOTIMG_BLOB_DTB:
            if (elf_info->elf_version == VER_ELF_2)
            {
                return &elf_info->prog[ELF_PROG_RPM];
            }
            break;
        default:
            break;
    }
    return NULL;
}

int libbootimg_update_headers(struct bootimg *b)
{
    uint32_t addr = 0;

    if (b == NULL)
        return translate_errnum(ENOENT);

    if (b->is_elf && b->hdr_info->elf_out_format == OUT_ELF)
    {
        b->hdr_info->prog[ELF_PROG_KER].size = b->hdr.kernel_size;
        b->hdr_info->prog[ELF_PROG_KER].msize = b->hdr.kernel_size;
        b->hdr_info->prog[ELF_PROG_RAM].size = b->hdr.ramdisk_size;
        b->hdr_info->prog[ELF_PROG_RAM].msize = b->hdr.ramdisk_size;
        b->hdr_info->prog[ELF_PROG_RPM].size = b->hdr.second_size;
        b->hdr_info->prog[ELF_PROG_RPM].msize = b->hdr.second_size;

        if (b->hdr_info->elf_version == VER_ELF_1)
        {
            b->hdr_info->prog[ELF_PROG_CMD].size = b->hdr_info->cmdline_size;
            b->hdr_info->prog[ELF_PROG_CMD].msize = b->hdr_info->cmdline_size;
        }
        else
        {
            b->hdr_info->prog[ELF_PROG_CMD].size = b->hdr.dt_size;
            b->hdr_info->prog[ELF_PROG_CMD].msize = b->hdr.dt_size;
        }

        LOG_DBG("Updating program headers...\n");

        addr = b->hdr_info->prog[ELF_PROG_KER].offset;

        addr += b->hdr_info->prog[ELF_PROG_KER].size;
        b->hdr_info->prog[ELF_PROG_RAM].offset = addr;

        addr += b->hdr_info->prog[ELF_PROG_RAM].size;
        b->hdr_info->prog[ELF_PROG_RPM].offset = addr;

        addr += b->hdr_info->prog[ELF_PROG_RPM].size;
        b->hdr_info->prog[ELF_PROG_CMD].offset = addr;

        addr += b->hdr_info->prog[ELF_PROG_CMD].size;

        // Also, we need to update the address/offset pointers & sizes
        // in the section header table
        int sect_entry_idx = 0;
        for (; sect_entry_idx < b->hdr_info->hdr.shnum; ++sect_entry_idx)
        {
            LOG_DBG("Updating section header entry %d/%d...\n",
                    sect_entry_idx + 1, b->hdr_info->hdr.shnum);
            b->hdr_info->sect[sect_entry_idx].offset = addr;
            addr += b->hdr_info->sect[sect_entry_idx].size;
        }

        if (b->hdr_info->elf_version == VER_ELF_1)
        {
            LOG_DBG("Updating misc header: name %s...\n", b->hdr.name);
            memcpy(b->hdr_info->misc->name, b->hdr.name, BOOT_NAME_SIZE);
        }

        // The section header is placed after the last section
        if (b->hdr_info->hdr.shnum > 0)
        {
            b->hdr_info->hdr.shoff = addr;
        }

    }
    else if (b->is_elf && b->hdr_info->elf_out_format == OUT_AND)
    {
        // ELF format found in stock Sony ROMs for, e.g, the Xperia Z2.
        // If more formats exist, we should differ the assignments at
        // the bottom of this function by the "elf version"

        b->hdr.magic[0] = 'A';
        b->hdr.magic[1] = 'N';
        b->hdr.magic[2] = 'D';
        b->hdr.magic[3] = 'R';
        b->hdr.magic[4] = 'O';
        b->hdr.magic[5] = 'I';
        b->hdr.magic[6] = 'D';
        b->hdr.magic[7] = '!';

        b->hdr.kernel_addr = b->hdr_info->prog[ELF_PROG_KER].paddr;
        b->hdr.ramdisk_addr = b->hdr_info->prog[ELF_PROG_RAM].paddr;
        b->hdr.tags_addr = b->hdr_info->prog[ELF_PROG_RPM].paddr;
    }

    print_hdr_to_log(&b->hdr);
    return 0;
}

int libbootimg_dump_blob(struct bootimg_blob *blob, const char *dest)
{
    FILE *f;
    int res = 0;

    if(blob->data == NULL)
        return LIBBOOTIMG_ERROR_NO_BLOB_DATA;

    f = fopen(dest, "w");
    if(!f)
        return translate_errnum(errno);

    if(fwrite(blob->data, *blob->size, 1, f) != 1)
        res = LIBBOOTIMG_ERROR_IO;

    fclose(f);
    return res;
}

int libbootimg_dump_kernel(struct bootimg *b, const char *dest)
{
    return libbootimg_dump_blob(&b->blobs[LIBBOOTIMG_BLOB_KERNEL], dest);
}

int libbootimg_dump_ramdisk(struct bootimg *b, const char *dest)
{
    return libbootimg_dump_blob(&b->blobs[LIBBOOTIMG_BLOB_RAMDISK], dest);
}

int libbootimg_dump_second(struct bootimg *b, const char *dest)
{
    return libbootimg_dump_blob(&b->blobs[LIBBOOTIMG_BLOB_SECOND], dest);
}

int libbootimg_dump_dtb(struct bootimg *b, const char *dest)
{
    return libbootimg_dump_blob(&b->blobs[LIBBOOTIMG_BLOB_DTB], dest);
}

int libbootimg_load_blob(struct bootimg_blob *blob, const char *src)
{
    FILE *f;
    int res = 0;
    uint8_t *data;
    struct stat info;

    if(stat(src, &info) < 0)
        return translate_errnum(errno);

    if(info.st_size > INT_MAX)
        return LIBBOOTIMG_ERROR_FILE_TOO_BIG;

    // probably /dev/null
    if(info.st_size == 0)
    {
        free(blob->data);
        blob->data = NULL;
        *blob->size = 0;
        return 0;
    }

    f = fopen(src, "r");
    if(!f)
        return translate_errnum(errno);

    data = malloc(info.st_size);

    if(fread(data, info.st_size, 1, f) == 1)
    {
        free(blob->data);
        blob->data = data;
        *blob->size = info.st_size;
    }
    else
    {
        res = translate_fread_error(f);
        free(data);
    }

    fclose(f);
    return res;
}

int libbootimg_load_kernel(struct bootimg *b, const char *src)
{
    return libbootimg_load_blob(&b->blobs[LIBBOOTIMG_BLOB_KERNEL], src);
}

int libbootimg_load_ramdisk(struct bootimg *b, const char *src)
{
    return libbootimg_load_blob(&b->blobs[LIBBOOTIMG_BLOB_RAMDISK], src);
}

int libbootimg_load_second(struct bootimg *b, const char *src)
{
    return libbootimg_load_blob(&b->blobs[LIBBOOTIMG_BLOB_SECOND], src);
}

int libbootimg_load_dtb(struct bootimg *b, const char *src)
{
    return libbootimg_load_blob(&b->blobs[LIBBOOTIMG_BLOB_DTB], src);
}

int libbootimg_write_img(struct bootimg *b, const char *dest)
{
    FILE *f;
    int res;

    f = fopen(dest, "w");
    if(!f)
        return translate_errnum(errno);

    res = libbootimg_write_img_fileptr(b, f);

    fclose(f);
    return res;
}

int libbootimg_write_img_fileptr(struct bootimg *b, FILE *f)
{
    int i;
    int res = 0;
    char *blank = NULL;
    size_t padding = 0;
    struct bootimg_blob *blob;
    int pos_start;
    int pos_end = 0;

    LOG_DBG("Writing.\n");
    pos_start = ftell(f);
    if(pos_start < 0)
        return translate_errnum(errno);

    if(b->hdr.kernel_size == 0 || b->hdr.ramdisk_size == 0)
        return LIBBOOTIMG_ERROR_MISSING_BLOB;

    if(b->hdr.page_size < sizeof(b->hdr))
        return LIBBOOTIMG_ERROR_INVALID_PAGESIZE;

    // make sure it ends with 0
    b->hdr.cmdline[BOOT_ARGS_SIZE-1] = 0;

    // set unused field to 0 - we might not be handling something
    // which gets turned-on by this field, like with dtb
    b->hdr.unused = 0;

    fill_id_hashes(b);

    blank = malloc(b->hdr.page_size);
    memset(blank, 0, b->hdr.page_size);

    // Update & write header
    libbootimg_update_headers(b);
    if (!b->is_elf || b->hdr_info->elf_out_format == OUT_AND)
    {
        if (fwrite(&b->hdr, sizeof(b->hdr), 1, f) != 1)
            goto fail_fwrite;

        padding = align_size(sizeof(b->hdr), b->hdr.page_size) - sizeof(b->hdr);
    }
    else if (b->is_elf && b->hdr_info->elf_out_format == OUT_ELF)
    {
        if (fwrite(&b->hdr_info->hdr, sizeof(b->hdr_info->hdr), 1, f) != 1)
        {
            goto fail_fwrite;
        }

        // Write the Program headers.
        int num_prog_hdr = b->hdr_info->hdr.phnum;
        int hdr_entry_idx = 0;
        for (; hdr_entry_idx < num_prog_hdr; ++hdr_entry_idx)
        {
            print_elf_prog_hdr_to_log(&b->hdr_info->prog[hdr_entry_idx]);
            if (fwrite(&b->hdr_info->prog[hdr_entry_idx],
                    b->hdr_info->hdr.phentsize, 1, f) == 1)
            {
                LOG_DBG("Program header %u/%u writing successful.\n",
                        hdr_entry_idx + 1, b->hdr_info->hdr.phnum);
            }
            else
            {
                LOG_DBG("Program failed %u/%u writing failed.\n",
                        hdr_entry_idx + 1, b->hdr_info->hdr.phnum);
                goto fail_fwrite;
            }
        }

        if (b->hdr_info->elf_version == VER_ELF_1)
        {
            print_elf_misc_hdr_to_log(b->hdr_info->misc);
            if (fwrite(b->hdr_info->misc->data, b->hdr_info->misc->data_size,
                    1, f) != 1)
            {
                LOG_DBG("Misc data writing successful.\n");
                goto fail_fwrite;
            }
            else if (fwrite(b->hdr_info->misc->name,
                    sizeof(b->hdr_info->misc->name), 1, f) != 1)
            {
                LOG_DBG("Misc boot name writing failed.\n");
                goto fail_fwrite;
            }
            else
            {
                LOG_DBG("Misc header writing successful.\n");
            }
        }
        else if (b->hdr_info->elf_version == VER_ELF_2)
        {
            // Fill remaining block with 0s up to first prog entry
            // (misc data NOT written back!)
            uint32_t header_size = sizeof(b->hdr_info->hdr) +
                    b->hdr_info->hdr.phnum * b->hdr_info->hdr.phentsize +
                    b->hdr_info->hdr.shnum * b->hdr_info->hdr.shentsize;
            uint32_t blank_size = b->hdr.page_size - header_size;
            if (fwrite(blank, blank_size, 1, f) != blank_size)
            {
                goto fail_fwrite;
            }
        }
    }

    if (!b->is_elf || b->hdr_info->elf_out_format == OUT_AND)
    {
        if (fwrite(blank, 1, padding, f) != padding)
            goto fail_fwrite;
    }

    for (i = 0; i < LIBBOOTIMG_BLOB_CNT; ++i)
    {
        blob = &b->blobs[i];

        if (*blob->size == 0)
        {
            LOG_DBG("Ignored empty blob number %d/%d...\n", i + 1, LIBBOOTIMG_BLOB_CNT);
            continue;
        }

        LOG_DBG("Writing blob number %d/%d...\n", i + 1, LIBBOOTIMG_BLOB_CNT);
        LOG_DBG("Writing to position 0x%lx.\n", ftell(f));

        if (fwrite(blob->data, *blob->size, 1, f) != 1)
        {
            LOG_DBG("Writing blob %d failed.\n", i + 1);
            goto fail_fwrite;
        }

        if (!b->is_elf || b->hdr_info->elf_out_format == OUT_AND)
        {
            padding = align_size(*blob->size, b->hdr.page_size) - *blob->size;
            if (fwrite(blank, 1, padding, f) != padding)
            {
                LOG_DBG("Writing padding of blob %d failed.\n", i + 1);
                goto fail_fwrite;
            }
        }
    }

    if (b->is_elf && b->hdr_info->elf_out_format == OUT_ELF)
    {
        if (b->hdr_info->elf_version == VER_ELF_1)
        {
            // Write the cmdline (last part ref by the prog header)
            pos_end += b->hdr_info->cmdline_size;
            LOG_DBG("cmdline: %s\n", b->hdr.cmdline);
            LOG_DBG("cmdline size: %u\n", b->hdr_info->cmdline_size);
            if (fwrite(&b->hdr.cmdline, b->hdr_info->cmdline_size, 1, f) != 1)
            {
                LOG_DBG("Failed to write the cmdline.\n");
                goto fail_fwrite;
            }
        }

        // Write the section header if needed by the ELF
        if (b->hdr_info->hdr.shnum > 0)
        {
            LOG_DBG("Writing section header.\n");
            fseek(f, b->hdr_info->hdr.shoff, SEEK_SET);
            if (fwrite(b->hdr_info->sect, b->hdr_info->hdr.shentsize, 1, f) == 1)
            {
                print_elf_sect_hdr_to_log(&b->hdr_info->sect[ELF_SECT_CMD]);
            }
            else
            {
                goto fail_fwrite;
            }
        }
    }

    pos_end = ftell(f);

    if (pos_end > 0)
        res = pos_end - pos_start;
    else
        res = translate_errnum(errno);

    goto exit;
fail_fwrite:
    res = LIBBOOTIMG_ERROR_IO;
exit:
    free(blank);
    LOG_DBG("Done writing.\n");
    return res;
}

int libbootimg_write_img_and_destroy(struct bootimg *b, const char *dest)
{
    int res = libbootimg_write_img(b, dest);
    libbootimg_destroy(b);
    return res;
}

uint8_t libbootimg_architecture(void)
{
    FILE* cpuinfo;
    char buffer[100];

    memset(buffer, 0, sizeof(buffer));
    cpuinfo = fopen("/proc/cpuinfo", "rb");

    if (cpuinfo == NULL)
    {
        return ARCH_32_BITS;
    }

    if (fread(buffer, 1, sizeof(buffer), cpuinfo) == 0)
    {
        fclose(cpuinfo);
        return ARCH_32_BITS;
    }
    fclose(cpuinfo);

    if (strstr(buffer, "aarch64")) {
        return ARCH_64_BITS;
    }

    return ARCH_32_BITS;
}

uint32_t libbootimg_version(void)
{
    return LIBBOOTIMG_VERSION;
}

const char *libbootimg_version_str(void)
{
    return LIBBOOTIMG_VERSION_STR;
}

const char *libbootimg_error_str(int error)
{
    switch (error)
    {
        case LIBBOOTIMG_SUCCESS:                return "No errors";
        case LIBBOOTIMG_ERROR_IO:               return "Input/output error";
        case LIBBOOTIMG_ERROR_ACCESS:           return "Permission denied";
        case LIBBOOTIMG_ERROR_NOT_FOUND:        return "No such file or directory";
        case LIBBOOTIMG_ERROR_INVALID_MAGIC:    return "Corrupted boot image, invalid magic";
        case LIBBOOTIMG_ERROR_IMG_EOF:          return "Corrupted boot image, premature end of file";
        case LIBBOOTIMG_ERROR_NO_BLOB_DATA:     return "No data loaded into this blob";
        case LIBBOOTIMG_ERROR_FILE_TOO_BIG:     return "File is too big";
        case LIBBOOTIMG_ERROR_MISSING_BLOB:     return "Missing required blob (kernel or ramdisk)";
        case LIBBOOTIMG_ERROR_INVALID_PAGESIZE: return "Invalid pagesize value";

        case LIBBOOTIMG_ERROR_OTHER:            return "Unhandled error";
        default:                                return "Unknown error";
    }
}

void print_hdr_to_log(struct boot_img_hdr* hdr)
{
    LOG_DBG("* architecture = %s\n",
            libbootimg_architecture() == ARCH_64_BITS ? "64bits" : "32bits");

    LOG_DBG("* kernel size       = %u bytes (%.2f MB)\n", hdr->kernel_size,
            (double )hdr->kernel_size / 0x100000);
    LOG_DBG("  ramdisk size      = %u bytes (%.2f MB)\n", hdr->ramdisk_size,
            (double )hdr->ramdisk_size / 0x100000);
    if (hdr->second_size)
    {
        LOG_DBG("  second stage size = %u bytes (%.2f MB)\n", hdr->second_size,
                (double )hdr->second_size / 0x100000);
    }
    if (hdr->dt_size)
    {
        LOG_DBG("  device tree size  = %u bytes (%.2f MB)\n", hdr->dt_size,
                (double )hdr->dt_size / 0x100000);
    }
    LOG_DBG("* load addresses:\n");
    LOG_DBG("  kernel:       0x%08x\n", hdr->kernel_addr);
    LOG_DBG("  ramdisk:      0x%08x\n", hdr->ramdisk_addr);
    if (hdr->second_size)
    {
        LOG_DBG("  second stage: 0x%08x\n", hdr->second_addr);
    }
    LOG_DBG("  tags:         0x%08x\n", hdr->tags_addr);
}

void print_elf_hdr_to_log(struct boot_img_elf_info* elf_info)
{
    (void)elf_info;
    LOG_DBG("\n");
    LOG_DBG("========= ELF header content =========\n");
    LOG_DBG("Architecture                   = %s\n",
            libbootimg_architecture() == ARCH_64_BITS ? "64bits" : "32bits");
    LOG_DBG("ELF Version                    = %x\n", elf_info->elf_version);
    LOG_DBG("Output format                  = %s\n", elf_info->elf_out_format == OUT_ELF ? "ELF" : "ANDROID");
    LOG_DBG("Type                           = %u\n", elf_info->hdr.type);
    LOG_DBG("Machine                        = %u\n", elf_info->hdr.machine);
    LOG_DBG("Version                        = %u\n", elf_info->hdr.version);
    LOG_DBG("Entry Address                  = %x\n", elf_info->hdr.entry_addr);
    LOG_DBG("Program Offset                 = %x\n", elf_info->hdr.phoff);
    LOG_DBG("Section Offset                 = %x\n", elf_info->hdr.shoff);
    LOG_DBG("Flags                          = %x\n", elf_info->hdr.flags);
    LOG_DBG("Ehsize                         = %u\n", elf_info->hdr.ehsize);
    LOG_DBG("Program headers size           = %u\n", elf_info->hdr.phentsize);
    LOG_DBG("Program headers number         = %u\n", elf_info->hdr.phnum);
    LOG_DBG("Section headers size           = %u\n", elf_info->hdr.shentsize);
    LOG_DBG("Section headers number         = %u\n", elf_info->hdr.shnum);
    LOG_DBG("Section strndx                 = %x\n", elf_info->hdr.shstrndx);
    LOG_DBG("======================================\n");
}

void print_elf_prog_hdr_to_log(struct boot_img_elf_prog_hdr* elf_prog_hdr)
{
    (void)elf_prog_hdr;
    LOG_DBG("===== ELF program header content =====\n");
    LOG_DBG("Type                           = %x\n", elf_prog_hdr->type);
    LOG_DBG("Offset                         = %x\n", elf_prog_hdr->offset);
    LOG_DBG("VAddr                          = %x\n", elf_prog_hdr->vaddr);
    LOG_DBG("PAddr                          = %x\n", elf_prog_hdr->paddr);
    LOG_DBG("Size                           = %u\n", elf_prog_hdr->size);
    LOG_DBG("MSize                          = %u\n", elf_prog_hdr->msize);
    LOG_DBG("Flags                          = %x\n", elf_prog_hdr->flags);
    LOG_DBG("Align                          = %x\n", elf_prog_hdr->align);
    LOG_DBG("======================================\n");
}

void print_elf_sect_hdr_to_log(struct boot_img_elf_sect_hdr* elf_sect_hdr)
{
    (void)elf_sect_hdr;
    LOG_DBG("===== ELF section header content =====\n");
    LOG_DBG("Name                           = %x\n", elf_sect_hdr->name);
    LOG_DBG("Type                           = %x\n", elf_sect_hdr->type);
    LOG_DBG("Flags                          = %x\n", elf_sect_hdr->flags);
    LOG_DBG("Address                        = %x\n", elf_sect_hdr->addr);
    LOG_DBG("Offset                         = %x\n", elf_sect_hdr->offset);
    LOG_DBG("Size                           = %u\n", elf_sect_hdr->size);
    LOG_DBG("======================================\n");
}

void print_elf_misc_hdr_to_log(struct boot_img_elf_misc_hdr* elf_misc_hdr)
{
    (void)elf_misc_hdr;
    LOG_DBG("====== ELF misc header content =======\n");
    LOG_DBG("Data size                      = %u\n", elf_misc_hdr->data_size);
    LOG_DBG("Boot name                      = %s\n", elf_misc_hdr->name);
    LOG_DBG("======================================\n");
}
