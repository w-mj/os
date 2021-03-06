#include <acpi.h>
#include <base.h>
#include <boot.h>
#include <delog.h>

#define SIG_RSDP ( ((u64) 'R' <<  0) | ((u64) 'S' <<  8) \
                 | ((u64) 'D' << 16) | ((u64) ' ' << 24) \
                 | ((u64) 'P' << 32) | ((u64) 'T' << 40) \
                 | ((u64) 'R' << 48) | ((u64) ' ' << 56) )
#define SIG_MADT ( ((u32) 'A' <<  0) | ((u32) 'P' <<  8) \
                 | ((u32) 'I' << 16) | ((u32) 'C' << 24) )
#define SIG_MCFG ( ((u32) 'M' <<  0) | ((u32) 'C' <<  8) \
                 | ((u32) 'F' << 16) | ((u32) 'G' << 24) )
#define SIG_HPET ( ((u32) 'H' <<  0) | ((u32) 'P' <<  8) \
                 | ((u32) 'E' << 16) | ((u32) 'T' << 24) )
#define SIG_FADT ( ((u32) 'F' <<  0) | ((u32) 'A' <<  8) \
                 | ((u32) 'C' << 16) | ((u32) 'P' << 24) )

madt_t * acpi_madt = NULL;
mcfg_t * acpi_mcfg = NULL;
fadt_t * acpi_fadt = NULL;
hpet_t * acpi_hpet = NULL;

static __INIT u8 calc_tbl_checksum(acpi_tbl_t * header) {
    u8 * end = (u8 *) header + header->length;
    u8   sum = 0;
    for (u8 * p = (u8 *) header; p < end; ++p) {
        sum += *p;
    }
    return sum;
}

static __INIT void store_acpi_tbl(acpi_tbl_t * tbl) {
	// _sL(tbl);
    switch (tbl->signature) {
    case SIG_MADT:
        logd("find madt table");
        acpi_madt = (madt_t *) tbl;
        break;
    case SIG_MCFG:
        logd("find mcfg table");
        acpi_mcfg = (mcfg_t *) tbl;
        break;
    case SIG_HPET:
        logd("find hpet table");
        acpi_hpet = (hpet_t *) tbl;
        break;
    case SIG_FADT:
        logd("find fadt table");
        acpi_fadt = (fadt_t *) tbl;
        break;
    default:
        break;
    }
}

static __INIT void parse_rsdt(acpi_rsdt_t * rsdt) {
    if (0 != calc_tbl_checksum(&(rsdt->header))) {
        return;
    }
	// _sa(rsdt, 128);

    int count = (rsdt->header.length - sizeof(acpi_tbl_t )) / sizeof(u32);
	_si(count);
    for (int i = 0; i < count; ++i) {
        store_acpi_tbl((acpi_tbl_t *) phys_to_virt(rsdt->entries[i]));
    }
}

static __INIT void parse_xsdt(acpi_xsdt_t * xsdt) {
    if (0 != calc_tbl_checksum(&(xsdt->header))) {
        return;
    }

    int count = (xsdt->header.length - sizeof(acpi_tbl_t )) / sizeof(u64);
    for (int i = 0; i < count; ++i) {
        store_acpi_tbl((acpi_tbl_t *) phys_to_virt(xsdt->entries[i]));
    }
}

static __INIT acpi_rsdp_t * find_rsdp() {
    u64 sig = SIG_RSDP;
    u64 * begin;
    u64 * end;

    // get EBDA base address from BIOS, and search in the first KB of EBDA
    u16 ebda_base = * (u16 *) phys_to_virt(0x40e);
    begin = (u64 *) phys_to_virt (ebda_base << 4);
    end   = (u64 *) phys_to_virt((ebda_base << 4) + 1024);
    for (u64 * p = begin; p < end; p += 2) {
        if (sig == *p) {
            return (acpi_rsdp_t *) p;
        }
    }

    // search the main BIOS area below 1M
    begin = (u64 *) phys_to_virt(0x000e0000);
    end   = (u64 *) phys_to_virt(0x00100000);
    for (u64 * p = begin; p < end; p += 2) {
        if (sig == *p) {
            return (acpi_rsdp_t *) p;
        }
    }

    return NULL;
}

__INIT void acpi_tbl_init() {
	// 初始化acpi设备表
	// acpi设备表会放在内存中标记为“不可用”的区域
	logd("acpi tbl init");
    acpi_rsdp_t * rsdp = find_rsdp();
    if (NULL == rsdp) {
		loge("rsdp not found");
        return;
    }
	//_sa(rsdp, 128);

    if (rsdp->revision == 0) {
        // version 1
		// length 是不属于version1的第一个字段
        u8 * end = (u8 *) &(rsdp->length);
        u8   sum = 0;
        for (u8 * p = (u8 *) rsdp; p < end; ++p) {
            sum += *p;
        }
        if (0 != sum) {
			loge("acpi rsdp checksum err");
            return;
        }
		// _sa(rsdp->rsdt_addr, 128);
        parse_rsdt((acpi_rsdt_t *) phys_to_virt(rsdp->rsdt_addr));
    } else {
        // version 2
        u8 * end = (u8 *) rsdp + rsdp->length;
        u8   sum = 0;
        for (u8 * p = (u8 *) rsdp; p < end; ++p) {
            sum += *p;
        }
        if (0 != sum) {
			loge("acpi rsdp checksum err");
            return;
        }
        parse_xsdt((acpi_xsdt_t *) phys_to_virt(rsdp->xsdt_addr));
    }
	logd("acpi tbl init finish");
}
