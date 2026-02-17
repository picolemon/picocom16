#include "picocom/devkit.h"
#include "sdl_storage_driver.h"
#include "ff.h" 
#include "diskio.h"
#include <stdio.h>
#include <unistd.h>
#include "thirdparty/miniz/miniz.h"


// Local defs
#define STORAGE_WRITE_COMPRESSED_SHADOW_COPY                // Write as zlib compressed, write raw for opening in os ( can .ignore it etc )


// Globals
SDLDiskImageMount_t g_DiskImage = {0};


//
//
int storage_mount_image(const char* filename, bool readonly)
{
#ifdef STORAGE_WRITE_COMPRESSED_SHADOW_COPY

    // Try compressed
    char compFilename[1024];
    snprintf(compFilename, sizeof(compFilename), "%s.gz", filename);      
    if( access(compFilename, F_OK) == 0 )
    {
    
        // read entire file
        FILE *f = fopen(compFilename, "rb");
        if(!f)
            return SDKErr_Fail;

        // Get size
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET); // set pos

        // read compressed into ram
        uint8_t* compImage = (uint8_t*)picocom_malloc(fsize + 1 + (1024*1024));
        fread(compImage, fsize, 1, f);
        fclose(f);
        
        // read image size & alloc image
        uint32_t headerSz = 0;
        memcpy(&headerSz, compImage, sizeof(headerSz));
        uint8_t* image = (uint8_t*)picocom_malloc(headerSz + 1);

        // zlib decompress                    
        mz_ulong uncomp_len = headerSz;
        int cmp_status = uncompress(image, &uncomp_len, compImage + sizeof(uint32_t), fsize-sizeof(uint32_t));
        if (cmp_status != Z_OK)
        {
            picocom_free( image );
            picocom_free( compImage );

            printf("uncompress() failed!\n");
            return SDKErr_Fail;
        }
        

        // free compressed
        picocom_free( compImage );

        g_DiskImage.readonly = readonly;
        g_DiskImage.image = image;
        g_DiskImage.imageSz = (uint32_t)headerSz;
        
        // set filename
        strncpy(g_DiskImage.mountedFilename, compFilename, sizeof(g_DiskImage.mountedFilename));

        g_DiskImage.mounted = true;

        return SDKErr_OK;
    }
    else // default to non compressed
    {
        // read entire file
        FILE *f = fopen(filename, "rb");
        if(!f)
            return SDKErr_Fail;

        // Get size
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET); // set pos

        g_DiskImage.readonly = readonly;
        g_DiskImage.image = (uint8_t*)picocom_malloc(fsize + 1);
        fread(g_DiskImage.image, fsize, 1, f);
        fclose(f);

        g_DiskImage.imageSz = (uint32_t)fsize;
        
        // set filename as compressed to save as
        strncpy(g_DiskImage.mountedFilename, compFilename, sizeof(g_DiskImage.mountedFilename));

        g_DiskImage.mounted = true;

        return SDKErr_OK;
    }

#else // Raw disk    
    // read entire file
    FILE *f = fopen(filename, "rb");
    if(!f)
        return SDKErr_Fail;

    // Get size
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET); // set pos

    g_DiskImage.readonly = readonly;
    g_DiskImage.image = (uint8_t*)picocom_malloc(fsize + 1);
    fread(g_DiskImage.image, fsize, 1, f);
    fclose(f);

    g_DiskImage.imageSz = (uint32_t)fsize;
    
    // set filename
    strncpy(g_DiskImage.mountedFilename, filename, sizeof(g_DiskImage.mountedFilename));

    g_DiskImage.mounted = true;

    return SDKErr_OK;
#endif
}


int storage_save_image(const char* filename)
{
    if(!g_DiskImage.mounted || !g_DiskImage.imageSz)
        return SDKErr_Fail;

#ifdef STORAGE_WRITE_COMPRESSED_SHADOW_COPY
    char compFilename[1024];
    snprintf(compFilename, sizeof(compFilename), "%s.gz", filename); 

    // Alloc compressed buffer
    mz_ulong compImageSz = g_DiskImage.imageSz + 1024 + sizeof(uint32_t);
    uint8_t* compImage = (uint8_t*)picocom_malloc( compImageSz );

    // Compress image + header offset
    int cmp_status = compress(compImage + sizeof(uint32_t), &compImageSz, g_DiskImage.image, g_DiskImage.imageSz);
    if (cmp_status != Z_OK)
    {        
        picocom_free( compImage );

        printf("compress() failed!\n");
        return SDKErr_Fail;
    }

    // write header
    memcpy(compImage, (void*)& g_DiskImage.imageSz, sizeof(uint32_t) );

    // read entire file
    FILE *f = fopen(compFilename, "wb");
    if(!f)
        return SDKErr_Fail;
    
    size_t writeSz = fwrite(compImage, 1, compImageSz + sizeof(uint32_t), f);
    fclose(f);

    if(writeSz != compImageSz + sizeof(uint32_t))
        return SDKErr_Fail;


    // Save raw copy
    f = fopen(filename, "wb");
    if(!f)
        return SDKErr_Fail;

    writeSz = fwrite(g_DiskImage.image, 1, g_DiskImage.imageSz, f);
    fclose(f);

    if(writeSz != g_DiskImage.imageSz)
        return SDKErr_Fail;


#else // Raw disk

    // read entire file
    FILE *f = fopen(filename, "wb");
    if(!f)
        return SDKErr_Fail;

    size_t writeSz = fwrite(g_DiskImage.image, 1, g_DiskImage.imageSz, f);
    fclose(f);

    if(writeSz != g_DiskImage.imageSz)
        return SDKErr_Fail;

#endif

    return SDKErr_OK;
}


