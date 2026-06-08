#include "sbi.h"
#include "helper.h"

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
                        unsigned long arg1, unsigned long arg2,
                        unsigned long arg3, unsigned long arg4,
                        unsigned long arg5) {
    struct sbiret ret;
    register unsigned long a0 asm("a0") = (unsigned long)arg0;
    register unsigned long a1 asm("a1") = (unsigned long)arg1;
    register unsigned long a2 asm("a2") = (unsigned long)arg2;
    register unsigned long a3 asm("a3") = (unsigned long)arg3;
    register unsigned long a4 asm("a4") = (unsigned long)arg4;
    register unsigned long a5 asm("a5") = (unsigned long)arg5;
    register unsigned long a6 asm("a6") = (unsigned long)fid;
    register unsigned long a7 asm("a7") = (unsigned long)ext;
    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    ret.error = a0;
    ret.value = a1;
    return ret;
}

/**
 * sbi_get_spec_version() - Get the SBI specification version.
 *
 * Return: The current SBI specification version.
 * The minor number of the SBI specification is encoded in the low 24 bits,
 * with the major number encoded in the next 7 bits. Bit 31 must be 0.
 */
long sbi_get_spec_version(void) {
    struct sbiret result = sbi_ecall(
        SBI_EXT_BASE, SBI_EXT_BASE_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0);
    if (result.error) {
        return result.error;
    }
    return result.value;
}

long sbi_get_impl_id(void) {
    struct sbiret result =
        sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_ID, 0, 0, 0, 0, 0, 0);
    if (result.error) {
        return result.error;
    }
    return result.value;
}

long sbi_get_impl_version(void) {
    struct sbiret result =
        sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_VERSION, 0, 0, 0, 0, 0, 0);
    if (result.error) {
        return result.error;
    }
    return result.value;
}

long sbi_probe_extension(long extension_id) {
    struct sbiret result = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_PROBE_EXT,
                                     (unsigned long)extension_id, 0, 0, 0, 0, 0);
    if (result.error) {
        return 0;
    }
    return result.value;
}

static long sbi_set_timer_legacy(uint64_t stime_value) {
    register unsigned long a0 asm("a0") = (unsigned long)stime_value;
    register unsigned long a7 asm("a7") = (unsigned long)SBI_EXT_LEGACY_SET_TIMER;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return (long)a0;
}

long sbi_set_timer(uint64_t stime_value) {
    static int timer_mode = -1; /* -1 unknown, 0 legacy, 1 time-ext */

    if (timer_mode < 0) {
        long probe = sbi_probe_extension(SBI_EXT_TIME);
        long spec = sbi_get_spec_version();
        printf("[SBI] spec=0x%x probe(TIME)=0x%x\n", (unsigned long)spec,
               (unsigned long)probe);
        timer_mode = (probe > 0) ? 1 : 0;
        printf("[SBI] timer mode: %s\n", timer_mode ? "TIME" : "LEGACY");
    }

    if (timer_mode == 1) {
        struct sbiret result =
            sbi_ecall(SBI_EXT_TIME, SBI_EXT_TIME_SET_TIMER,
                      (unsigned long)stime_value, 0, 0, 0, 0, 0);
        if (result.error == -2) {
            timer_mode = 0;
            printf("[SBI] TIME unsupported, switching to LEGACY\n");
            return sbi_set_timer_legacy(stime_value);
        }
        if (result.error) {
            printf("[SBI] TIME set_timer err=%d\n", (int)result.error);
        }
        return result.error;
    }

    return sbi_set_timer_legacy(stime_value);
}
