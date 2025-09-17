/*
 * ocptelemetry.cpp
 *
 * Copyright (c) 2026 Western Digital Corporation or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#include <smartmon/ocptelemetry.h>

#include <cmath>

#define MIN(_A, _B) ((_A) < (_B) ? (_A) : (_B))

namespace smartmon {

static void ocp_process_stat_id_strings(uint8_t *data, size_t data_len, ocp_string_def *string_def)
{
  while (data_len > 0) {
    struct ocp_statistic_id_string_table_entry *entry =
      (struct ocp_statistic_id_string_table_entry *)data;
    string_def->stat_id_string_map[entry->vu_statistic_id] = *entry;
    data_len -= sizeof *entry;
    data += sizeof *entry;
  }
}

static void ocp_process_event_strings(uint8_t *data, size_t data_len, ocp_string_def *string_def)
{
  while (data_len > 0) {
    uint32_t key;
    struct ocp_event_id_string_table_entry *entry =
      (struct ocp_event_id_string_table_entry *)data;
    key = OCP_EVENT_KEY(entry->dbg_class, entry->id);
    string_def->event_string_map[key] = *entry;
    data_len -= sizeof *entry;
    data += sizeof *entry;
  }
}

static bool validate_ocp_telemetry_data_header(struct ocp_telemetry_data_header *header,
                                               unsigned nsectors)
{
  size_t max_dword = sizeof *header;

  if (header->statistic2_size_dword > 0 &&
      header->statistic2_start_dword + header->statistic2_size_dword > max_dword) {
    max_dword = header->statistic2_start_dword + header->statistic2_size_dword;
  } else if (header->statistic1_size_dword > 0 &&
             header->statistic1_start_dword + header->statistic1_size_dword > max_dword) {
    max_dword = header->statistic1_start_dword + header->statistic1_size_dword;
  }

  if (header->event2_FIFO_size_dword > 0 &&
      header->event2_FIFO_start_dword + header->event2_FIFO_size_dword > max_dword) {
    max_dword = header->event2_FIFO_start_dword + header->event2_FIFO_size_dword;
  } else if (header->event1_FIFO_size_dword > 0 &&
             header->event1_FIFO_start_dword + header->event1_FIFO_size_dword > max_dword) {
    max_dword = header->event1_FIFO_start_dword + header->event1_FIFO_size_dword;
  }

  if (nsectors < (size_t)ceil(max_dword/128) + 1) {
    return false;
  }

  return true;
}

static bool read_ocp_telemetry_data_range_sata(ata_device * device,
                                               size_t start_dword, size_t size_dword,
                                               char *dest)
{
  uint32_t page[128];
  size_t page_idx = ceil(start_dword / 128) + 1;
  size_t page_offset = start_dword - (page_idx - 1) * 128;
  size_t dwords_to_read = size_dword;

  while (dwords_to_read > 0) {
    size_t dwords_in_page = dwords_to_read < (128 - page_offset) ? dwords_to_read : (128 - page_offset);

    if (!ataReadLogExt(device, 0x24, 0, page_idx, page, 1)) {
      return false;
    }

    memcpy(dest, (char *)(page + page_offset), dwords_in_page << 2);
    dwords_to_read -= dwords_in_page;
    dest += dwords_in_page << 2;
    ++page_idx;
    page_offset = 0;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////
// Saved Device Internal Status log (Log 0x25)

bool read_ata_ocp_telemetry_string_state(ata_device * device, unsigned nsectors,
                                         struct ata_device_internal_status *internal_status,
                                         struct ocp_telemetry_strings_header *ocp_strings_header,
                                         ocp_string_def *string_def)
{
  uint32_t log_page[128];

  if (!ataReadLogExt(device, 0x25, 0, 0, internal_status, 1)) {
    return false;
  }

  if (internal_status->area1_last_log_page == 0) {
    return false;
  }

  // The telemetry strings header is located on log page 1, starting
  // at byte 0, and occupies the first 432 bytes. The remainder of the
  // log page may contain string table entries.
  if (!ataReadLogExt(device, 0x25, 0, 1, (void *)log_page, 1)) {
    return false;
  }
  *ocp_strings_header = *((struct ocp_telemetry_strings_header *)log_page);

  // Any string data will immediately follow the header as the spec
  // states that there can be no gaps between the tables. Also, the
  // spec dictates that the stat id string table starts immediately
  // following the header.
  size_t dwords_to_read = ocp_strings_header->statistics_id_string_table_size +
    ocp_strings_header->event_string_table_size +
    ocp_strings_header->vu_event_string_table_size +
    ocp_strings_header->ascii_table_size;

  if (nsectors < (size_t)ceil((dwords_to_read + (sizeof *ocp_strings_header >> 2))/128) + 1) {
    return false;
  }

  if (ocp_strings_header->ascii_table_size > 0) {
    string_def->ocp_string_ascii_table = (char *)malloc(ocp_strings_header->ascii_table_size << 2);
    if (string_def->ocp_string_ascii_table == NULL) {
      return false;
    }
  }

#define OCP_DO_STRING_TABLE_PROCESSING(_TABLE_NAME, _POS)       \
   ocp_strings_header->_TABLE_NAME##_table_size > 0 &&           \
   (_POS) >= ocp_strings_header->_TABLE_NAME##_table_start && \
   (_POS) < ocp_strings_header->_TABLE_NAME##_table_start + \
               ocp_strings_header->_TABLE_NAME##_table_size
#define OCP_STRING_TABLE_BYTES_IN_PAGE(_TABLE_NAME, _LEN)     \
    (dword_pos + (_LEN) < ocp_strings_header->_TABLE_NAME##_table_start + \
                    ocp_strings_header->_TABLE_NAME##_table_size  ? \
     (_LEN) : ocp_strings_header->_TABLE_NAME##_table_start +  \
     ocp_strings_header->_TABLE_NAME##_table_size - dword_pos)

  size_t dword_pos = sizeof *ocp_strings_header >> 2;
  size_t dwords_in_page = 128 - (sizeof *ocp_strings_header >> 2);
  unsigned int log_page_idx = 1;
  uint8_t *log_page_pos = (uint8_t *)log_page + sizeof *ocp_strings_header;
  size_t dwords_consumed = 0;
  size_t ascii_offset = 0;

  while (dwords_to_read > 0) {
    if (OCP_DO_STRING_TABLE_PROCESSING(statistics_id_string, dword_pos)) {
      dwords_consumed = OCP_STRING_TABLE_BYTES_IN_PAGE(statistics_id_string, dwords_in_page);
      ocp_process_stat_id_strings(log_page_pos, dwords_consumed << 2, string_def);
    } else if (OCP_DO_STRING_TABLE_PROCESSING(event_string, dword_pos)) {
      dwords_consumed = OCP_STRING_TABLE_BYTES_IN_PAGE(event_string, dwords_in_page);
      ocp_process_event_strings(log_page_pos, dwords_consumed << 2, string_def);
    } else if (OCP_DO_STRING_TABLE_PROCESSING(vu_event_string, dword_pos)) {
      dwords_consumed = OCP_STRING_TABLE_BYTES_IN_PAGE(vu_event_string, dwords_in_page);
      ocp_process_event_strings(log_page_pos, dwords_consumed << 2, string_def);
    } else if (OCP_DO_STRING_TABLE_PROCESSING(ascii, dword_pos)) {
      dwords_consumed = OCP_STRING_TABLE_BYTES_IN_PAGE(ascii, dwords_in_page);
      memcpy(&string_def->ocp_string_ascii_table[ascii_offset], log_page_pos,
             dwords_consumed << 2);
      ascii_offset += dwords_consumed << 2;
    } else {
      printf("Ran out of space before all dwords were read\n");
      break;
    }
    dword_pos += dwords_consumed;
    log_page_pos += dwords_consumed << 2;
    dwords_to_read -= dwords_consumed;
    dwords_in_page -= dwords_consumed;
    if (dwords_in_page > 0)
      continue;

    if (dwords_to_read > 0) {
      dwords_in_page = MIN(dwords_to_read, 128);
      ++log_page_idx;
      log_page_pos = (uint8_t *)log_page;
      if (!ataReadLogExt(device, 0x25, 0, log_page_idx, (void *)log_page, 1)) {
        return false;
      }
    }
  }

  return true;
}

bool read_ata_ocp_telemetry_statistics(ata_device * device, unsigned nsectors,
                                       struct ata_device_internal_status *internal_status,
                                       struct ocp_telemetry_data_header *ocp_data_header,
                                       char **stats)
{
  if (!ataReadLogExt(device, 0x24, 0, 0, internal_status, 1)) {
    return false;
  }

  // SATA Layout for OCP Telemetry
  // - data area 1 (starting on page 1) contains the OCP Telemetry Data Header
  //   and OCP Telemetry data area 1. Byte 0 is the start of OCP Telemetry
  //   Data Header.
  // - data area 2 (also starting on page 1) maps to OCP Telemetry data area 2,
  //   where the OCP telemetry data area 2 starts at byte 0, i.e., OCP Telemetry
  //   data area 2 overlaps the OCP telemetry data header and data area 1.
  // The OCP data area 1 statistics start and event fifo start offsets are relative
  // to byte 0 of the OCP telemetry data header. The OCP data area 2 statistics
  // start and the event fifo start offsets are relative to the start of byte 0 in
  // the OCP telemetry data area 2 which is the same as the start of the OCP Telemetry
  // data header. So all these start offsets are relative to byte 0 of page 1.
  if (internal_status->area1_last_log_page == 0) {
    return false;
  }

  // area1 starts at log page 1
  if (!ataReadLogExt(device, 0x24, 0, 1, ocp_data_header, 1)) {
    return false;
  }

  if (!validate_ocp_telemetry_data_header(ocp_data_header, nsectors))
    return false;

  size_t log_size = (ocp_data_header->statistic1_size_dword +
                     ocp_data_header->statistic2_size_dword +
                     ocp_data_header->event1_FIFO_size_dword +
                     ocp_data_header->event2_FIFO_size_dword) << 2;

  char *logs = (char *)malloc(log_size);
  if (logs == NULL) {
    return false;
  }

  size_t bytes_read = 0;
  if (ocp_data_header->statistic1_size_dword > 0) {
    if (!read_ocp_telemetry_data_range_sata(device, ocp_data_header->statistic1_start_dword,
                                            ocp_data_header->statistic1_size_dword,
                                            logs)) {
      goto read_error;
    }
    bytes_read += ocp_data_header->statistic1_size_dword << 2;
  }
  if (ocp_data_header->statistic2_size_dword > 0) {
    if (!read_ocp_telemetry_data_range_sata(device, ocp_data_header->statistic2_start_dword,
                                            ocp_data_header->statistic2_size_dword,
                                            &logs[bytes_read])) {
      goto read_error;
    }
    bytes_read += ocp_data_header->statistic2_size_dword << 2;
  }
  if (ocp_data_header->event1_FIFO_size_dword > 0) {
    if (!read_ocp_telemetry_data_range_sata(device, ocp_data_header->event1_FIFO_start_dword,
                                            ocp_data_header->event1_FIFO_size_dword,
                                            &logs[bytes_read])) {
      goto read_error;
    }
    bytes_read += ocp_data_header->event1_FIFO_size_dword << 2;
  }
  if (ocp_data_header->event2_FIFO_size_dword > 0) {
    if (!read_ocp_telemetry_data_range_sata(device, ocp_data_header->event2_FIFO_start_dword,
                                            ocp_data_header->event2_FIFO_size_dword,
                                            &logs[bytes_read])) {
      goto read_error;
    }
  }
  *stats = logs;
  return true;

read_error:
  free(logs);
  return false;
}

} // namespace smartmon
