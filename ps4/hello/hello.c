/*
 * ps4/hello/hello.c - the smallest payload that proves the loader works.
 *
 * Build with `./build.sh payload hello`. It does exactly one thing: write a line
 * to the kernel log and put a notification on screen. No decoder, no USB, no
 * virtual pad, none of our own objects.
 *
 * Why it exists: if the full payload shows nothing but this shows its
 * notification, then GoldHEN is loading and running ps4-payload-sdk ELFs
 * correctly, and the fault is inside the full payload (its imports or its
 * logic). If even this shows nothing, the ELF loader is the problem.
 */
#include <orbis/libkernel.h>

/* Defined by the SDK CRT that is linked in (crt/klog.c). */
int klog_printf(const char *fmt, ...);

int main(void)
{
    static const char       msg[] = "XCP HELLO: loader works";
    OrbisNotificationRequest req;
    unsigned                 i;

    klog_printf("XCP HELLO: payload main ran\n");

    /* Zeroed by hand so this file pulls in no libc at all. */
    for (i = 0; i < sizeof(req); i++) ((unsigned char *)&req)[i] = 0;
    req.type     = NotificationRequest;
    req.targetId = -1;
    for (i = 0; i + 1 < sizeof(req.message) && i < sizeof(msg); i++) {
        req.message[i] = msg[i];
    }

    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    return 0;
}
