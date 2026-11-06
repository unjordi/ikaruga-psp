// Minimal homebrew: prints the project name with the debug screen and waits
// until the user exits from the HOME menu.
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>

PSP_MODULE_INFO("ikaruga-psp hola", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1; (void)arg2; (void)common;
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("exit_callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);
}

int main(void)
{
    setup_callbacks();
    pspDebugScreenInit();
    // Large white-on-black text, centered-ish, so OCR on a capture can read it.
    pspDebugScreenSetBackColor(0xFF000000);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenClear();
    pspDebugScreenSetXY(26, 16);
    pspDebugScreenPrintf("ikaruga-psp");

    for (;;)
        sceDisplayWaitVblankStart();
    return 0;
}
