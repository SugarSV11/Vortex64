// Generated from Project64/UserInterface/Debugger/Debugger-Commands.cpp (GetDataAddressNotes, GetCodeAddressNotes): the annotation tables of the command list, verbatim.
static const char * GetDataAddressNotes(uint32_t vAddr)
{
    switch (vAddr)
    {
    case 0xA3F00000: return "RDRAM_CONFIG_REG/RDRAM_DEVICE_TYPE_REG";
    case 0xA3F00004: return "RDRAM_DEVICE_ID_REG";
    case 0xA3F00008: return "RDRAM_DELAY_REG";
    case 0xA3F0000C: return "RDRAM_MODE_REG";
    case 0xA3F00010: return "RDRAM_REF_INTERVAL_REG";
    case 0xA3F00014: return "RDRAM_REF_ROW_REG";
    case 0xA3F00018: return "RDRAM_RAS_INTERVAL_REG";
    case 0xA3F0001C: return "RDRAM_MIN_INTERVAL_REG";
    case 0xA3F00020: return "RDRAM_ADDR_SELECT_REG";
    case 0xA3F00024: return "RDRAM_DEVICE_MANUF_REG";

    case 0xA4040000: return "SP_MEM_ADDR_REG";
    case 0xA4040004: return "SP_DRAM_ADDR_REG";
    case 0xA4040008: return "SP_RD_LEN_REG";
    case 0xA404000C: return "SP_WR_LEN_REG";
    case 0xA4040010: return "SP_STATUS_REG";
    case 0xA4040014: return "SP_DMA_FULL_REG";
    case 0xA4040018: return "SP_DMA_BUSY_REG";
    case 0xA404001C: return "SP_SEMAPHORE_REG";

    case 0xA4080000: return "SP_PC";

    case 0xA4100000: return "DPC_START_REG";
    case 0xA4100004: return "DPC_END_REG";
    case 0xA4100008: return "DPC_CURRENT_REG";
    case 0xA410000C: return "DPC_STATUS_REG";
    case 0xA4100010: return "DPC_CLOCK_REG";
    case 0xA4100014: return "DPC_BUFBUSY_REG";
    case 0xA4100018: return "DPC_PIPEBUSY_REG";
    case 0xA410001C: return "DPC_TMEM_REG";

    case 0xA4300000: return "MI_INIT_MODE_REG/MI_MODE_REG";
    case 0xA4300004: return "MI_VERSION_REG/MI_NOOP_REG";
    case 0xA4300008: return "MI_INTR_REG";
    case 0xA430000C: return "MI_INTR_MASK_REG";

    case 0xA4400000: return "VI_STATUS_REG/VI_CONTROL_REG";
    case 0xA4400004: return "VI_ORIGIN_REG/VI_DRAM_ADDR_REG";
    case 0xA4400008: return "VI_WIDTH_REG/VI_H_WIDTH_REG";
    case 0xA440000C: return "VI_INTR_REG/VI_V_INTR_REG";
    case 0xA4400010: return "VI_CURRENT_REG/VI_V_CURRENT_LINE_REG";
    case 0xA4400014: return "VI_BURST_REG/VI_TIMING_REG";
    case 0xA4400018: return "VI_V_SYNC_REG";
    case 0xA440001C: return "VI_H_SYNC_REG";
    case 0xA4400020: return "VI_LEAP_REG/VI_H_SYNC_LEAP_REG";
    case 0xA4400024: return "VI_H_START_REG/VI_H_VIDEO_REG";
    case 0xA4400028: return "VI_V_START_REG/VI_V_VIDEO_REG";
    case 0xA440002C: return "VI_V_BURST_REG";
    case 0xA4400030: return "VI_X_SCALE_REG";
    case 0xA4400034: return "VI_Y_SCALE_REG";

    case 0xA4500000: return "AI_DRAM_ADDR_REG";
    case 0xA4500004: return "AI_LEN_REG";
    case 0xA4500008: return "AI_CONTROL_REG";
    case 0xA450000C: return "AI_STATUS_REG";
    case 0xA4500010: return "AI_DACRATE_REG";
    case 0xA4500014: return "AI_BITRATE_REG";

    case 0xA4600000: return "PI_DRAM_ADDR_REG";
    case 0xA4600004: return "PI_CART_ADDR_REG";
    case 0xA4600008: return "PI_RD_LEN_REG";
    case 0xA460000C: return "PI_WR_LEN_REG";
    case 0xA4600010: return "PI_STATUS_REG";
    case 0xA4600014: return "PI_BSD_DOM1_LAT_REG";
    case 0xA4600018: return "PI_BSD_DOM1_PWD_REG";
    case 0xA460001C: return "PI_BSD_DOM1_PGS_REG";
    case 0xA4600020: return "PI_BSD_DOM1_RLS_REG";
    case 0xA4600024: return "PI_BSD_DOM2_LAT_REG";
    case 0xA4600028: return "PI_BSD_DOM2_PWD_REG";
    case 0xA460002C: return "PI_BSD_DOM2_PGS_REG";
    case 0xA4600030: return "PI_BSD_DOM2_RLS_REG";

    case 0xA4700000: return "RI_MODE_REG";
    case 0xA4700004: return "RI_CONFIG_REG";
    case 0xA4700008: return "RI_CURRENT_LOAD_REG";
    case 0xA470000C: return "RI_SELECT_REG";
    case 0xA4700010: return "RI_REFRESH_REG/RI_COUNT_REG";
    case 0xA4700014: return "RI_LATENCY_REG";
    case 0xA4700018: return "RI_RERROR_REG";
    case 0xA470001C: return "RI_WERROR_REG";

    case 0xA4800000: return "SI_DRAM_ADDR_REG";
    case 0xA4800004: return "SI_PIF_ADDR_RD64B_REG";
    case 0xA4800010: return "SI_PIF_ADDR_WR64B_REG";
    case 0xA4800018: return "SI_STATUS_REG";

    case 0xA5000500: return "ASIC_DATA";
    case 0xA5000504: return "ASIC_MISC_REG";
    case 0xA5000508: return "ASIC_STATUS";
    case 0xA500050C: return "ASIC_CUR_TK";
    case 0xA5000510: return "ASIC_BM_STATUS";
    case 0xA5000514: return "ASIC_ERR_SECTOR";
    case 0xA5000518: return "ASIC_SEQ_STATUS";
    case 0xA500051C: return "ASIC_CUR_SECTOR";
    case 0xA5000520: return "ASIC_HARD_RESET";
    case 0xA5000524: return "ASIC_C1_S0";
    case 0xA5000528: return "ASIC_HOST_SECBYTE";
    case 0xA500052C: return "ASIC_C1_S2";
    case 0xA5000530: return "ASIC_SEC_BYTE";
    case 0xA5000534: return "ASIC_C1_S4";
    case 0xA5000538: return "ASIC_C1_S6";
    case 0xA500053C: return "ASIC_CUR_ADDR";
    case 0xA5000540: return "ASIC_ID_REG";
    case 0xA5000544: return "ASIC_TEST_REG";
    case 0xA5000548: return "ASIC_TEST_PIN_SEL";

    case 0xB0000004: return "Header: Clock rate";
    case 0xB0000008: return "Header: Game entry point";
    case 0xB000000C: return "Header: Release";
    case 0xB0000010: return "Header: CRC1";
    case 0xB0000014: return "Header: CRC2";
    }

    return nullptr;
}

static const char * GetCodeAddressNotes(uint32_t vAddr)
{
    switch (vAddr)
    {
    case 0x80000000: return "Exception: TLB Refill";
    case 0x80000080: return "Exception: XTLB Refill";
    case 0x80000100: return "Exception: Cache error (See A0000100)";
    case 0x80000180: return "Exception: General";

    case 0xA0000100: return "Exception: Cache error";

    case 0xBFC00000: return "Exception: Reset/NMI";
    case 0xBFC00200: return "Exception: TLB Refill (boot)";
    case 0xBFC00280: return "Exception: XTLB Refill (boot)";
    case 0xBFC00300: return "Exception: Cache error (boot)";
    case 0xBFC00380: return "Exception: General (boot)";
    }

    if (g_MMU == nullptr)
    {
        return nullptr;
    }

    uint8_t * rom = g_Rom->GetRomAddress();
    uint32_t gameEntryPoint = *(uint32_t *)&rom[0x08];

    if (vAddr == gameEntryPoint)
    {
        return "Game entry point";
    }

    return nullptr;
}
