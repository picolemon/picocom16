#include "picocom/devkit.h"
#include "native_sdl_storage_driver.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __EMSCRIPTEN__
    #include "emscripten.h"
#endif


// globals
char g_StorageDir[256];


//
//
int storage_mount_image(const char* filename, bool readonly)
{
    return SDKErr_Fail;
}


int storage_save_image(const char* filename)
{  
    return SDKErr_OK;
}


int storage_post_save()
{
#ifdef __EMSCRIPTEN__
    printf("storage_post_save\n");
    
    EM_ASM(  
        FS.syncfs(false, function (err) {
            // Error
            console.log("commited:", err);
        });
    );
#endif
    return SDKErr_OK;  
}

void storage_unmount_image()
{
}


bool storage_has_image(const char* filename)
{
    return false;
}


void my_assert_func(const char *file, int line, const char *func,
                    const char *pred) {
    printf("assertion \"%s\" failed: file \"%s\", line %d, function: %s\n",
           pred, file, line, func);
    picocom_panic(SDKErr_Fail, "!assert failed");
}


bool storage_has_mount()
{
    return false;
}


bool storage_set_dir(const char* filename)
{
    strcpy(g_StorageDir, filename);
    return true;
}


const char* storage_get_dir()
{
    return g_StorageDir;
}


bool storage_create_native_dir(const char* filename)
{
    return mkdir(filename, 0755) == 0;
}
