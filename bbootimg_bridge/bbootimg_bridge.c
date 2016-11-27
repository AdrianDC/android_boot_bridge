/*
 * This file is part of MultiROM.
 *
 * MultiROM is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiROM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiROM.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include <../../multirom/lib/util.h>

#include <libbootimg.h>

int bbootimg_bridge(const char* import_path, const char* export_path)
{
    int res = -1;
    struct bootimg img;
    char* cmdline = NULL;
    char tmp[256];

    if (libbootimg_init_load(&img, import_path, LIBBOOTIMG_LOAD_ALL) < 0)
    {
        printf("Could not open boot image (%s)!\n", import_path);
        return -1;
    }

    if (libbootimg_dump_kernel(&img, "tmp_kernel") < 0)
    {
        printf("Failed to dump the kernel from %s to tmp_kernel!\n", import_path);
        goto exit;
    }

    if (libbootimg_dump_ramdisk(&img, "tmp_ramdisk") < 0)
    {
        printf("Failed to dump the ramdisk from %s to tmp_ramdisk!\n", import_path);
        goto exit;
    }

    if (*(img.blobs[LIBBOOTIMG_BLOB_SECOND].size) != 0 &&
            libbootimg_dump_second(&img, "tmp_second") < 0)
    {
        printf("Failed to dump the second from %s to tmp_second!\n", import_path);
        goto exit;
    }

    if (*(img.blobs[LIBBOOTIMG_BLOB_DTB].size) != 0 &&
            libbootimg_dump_dtb(&img, "tmp_dtb") < 0)
    {
        printf("Failed to dump the dtb from %s to tmp_dtb!\n", import_path);
        goto exit;
    }

    cmdline = strdup((char*)img.hdr.cmdline);
    libbootimg_destroy(&img);

    if (libbootimg_init_load(&img, export_path, LIBBOOTIMG_LOAD_ALL) < 0)
    {
        printf("Could not open boot image (%s)!\n", export_path);
        return -1;
    }

    if (libbootimg_load_kernel(&img, "tmp_kernel") < 0)
    {
        printf("Failed to import the kernel from tmp_kernel to %s!\n", export_path);
        goto exit;
    }

    if (libbootimg_load_ramdisk(&img, "tmp_ramdisk") < 0)
    {
        printf("Failed to import the ramdisk from tmp_ramdisk to %s!\n", export_path);
        goto exit;
    }

    if (*(img.blobs[LIBBOOTIMG_BLOB_SECOND].size) != 0 &&
            libbootimg_load_second(&img, "tmp_second") < 0)
    {
        printf("Failed to import the second from tmp_second to %s!\n", export_path);
        goto exit;
    }

    if (*(img.blobs[LIBBOOTIMG_BLOB_DTB].size) != 0 &&
            libbootimg_load_dtb(&img, "tmp_dtb") < 0)
    {
        printf("Failed to import the dtb from tmp_dtb to %s!\n", export_path);
        goto exit;
    }

    strcpy((char*)img.hdr.cmdline, cmdline);
    img.hdr_info->cmdline_size = strlen(cmdline);

    strcpy(tmp, export_path);
    strcat(tmp, ".new");

    if (libbootimg_write_img(&img, tmp) >= 0)
    {
        printf("Writing boot.img updated with kernel\n");
        if (copy_file(tmp, export_path) < 0) {
            printf("Failed to copy %s to %s!\n", tmp, export_path);
        } else {
            res = 0;
        }
        remove(tmp);
    }
    else
    {
        printf("Failed to libbootimg_write_img!\n");
    }

    free(cmdline);

exit:
    libbootimg_destroy(&img);
    return res;
}

int main(int argc, char *argv[])
{
    int i;
    char *export_path = NULL;
    char *import_path = NULL;

    for (i = 1; i < argc; ++i)
    {
        if (strstartswith(argv[i], "--import="))
        {
            import_path = argv[i] + strlen("--import=");
        }
        else if (strstartswith(argv[i], "--export="))
        {
            export_path = argv[i] + strlen("--export=");
        }
    }

    if (!import_path || !export_path)
    {
        printf("\n");
        printf("bbootimg_bridge --import=[path to origin kernel] --export=[path to output kernel]\n");
        printf("\n");
        fflush(stdout);
        return 1;
    }

    return bbootimg_bridge(import_path, export_path);
}