int storage_post_save()
{
    //printf("storage_post_save\n");
    return SDKErr_OK;
}


void storage_unmount_image()
{
    // free image
    if(g_DiskImage.mounted)
    {
        picocom_free(g_DiskImage.image);
        g_DiskImage.image = 0;
        g_DiskImage.imageSz = 0;
    }

    strcpy(g_DiskImage.mountedFilename, "");
    g_DiskImage.mounted = false;
}


bool storage_has_image(const char* filename)
{
#ifdef STORAGE_WRITE_COMPRESSED_SHADOW_COPY
    char compFilename[1024];
    snprintf(compFilename, sizeof(compFilename), "%s.gz", filename);      
    if( access(filename, F_OK) == 0 )
        return 1; // has compressed
    
    return access(filename, F_OK) == 0;

#else // Raw disk    
    return access(filename, F_OK) == 0;
#endif    
}


//
// 
DRESULT disk_write(BYTE pdrv, /* Physical drive nmuber to identify the drive */
                   const BYTE *buff, /* Data to be written */
                   LBA_t sector,     /* Start sector in LBA */
                   UINT count        /* Number of sectors to write */
) 
{
    if(g_DiskImage.readonly)
        return RES_WRPRT;

    size_t size = count * 512;
    size_t offset = sector * 512;
    if(offset + size > g_DiskImage.imageSz)
    {
        return RES_ERROR;
    }
    
    memcpy(g_DiskImage.image + offset, buff, size);

    return RES_OK;
}


DRESULT disk_read(BYTE pdrv,  /* Physical drive nmuber to identify the drive */
                  BYTE *buff, /* Data buffer to store read data */
                  LBA_t sector, /* Start sector in LBA */
                  UINT count    /* Number of sectors to read */
) {
    if(!g_DiskImage.image)
        return RES_NOTRDY;

    size_t size = count * 512;
    size_t offset = sector * 512;
    if(offset + size > g_DiskImage.imageSz)
    {
        return RES_ERROR;
    }
    
    memcpy(buff, g_DiskImage.image + offset, size);

    return RES_OK;
}


DRESULT disk_ioctl(BYTE pdrv, /* Physical drive nmuber (0..) */
                   BYTE cmd,  /* Control code */
                   void *buff /* Buffer to send/receive control data */
) 
{
    if(!g_DiskImage.image)
        return RES_NOTRDY;

    switch (cmd) 
    {
        case GET_SECTOR_COUNT: 
        { 
            static LBA_t n;
            n = g_DiskImage.imageSz / 512;
            *(LBA_t *)buff = n;
            if (!n) return RES_ERROR;
            return RES_OK;      
        }
        case GET_BLOCK_SIZE: 
        {
            static DWORD bs = 1;
            *(DWORD *)buff = bs;
            return RES_OK;    
        }
        case CTRL_SYNC:
        {
            return RES_OK;                         
        }
        default:
        {       
            return RES_PARERR;                                          
        }
    }   
}


DSTATUS disk_status(BYTE pdrv /* Physical drive nmuber to identify the drive */ ) 
{
    if(g_DiskImage.readonly)
        return STA_PROTECT;
    return 0; // See http://elm-chan.org/fsw/ff/doc/dstat.html
}


DSTATUS disk_initialize( BYTE pdrv ) 
{
    if(g_DiskImage.readonly)
        return STA_PROTECT;
   return 0; // See http://elm-chan.org/fsw/ff/doc/dstat.html
}


void my_assert_func(const char *file, int line, const char *func,
                    const char *pred) {
    printf("assertion \"%s\" failed: file \"%s\", line %d, function: %s\n",
           pred, file, line, func);
    picocom_panic(SDKErr_Fail, "!assert failed");
}


bool storage_has_mount()
{
    return g_DiskImage.mounted;
}
