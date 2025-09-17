/*
 * ocptelemetry.h
 *
 * Copyright (c) 2026 Western Digital Corporation or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef OCPTELEMETRY_H
#define OCPTELEMETRY_H

#include <map>

#include <smartmon/atacmds.h>
#include <smartmon/dev_interface.h>

#define OCP_EVENT_KEY(_CLASS, _EVENT_ID) (_CLASS << 16 | _EVENT_ID[1] << 8 | _EVENT_ID[0])

namespace smartmon {

// Format for ATA Current/Saved Device Internal Status Log Pages
// (log page 0x24 and 0x25).
// For log page 0x24, areas 1 to 3 define the location of the OCP
// telemetry information:
// - area1 contains both the OCP Telemetry Data Header
//   and the OCP Telemetry Data Area 1
// - area2 contains the OCP Telemetry Data Area 2
// - area3 contains the OCP Vendor Telemetry Data Area 3.
//   This format it vendor unique.
// For log page 0x25, areas 1 to 3 define the location of the OCP
// telemetry information:
// - area1 contains both the OCP Telemetry String Header
//   and the OCP Telemetry String Area
// - area2 may contain part of the OCP Telemetry String Area (the
//   string area may extend into area2)
// - area3 is reserved with a size of 0.

#pragma pack(1)
struct ata_device_internal_status {
  uint8_t  log_address;
  uint8_t  byte001_byte003[3];
  uint32_t organization_id;              // Vendor-specific or standard org ID
  uint16_t area1_last_log_page;
  uint16_t area2_last_log_page;
  uint16_t area3_last_log_page;
  uint8_t  bytes014_381[368];
  uint8_t  saved_data_available;
  uint8_t  saved_data_generation_number;
  uint8_t  reason_id[128];               // ASCII string
} SMARTMON_ATTR_PACKED;
#pragma pack()
SMARTMON_ASSERT_SIZEOF(ata_device_internal_status, 512);

// OCP Telemetry Data Header
// Section 7.2.10 in OCP Datacenter SAS-SATA Device Specification v1.5

#define OCP_GUID_LEN 16
#pragma pack(1)
struct ocp_telemetry_data_header {
  uint16_t major_version;
  uint16_t minor_version;
  uint8_t  byte004_byte007[4];
  uint8_t  timestamp[6];
  uint16_t timestamp_info;
  uint8_t  guid[OCP_GUID_LEN];       // F5DAF2C03433422EB616D11C79F6F9E3h
  uint16_t device_string_data_size;
  uint8_t  firmware_version[8];
  uint8_t  bytes042_109[68];
  uint64_t statistic1_start_dword;   // dword
  uint64_t statistic1_size_dword;
  uint64_t statistic2_start_dword;
  uint64_t statistic2_size_dword;
  uint64_t event1_FIFO_start_dword;
  uint64_t event1_FIFO_size_dword;
  uint64_t event2_FIFO_start_dword;
  uint64_t event2_FIFO_size_dword;
  uint8_t  bytes174_511[338];
} SMARTMON_ATTR_PACKED;
#pragma pack()
SMARTMON_ASSERT_SIZEOF(ocp_telemetry_data_header, 512);

#define OCP_FIFO_NAME_LEN 16

// OCP Telemetry Strings Header
// Section 7.2.13 in OCP Datacenter SAS-SATA Device Specification v1.5
#pragma pack(1)
struct ocp_telemetry_strings_header {
  uint8_t  log_page_version;
  uint8_t  byte001_byte015[15];
  uint8_t  guid[OCP_GUID_LEN];
  uint8_t  byte039_byte063[32];
  uint64_t statistics_id_string_table_start;
  uint64_t statistics_id_string_table_size;
  uint64_t event_string_table_start;
  uint64_t event_string_table_size;
  uint64_t vu_event_string_table_start;
  uint64_t vu_event_string_table_size;
  uint64_t ascii_table_start;
  uint64_t ascii_table_size;
  uint8_t  event_fifo_1_name[OCP_FIFO_NAME_LEN];
  uint8_t  event_fifo_2_name[OCP_FIFO_NAME_LEN];
  uint8_t  byte160_byte431[272];
} SMARTMON_ATTR_PACKED;
#pragma pack()
SMARTMON_ASSERT_SIZEOF(ocp_telemetry_strings_header, 432);

// OCP Statistics Identifier String Table Entry
// Section 7.2.14 in OCP Datacenter SAS-SATA Device Specification v1.5
#pragma pack(1)
struct ocp_statistic_id_string_table_entry {
  uint16_t vu_statistic_id;
  uint8_t  byte002_byte02;
  uint8_t  ascii_id_len;
  uint8_t  ascii_id_offset[8];
  uint8_t  byte012_byte015[4];
} SMARTMON_ATTR_PACKED;
#pragma pack()

// OCP Event Identifier and OCP Vendor Unique Event Identifier String Table
// Entries.
// Sections 7.2.15 and 7.2.16 in OCP Datacenter SAS-SATA Device Specification
// v1.5
#pragma pack(1)
struct ocp_event_id_string_table_entry {
  uint8_t dbg_class;
  uint8_t id[2];
  uint8_t ascii_id_len;
  uint8_t ascii_id_offset[8];
  uint8_t byte012_byte015[4];
} SMARTMON_ATTR_PACKED;
#pragma pack()

// OCP Reason Identifier
// Section 7.2.2 in OCP Datacenter SAS-SATA Device Specification v1.5
#pragma pack(1)
struct ocp_reason_id {
  uint8_t  error_id[64];
  uint8_t  file_id[8];
  uint16_t line_number;
  uint8_t  valid_flags;
  uint8_t  byte095_byte075[31];
  uint8_t  vu_reason_extension[32];
} SMARTMON_ATTR_PACKED;
#pragma pack()

enum ocp_reason_id_valid_flags {
  OCP_REASON_ID_LINE_NUMBER = 1,
  OCP_REASON_ID_FILE_ID     = 1 << 1,
  OCP_REASON_ID_ERROR_ID    = 1 << 2,
  OCP_REASON_ID_VU_EXT      = 1 << 3,
};

// OCP Statistic Descriptor
// Section 7.2.3 in OCP Datacenter SAS-SATA Device Specification v1.5

#pragma pack(1)
struct ocp_statistic_header {
  uint16_t statistics_id;
  uint8_t  statistics_info[3];
  uint8_t  byte005_byte005;
  uint16_t statistic_data_size; // Number of dwords
} SMARTMON_ATTR_PACKED;
#pragma pack()
SMARTMON_ASSERT_SIZEOF(ocp_statistic_header, 8);

#pragma pack(1)
struct ocp_statistic_descriptor {
  struct ocp_statistic_header h;
  union {
    struct {
      uint8_t data[0];
    } single;
    struct {
      uint8_t  element_size;
      uint8_t  byte010_byte010;
      uint16_t number_of_elements; // 0-indexed
      uint8_t  data[];
    } array;
    union {
      uint8_t data[0];
    } custom;
  };
} SMARTMON_ATTR_PACKED;
#pragma pack()

enum ocp_stat_type
{
  OCP_STAT_TYPE_SINGLE = 0x0,
  OCP_STAT_TYPE_ARRAY  = 0x1,
  OCP_STAT_TYPE_CUSTOM = 0x2
};

enum ocp_data_type
{
  OCP_DATA_TYPE_NA    = 0x0,
  OCP_DATA_TYPE_INT   = 0x1,
  OCP_DATA_TYPE_UINT  = 0x2,
  OCP_DATA_TYPE_FP    = 0x3,
  OCP_DATA_TYPE_ASCII = 0x4
};

enum ocp_unit_type
{
  OCP_UNIT_TYPE_NA         = 0x00,
  OCP_UNIT_TYPE_MSEC       = 0x01,
  OCP_UNIT_TYPE_SEC        = 0x02,
  OCP_UNIT_TYPE_HOUR       = 0x03,
  OCP_UNIT_TYPE_DAY        = 0x04,
  OCP_UNIT_TYPE_MB         = 0x05,
  OCP_UNIT_TYPE_GB         = 0x06,
  OCP_UNIT_TYPE_TB         = 0x07,
  OCP_UNIT_TYPE_PB         = 0x08,
  OCP_UNIT_TYPE_C          = 0x09,
  OCP_UNIT_TYPE_K          = 0x0a,
  OCP_UNIT_TYPE_F          = 0x0b,
  OCP_UNIT_TYPE_MV         = 0x0c,
  OCP_UNIT_TYPE_MA         = 0x0d,
  OCP_UNIT_TYPE_OHM        = 0x0e,
  OCP_UNIT_TYPE_RPM        = 0x0f,
  OCP_UNIT_TYPE_MICROMETER = 0x10,
  OCP_UNIT_TYPE_NANOMETER  = 0x11,
  OCP_UNIT_TYPE_ANGSTROMS  = 0x12,
  OCP_UNIT_TYPE_MAX        = OCP_UNIT_TYPE_ANGSTROMS,
};

static const char* const ocp_stat_data_unit_str[] = { "N/A", "ms", "s", "h", "d",
  "MB", "GB", "TB", "PB", "C", "K", "F", "mV", "mA", "Ohm", "RPM",
  "micrometer", "nanometer", "angstroms" };

enum ocp_behavior_type
{
  OCP_BEHV_TYPE_NA      = 0x0,
  OCP_BEHV_TYPE_NONE    = 0x1,
  OCP_BEHV_TYPE_R_PC    = 0x2,
  OCP_BEHV_TYPE_SC_R    = 0x3,
  OCP_BEHV_TYPE_SC_R_PC = 0x4,
  OCP_BEHV_TYPE_SC      = 0x5,
  OCP_BEHV_TYPE_R       = 0x6,
};

// Statistics ID: 0002h
#pragma pack(1)
struct ocp_ata_log_stat_desc {
  struct ocp_statistic_header h;
  uint8_t log_addr;
  uint8_t log_page_count;
  uint16_t initial_log_page;
  uint8_t log_page_data[];
};
#pragma pack()

// Statistics ID: 0003h
#pragma pack(1)
struct ocp_scsi_log_stat_desc {
  struct ocp_statistic_header h;
  uint8_t log_page;
  uint8_t log_subpage;
  uint8_t reserved[2];
  uint8_t log_page_data[];
};
#pragma pack()

// Statistics ID: 6006h
#pragma pack(1)
struct ocp_hdd_spinup_stat_desc {
  struct ocp_statistic_header h;
  uint16_t spinup_max;
  uint16_t spinup_min;
  uint16_t spinup_hist[10];
};
#pragma pack()

struct ocp_stat_str_def {
  uint16_t id;
  char desc[128];
};

static const struct ocp_stat_str_def ocp_builtin_stat_str[] = {
  {0x0002, "ATA Log"},
  {0x0003, "SCSI Log Page"},

  {0x2001, "Reallocated Block Count"},
  {0x2002, "Pending Defects Count"},
  {0x2003, "Power-on Hours Count"},
  {0x2004, "Power-on Cycle Count"},
  {0x2005, "Spare Blocks Used"},
  {0x2006, "Spare Blocks Remaining"},
  {0x2007, "Unexpected Power Loss Count"},
  {0x2008, "Current Temperature"},
  {0x2009, "Minimum Lifetime Temperature"},
  {0x200a, "Maximum Lifetime Temperature"},
  {0x200b, "Uncorrectable Read Error Count"},
  {0x200c, "Background Uncorrectable Read Error Count"},
  {0x200d, "Interface CRC Error Count"},
  {0x200e, "Volatile Memory Backup Source Failure"},
  {0x200f, "Read Only Mode"},
  {0x2010, "Host Write Commands"},
  {0x2011, "Host Read Commands"},
  {0x2012, "Logical Blocks Read"},
  {0x2013, "Logical Blocks Written"},
  {0x2014, "Total Media Writes"},
  {0x2015, "Total Media Reads"},
  {0x2016, "Soft ECC Error Count"},
  {0x2017, "Host Trim/Unmap Commands"},
  {0x2018, "End-to-end Detected Errors"},
  {0x2019, "End-to-end Corrected Errors"},
  {0x201a, "Unaligned I/O count"},
  {0x201b, "Security version number"},
  {0x201c, "Thermal Throttling Status"},
  {0x201d, "Thermal Throttling Count"},
  {0x201e, "DSS Specification Version"},
  {0x201f, "Incomplete Shutdown Count"},
  {0x2020, "Percent Free Blocks"},
  {0x2021, "Lowest Permitted Firmware Revision"},
  {0x2022, "Maximum Peak Power Capability"},
  {0x2023, "Current Maximum Average Power"},
  {0x2024, "Lifetime Power Consumed"},
  {0x2025, "Power Changes"},
  {0x2026, "Phy Reinitialization Count"},
  {0x2027, "Secondary Phy Reinitialization Count"},
  {0x2028, "Command Timeouts"},
  {0x2029, "Hardware Revision"},
  {0x202a, "Firmware Revision"},

  {0x4001, "Raw Capacity"},
  {0x4002, "User Capacity"},
  {0x4003, "Erase Count"},
  {0x4004, "Erase Fail Count"},
  {0x4005, "Maximum Erase Count"},
  {0x4006, "Average Erase Count"},
  {0x4007, "Program Fail Count"},
  {0x4008, "XOR Recovery Count"},
  {0x4009, "Percent Device Life Remaining"},
  {0x400a, "Lifetime Erase Count"},
  {0x400b, "Bad User NAND Blocks"},
  {0x400c, "Bad System NAND Blocks"},
  {0x400d, "Minimum Erase Count"},
  {0x400e, "Power Loss Protection Start Count"},
  {0x400f, "System Data Percent Used"},
  {0x4010, "Power Loss Protection Health"},
  {0x4011, "Endurance Estimate"},
  {0x4012, "Percent User Spare Available"},
  {0x4013, "Percent System Spare Available"},
  {0x4014, "Total Media Dies"},
  {0x4015, "Media Die Failure Tolerance"},
  {0x4016, "Media Dies Offline"},
  {0x4017, "System Area Program Fail Count"},
  {0x4018, "System Area Program Fail Percentage Remaining"},
  {0x4019, "System Area Uncorrectable Read Error Count"},
  {0x401a, "System Area Uncorrectable Read Percentage Remaining"},
  {0x401b, "System Area Erase Fail Count"},
  {0x401c, "System Area Erase Fail Percentage Remaining"},

  {0x6001, "Start/Stop Count"},
  {0x6002, "Load Cycle Count"},
  {0x6003, "Shock Overlimit Count"},
  {0x6004, "Head Flying Hours"},
  {0x6005, "Free Fall Events Count"},
  {0x6006, "Spinup Times"},
};

#define OCP_BUILTIN_STAT_STR_LEN  (sizeof ocp_builtin_stat_str / sizeof ocp_builtin_stat_str[0])

enum ocp_event_class {
  OCP_EVENT_CLASS_TIMESTAMP       = 0x01,
  OCP_EVENT_CLASS_RESET           = 0x04,
  OCP_EVENT_CLASS_BOOT_SEQ        = 0x05,
  OCP_EVENT_CLASS_FIRMWARE_ASSERT = 0x06,
  OCP_EVENT_CLASS_TEMPERATURE     = 0x07,
  OCP_EVENT_CLASS_MEDIA           = 0x08,
  OCP_EVENT_CLASS_MEDIA_WEAR      = 0x09,
  OCP_EVENT_CLASS_STATISTIC_SNAP  = 0x0A,
  OCP_EVENT_CLASS_VIRTUAL_FIFO    = 0x0B,
  OCP_EVENT_CLASS_SATA_PHY_LINK   = 0x0C,
  OCP_EVENT_CLASS_SATA_TRANSPORT  = 0x0D,
  OCP_EVENT_CLASS_SAS_PHY_LINK    = 0x0E,
  OCP_EVENT_CLASS_SAS_TRANSPORT   = 0x0F,
};

enum ocp_timestamp_event_id {
  OCP_TIMESTAMP_EVENT_HOST_INITIATED     = 0x0,
  OCP_TIMESTAMP_EVENT_FIRMWARE_INITIATED = 0x1,
  OCP_TIMESTAMP_EVENT_OBSOLETE           = 0x2,
  OCP_TIMESTAMP_EVENT_MAX                = OCP_TIMESTAMP_EVENT_OBSOLETE
};

static const char * const ocp_timestamp_event_id_str[] =
{
  "Host Initiated Timestamp",
  "Firmware Initiated Timestamp",
  "Obsolete ID (0x02)",
};

enum ocp_reset_event_id {
  OCP_RESET_EVENT_MAIN_POWER_CYCLE          = 0x0,
  OCP_RESET_EVENT_SATA_SRST                 = 0x1,
  OCP_RESET_EVENT_SATA_COMRESET             = 0x2,
  OCP_RESET_EVENT_SAS_HARD_RESET            = 0x3,
  OCP_RESET_EVENT_SAS_COMINIT               = 0x4,
  OCP_RESET_EVENT_SAS_DWORD_SYNC_LOSS       = 0x5,
  OCP_RESET_EVENT_SAS_SPL_PACKET_SYNC_LOSS  = 0x6,
  OCP_RESET_EVENT_SAS_RECV_IDENTIFY_TIMEOUT = 0x7,
  OCP_RESET_EVENT_SAS_HOT_PLUG_TIMEOUT      = 0x8,
  OCP_RESET_EVENT_MAX                       = OCP_RESET_EVENT_SAS_HOT_PLUG_TIMEOUT,
};

static const char * const ocp_reset_event_id_str[] =
{
  "Main Power Cycle",
  "SATA - SRST",
  "SATA - COMRESET",
  "SAS - Hard Reset",
  "SAS - COMINIT",
  "SAS - DWORD Synchronization Loss",
  "SAS - SPL Packet Synchronization Loss",
  "SAS - Receive Identify Timeout Timer Expired",
  "SAS - Hot-plug Timeout",
};


enum ocp_boot_seq_event_id {
  OCP_BOOT_SEQ_EVENT_SSD_MAIN_FW_BOOT_COMPLETE  = 0x0,
  OCP_BOOT_SEQ_EVENT_FTL_LOAD_FROM_NVM_COMPLETE = 0x1,
  OCP_BOOT_SEQ_EVENT_FTL_REBUILD_STARTED        = 0x2,
  OCP_BOOT_SEQ_EVENT_FTL_READY                  = 0x3,
  OCP_BOOT_SEQ_EVENT_HDD_MAIN_FW_BOOT_COMPLETE  = 0x100,
  OCP_BOOT_SEQ_EVENT_SPIN_UP_START              = 0x101,
  OCP_BOOT_SEQ_EVENT_SPIN_UP_COMPLETE           = 0x102,
  OCP_BOOT_SEQ_EVENT_DEVICE_READY               = 0x103,
};

static const char * const ocp_ssd_boot_seq_event_id_str[] =
{
  "Main Firmware Boot Complete",
  "FTL Load From NVM Complete",
  "FTL Rebuild Started",
  "FTL Ready"
};

static const char * const ocp_hdd_boot_seq_event_id_str[] =
{
  "Main Firmware Boot Complete",
  "Spin-up Start",
  "Spin-up Complete",
  "Device Ready"
};

enum ocp_firmware_assert_event_id {
  OCP_FW_ASSERT_EVENT_PROTOCOL_CODE     = 0x0,
  OCP_FW_ASSERT_EVENT_MEDIA_CODE        = 0x1,
  OCP_FW_ASSERT_EVENT_SECURITY_CODE     = 0x2,
  OCP_FW_ASSERT_EVENT_BG_SERVICE_CODE   = 0x3,
  OCP_FW_ASSERT_EVENT_FTL_REBUILD_FAIL  = 0x4,
  OCP_FW_ASSERT_EVENT_FTL_DATA_MISMATCH = 0x5,
  OCP_FW_ASSERT_EVENT_BAD_BLOCK_RELOC   = 0x6,
  OCP_FW_ASSERT_EVENT_OTHER_CODE        = 0x7,
  OCP_FW_ASSERT_EVENT_MAX = OCP_FW_ASSERT_EVENT_OTHER_CODE
};

static const char * const ocp_fw_assert_event_id_str[] =
{
  "Assert in SAS, SCSI, SATA or ATA Processing Code",
  "Assert in Media Code",
  "Assert in Security Code",
  "Assert in Background Services Code",
  "FTL Rebuild Failed",
  "FTL Data Mismatch",
  "Assert in Bad Block Relocation Code",
  "Assert in Other Code"
};

enum ocp_temperature_event_id {
  OCP_TEMPERATURE_EVENT_THROTTLE_CEASED    = 0x0,
  OCP_TEMPERATURE_EVENT_THROTTLE_INCREASED = 0x1,
  OCP_TEMPERATURE_EVENT_THERMAL_SHUTDOWN   = 0x2,
  OCP_TEMPERATURE_EVENT_MAX = OCP_TEMPERATURE_EVENT_THERMAL_SHUTDOWN,
};

static const char * const ocp_temperature_event_id_str[] =
{
  "Temperature decrease ceased thermal throttling",
  "Temperature increase commenced thermal throttling",
  "Temperature increase caused thermal shutdown",
};

enum ocp_media_event_id {
  OCP_MEDIA_EVENT_XOR_RECOVERY            = 0x0,
  OCP_MEDIA_EVENT_UNCORRECTABLE_ERROR     = 0x1,
  OCP_MEDIA_EVENT_BAD_BLOCK_PROGRAM_ERROR = 0x2,
  OCP_MEDIA_EVENT_BAD_BLOCK_ERASE_ERROR   = 0x3,
  OCP_MEDIA_EVENT_BAD_BLOCK_READ_ERROR    = 0x4,
  OCP_MEDIA_EVENT_MEDIA_PLANE_FAILURE     = 0x5,
  OCP_MEDIA_EVENT_MEDIA_DIE_FAILURE       = 0x6,
  OCP_MEDIA_EVENT_HDD_FAILURE             = 0x7,
  OCP_MEDIA_EVENT_MAX = OCP_MEDIA_EVENT_HDD_FAILURE,
};

static const char * const ocp_media_event_id_str[] =
{
  "XOR (or equivalent) Recovery Invoked",
  "Uncorrectable Media Error",
  "Block Marked Bad Due To SSD Media Program Error",
  "Block Marked Bad Due To SSD Media Erase Error",
  "Block Marked Bad Due To Read Error",
  "SSD Media Plane Failure",
  "SSD Media Die Failure",
  "HDD Head or Surface Failure",
};

enum ocp_media_wear_event_id {
  OCP_MEDIA_WEAR_EVENT_MEDIA_WEAR = 0x0,
  OCP_MEDIA_WEAR_EVENT_MAX = OCP_MEDIA_WEAR_EVENT_MEDIA_WEAR,
};

static const char * const ocp_media_wear_event_id_str[] =
{
  "Media Wear",
};

enum ocp_virtual_fifo_event_id {
  OCP_VIRTUAL_FIFO_EVENT_START = 0x0,
  OCP_VIRTUAL_FIFO_EVENT_END   = 0x1,
  OCP_VIRTUAL_FIFO_EVENT_MAX = OCP_VIRTUAL_FIFO_EVENT_END,
};

static const char * const ocp_virtual_fifo_event_id_str[] =
{
  "Virtual FIFO Start",
  "Virtual FIFO End",
};

enum ocp_sata_phy_link_event_id {
  OCP_SATA_PHY_LINK_EVENT_RESET_COMRESET  = 0x00,
  OCP_SATA_PHY_LINK_EVENT_RESET_NO_SIGNAL = 0x01,
  OCP_SATA_PHY_LINK_EVENT_DEV_DROP_LINK   = 0x02,
  OCP_SATA_PHY_LINK_EVENT_READY_GEN_3     = 0x03,
  OCP_SATA_PHY_LINK_EVENT_READY_GEN_2     = 0x04,
  OCP_SATA_PHY_LINK_EVENT_READY_GEN_1     = 0x05,
  OCP_SATA_PHY_LINK_EVENT_PARTIAL_ENTERED = 0x06,
  OCP_SATA_PHY_LINK_EVENT_PARTIAL_EXITED  = 0x07,
  OCP_SATA_PHY_LINK_EVENT_REDUCE_SPEED    = 0x08,
  OCP_SATA_PHY_LINK_EVENT_ERROR           = 0x09,
  OCP_SATA_PHY_LINK_EVENT_TX_HOLD         = 0x0A,
  OCP_SATA_PHY_LINK_EVENT_RX_HOLD         = 0x0B,
  OCP_SATA_PHY_LINK_EVENT_PMNAK_RX        = 0x0C,
  OCP_SATA_PHY_LINK_EVENT_PMNAK_TX        = 0x0D,
  OCP_SATA_PHY_LINK_EVENT_R_ERR_RX        = 0x0E,
  OCP_SATA_PHY_LINK_EVENT_R_ERR_TX        = 0x0F,
  OCP_SATA_PHY_LINK_EVENT_TX_DEV_BITS_ERR = 0x10,
  OCP_SATA_PHY_LINK_EVENT_MAX = OCP_SATA_PHY_LINK_EVENT_TX_DEV_BITS_ERR
};

static const char * const ocp_sata_phy_link_event_id_str[] =
{
  "DR_Reset Entered due to Unexpected COMRESET",
  "DR_Reset Entered due to Phy Signal Not Detected",
  "Device Dropped Link while Host Link is Up",
  "DR_Ready entered at Gen 3",
  "DR_Ready entered at Gen 2",
  "DR_Ready entered at Gen 1",
  "DR_Partial Entered",
  "DR_Partial Exited",
  "DR_Reduce_Speed Entered",
  "DR_Error Entered",
  "Transmitting HOLD",
  "Receiving HOLD",
  "PMNAK Received",
  "PMNAK Transmitted",
  "R_ERR Received",
  "R_ERR Transmitted",
  "Set Device Bits Transmitted with Error Bit Set",
};

enum ocp_sata_transport_event_id {
  OCP_SATA_TRANSPORT_EVENT_NON_DATA_FIS_RX  = 0x00,
  OCP_SATA_TRANSPORT_EVENT_NON_DATA_FIS_TX  = 0x01,
  OCP_SATA_TRANSPORT_EVENT_DATA_FIS_RX      = 0x02,
  OCP_SATA_TRANSPORT_EVENT_DATA_FIS_TX      = 0x03,
  OCP_SATA_TRANSPORT_EVENT_MAX = OCP_SATA_TRANSPORT_EVENT_DATA_FIS_TX,
};

static const char * const ocp_sata_transport_event_id_str[] =
{
  "Non-Data FIS Received",
  "Non-Data FIS Transmitted",
  "Data FIS Received",
  "Data FIS Transmitted",
};

enum ocp_sas_phy_link_event_id {
  OCP_SAS_PHY_LINK_EVENT_LINK_UP_1_5_Gbps      = 0x00,
  OCP_SAS_PHY_LINK_EVENT_LINK_UP_3_0_Gbps      = 0x01,
  OCP_SAS_PHY_LINK_EVENT_LINK_UP_6_0_Gbps      = 0x02,
  OCP_SAS_PHY_LINK_EVENT_LINK_UP_12_0_Gbps     = 0x03,
  OCP_SAS_PHY_LINK_EVENT_LINK_UP_22_5_Gbps     = 0x04,
  OCP_SAS_PHY_LINK_EVENT_IDENTIFY_RX           = 0x05,
  OCP_SAS_PHY_LINK_EVENT_HARD_RESET_RX         = 0x06,
  OCP_SAS_PHY_LINK_EVENT_LINK_LOSS             = 0x07,
  OCP_SAS_PHY_LINK_EVENT_DWORD_SYNCH_LOSS      = 0x08,
  OCP_SAS_PHY_LINK_EVENT_SPL_PACKET_SYNCH_LOSS = 0x09,
  OCP_SAS_PHY_LINK_EVENT_IDENTIFY_RX_TIMEOUT   = 0x0A,
  OCP_SAS_PHY_LINK_EVENT_BREAK_RX              = 0x0B,
  OCP_SAS_PHY_LINK_EVENT_BREAK_REPLY_RX        = 0x0C,
  OCP_SAS_PHY_LINK_EVENT_MAX = OCP_SAS_PHY_LINK_EVENT_BREAK_REPLY_RX,
};

static const char * const ocp_sas_phy_link_event_id_str[] =
{
  "Link Up - 1.5 Gbps",
  "Link Up - 3.0 Gbps",
  "Link Up - 6.0 Gbps",
  "Link Up - 12.0 Gbps",
  "Link Up - 22.5 Gbps",
  "Identify Received (Data)",
  "HARD_RESET Received",
  "Link Loss",
  "DWORD Synchronization Loss",
  "SPL Packet Synchronization Loss",
  "Identify Receive TImeout",
  "BREAK Received",
  "BREAK_REPLY Received",
};

enum ocp_sas_transport_event_id {
  OCP_SAS_TRANSPORT_EVENT_DATA_FRAME_RX      = 0x00,
  OCP_SAS_TRANSPORT_EVENT_DATA_FRAME_TX      = 0x01,
  OCP_SAS_TRANSPORT_EVENT_XFER_RDY_FRAME_RX  = 0x02,
  OCP_SAS_TRANSPORT_EVENT_COMMAND_FRAME_RX   = 0x03,
  OCP_SAS_TRANSPORT_EVENT_RESPONSE_FRAME_TX  = 0x04,
  OCP_SAS_TRANSPORT_EVENT_TASK_FRAME_RX      = 0x05,
  OCP_SAS_TRANSPORT_EVENT_SSP_FRAME_RX       = 0x06,
  OCP_SAS_TRANSPORT_EVENT_SSP_FRAME_TX       = 0x07,
  OCP_SAS_TRANSPORT_EVENT_NAK_RX             = 0x08,
  OCP_SAS_TRANSPORT_EVENT_MAX = OCP_SAS_TRANSPORT_EVENT_NAK_RX,
};

static const char * const ocp_sas_transport_event_id_str[] =
{
  "DATA Frame Received",
  "DATA Frame Sent",
  "XFER_RDY Frame Sent",
  "COMMAND Frame Received",
  "RESPONSE Frame Sent",
  "TASK Frame Received",
  "SSP Frame Received",
  "SSP Frame Sent",
  "NAK Received",
};

// OCP Event Descriptor
// Section 7.2.8.1 in OCP Datacenter SAS-SATA Device Specification v1.5
#pragma pack(1)
struct ocp_event_descriptor {
  uint8_t debug_event_class_type;
  uint8_t event_id[2];
  uint8_t data_size;
  uint8_t data[];
} SMARTMON_ATTR_PACKED;
#pragma pack()
SMARTMON_ASSERT_SIZEOF(ocp_event_descriptor, 4);

#pragma pack(1)
struct ocp_event_vu {
  uint8_t id[2];
  uint8_t data[];
} SMARTMON_ATTR_PACKED;
#pragma pack()
SMARTMON_ASSERT_SIZEOF(ocp_event_vu, 2);

#pragma pack(1)
struct ocp_event_timestamp {
  uint8_t timestamp[8];
} SMARTMON_ATTR_PACKED;
#pragma pack()
SMARTMON_ASSERT_SIZEOF(ocp_event_timestamp, 8);

#pragma pack(1)
struct ocp_event_media_wear {
  uint32_t host_tb_written;
  uint32_t media_tb_written;
  uint32_t ssd_media_tb_erased;
} SMARTMON_ATTR_PACKED;
#pragma pack()
SMARTMON_ASSERT_SIZEOF(ocp_event_media_wear, 12);

// For the marker array, bits 10:0 are virtual fifo number where bits 7:0 are
// in marker[0]. Bits 13:11 designate the data area of the FIFO.
#pragma pack(1)
struct ocp_event_virtual_fifo {
  uint8_t marker[2];
  uint16_t reserved;
} SMARTMON_ATTR_PACKED;
#pragma pack()
SMARTMON_ASSERT_SIZEOF(ocp_event_virtual_fifo, 4);

#pragma pack(1)
struct ocp_event_class_0Dh {
  uint32_t fis[7];
} SMARTMON_ATTR_PACKED;
#pragma pack()
SMARTMON_ASSERT_SIZEOF(ocp_event_class_0Dh, 28);

typedef struct ocp_string_def {
  std::map<uint16_t, struct ocp_statistic_id_string_table_entry> stat_id_string_map;
  std::map<uint32_t, struct ocp_event_id_string_table_entry> event_string_map;
  char *ocp_string_ascii_table;
  char event_fifo_1_name[OCP_FIFO_NAME_LEN + 1];
  char event_fifo_2_name[OCP_FIFO_NAME_LEN + 1];
} ocp_string_def;

bool read_ata_ocp_telemetry_string_state(ata_device * device, unsigned nsectors,
                                         struct ata_device_internal_status *internal_status,
                                         struct ocp_telemetry_strings_header *ocp_strings_header,
                                         ocp_string_def *string_def);

bool read_ata_ocp_telemetry_statistics(ata_device * device, unsigned nsectors,
                                       struct ata_device_internal_status *internal_status,
                                       struct ocp_telemetry_data_header *ocp_data_header,
                                       char **stats);
} // namespace smartmon

#endif // OCPTELEMETRY_H
