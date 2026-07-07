#include <stdint.h>
#define SYS_DEBUG_PRINT 0x0800U
#define SYS_VM_MAP      0x0202U

static inline long svc3(uint64_t x8, uint64_t a0, uint64_t a1, uint64_t a2)
{
    register uint64_t r8 asm("x8") = x8;
    register uint64_t r0 asm("x0") = a0;
    register uint64_t r1 asm("x1") = a1;
    register uint64_t r2 asm("x2") = a2;
    asm volatile("svc #0" : "+r"(r0) : "r"(r8), "r"(r1), "r"(r2) : "memory");
    return (long)r0;
}

static void print(const char *s) { uint32_t n=0; while(s[n])n++; svc3(SYS_DEBUG_PRINT,(uint64_t)s,n,0); }

static void print_hex(long val)
{
    char buf[20]; int i;
    static const char hex[] = "0123456789ABCDEF";
    buf[0]='0'; buf[1]='x';
    for(i=0;i<16;i++) buf[2+i]=hex[((uint64_t)val>>((15-i)*4))&0xF];
    buf[18]='\n'; buf[19]='\0';
    print(buf);
}

void _start(void)
{
    long ret;
    print("drv: step1\n");

    /* 映射 virtio MMIO */
    ret = svc3(SYS_VM_MAP, 0x0A000000ULL, 0x1000ULL, 0x3U);
    print("drv: map=");
    print_hex(ret);

    if (ret > 0)
    {
        volatile uint32_t *mmio = (volatile uint32_t *)(uintptr_t)ret;
        uint32_t magic = mmio[0];
        print("drv: magic=");
        print_hex((long)(uint64_t)magic);

        uint32_t devid = mmio[2]; /* DEVICE_ID at offset 0x008 */
        print("drv: devid=");
        print_hex((long)(uint64_t)devid);
    }
    else
    {
        print("drv: map failed\n");
    }

    print("drv: done\n");
    for(;;) asm volatile("wfe");
}
