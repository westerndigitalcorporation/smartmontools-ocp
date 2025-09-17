/*
 * ocptelemetryprint.cpp
 *
 * Copyright (c) 2026 Western Digital Corporation or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#include "ocptelemetryprint.h"

#include <smartmon/ocptelemetry.h>
#include <smartmon/sg_unaligned.h>

#include "smartctl.h"

#include <cmath>

using namespace smartmon;

#define OCP_STR_BUF_SIZE  256

#define MIN(_A, _B) ((_A) < (_B) ? (_A) : (_B))

static uint64_t ocp_telemetry_timestamp_to_uint64(uint8_t timestamp[6],
                                                  uint16_t timestamp_info)
{
  uint64_t msecs_since_1970 = 0;
  uint8_t protocol = (timestamp_info & 0x30) >> 4;

  // SAS timestamp is big endian
  if (protocol == 1) {
    msecs_since_1970 = sg_get_unaligned_be32(timestamp);
    msecs_since_1970 = (msecs_since_1970 << 16) + sg_get_unaligned_be16(&timestamp[4]);
  } else if (protocol == 2) {
    msecs_since_1970 = sg_get_unaligned_le32(&timestamp[2]);
    msecs_since_1970 = (msecs_since_1970 << 16) + sg_get_unaligned_le16(timestamp);
  } else {
    pout("Unknown timestamp protocol (%d)", protocol);
  }
  return msecs_since_1970;
}

static void ocp_guid_to_str(char *str, uint8_t guid[OCP_GUID_LEN])
{
  for (int i = 0; i < OCP_GUID_LEN; i++)
    snprintf(&str[i * 2], 3, "%02X", guid[OCP_GUID_LEN - i - 1]);
  snprintf(&str[OCP_GUID_LEN * 2], 2, "h");
}

void ocp_ascii_to_c_str(void *data, size_t size, char *c_str, size_t len)
{
  char *val = (char *)data;
  size_t i = size - 1;

  do {
    if (val[i] != ' ') {
      ++i;
      break;
    }
    --i;
  } while(i > 0);
  i = MIN(i, len - 1);
  if (i > 0) {
    memcpy(c_str, data, i);
  }
  c_str[i] = 0;
}

static void set_indent_spaces(char *str, size_t size, unsigned count)
{
  unsigned indent = MIN(size - 1, count);
  memset(str, ' ', indent);
  str[indent] = 0;
}

static void hex_dump(char *buf, size_t len, unsigned int idx, bool ascii, bool single_line,
                     uint8_t *data, size_t size)
{
  unsigned char *val = (uint8_t *)data;
  size_t pos = 0;
  size_t max = single_line ? size : 16;

  if (!single_line)
    pos = snprintf(buf, len, "%07x: ", idx);
  for (size_t i = 0; i < max; i++) {
    if (i < size)
      pos += snprintf(buf + pos, len - pos, "%s%02x ", single_line ? "0x" : "", val[i]);
    else
      pos += snprintf(buf + pos, len - pos, "   ");
  }
  if (ascii) {
    for (size_t i = 0; i < max; i++) {
#define VAL(n) (' ' <= val[n] && val[n] <= '~' ? (int)val[n] : '.')
      if (i < size)
        pos += snprintf(buf + pos, len - pos, "%c", VAL(i));
      else
        pos += snprintf(buf + pos, len - pos, " ");
    }
  }
}

static void hex_dump_line(json::ref jref, void *data, size_t size, bool newline)
{
  // For single line, each byte will be printed as "0xXX "
  size_t string_size = size * 5 + 1;
  char *val_hex = (char *)malloc(string_size);

  if (val_hex == NULL) {
    jout("N/A");
    jref = "N/A";
    return;
  }
  hex_dump(val_hex, string_size, 0, false, true, (uint8_t *)data, size);
  jout("%s", val_hex);
  if (newline)
    jout("\n");
  jref = val_hex;
}

static void hex_dump_lines(json::ref jref, void *data, size_t size, unsigned indent)
{
  char val_hex[OCP_STR_BUF_SIZE];
  size_t i = 0, j = 0;
  uint8_t *val = (uint8_t *)data;
  char header[OCP_STR_BUF_SIZE];

  set_indent_spaces(header, sizeof header, indent);

  while (i < size) {
    hex_dump(val_hex, sizeof val_hex, i, true, false, val, size - i);
    jout("%s%s%s", i == 0 ? "" : "\n", header, val_hex);
    jref[j] = val_hex;
    val += 16;
    i += 16;
    ++j;
  }
}

static uint64_t ocp_get_uint_val(void *data, size_t size)
{
  switch (size) {
  case 1: {
    uint8_t *val = (uint8_t *)data;
    return *val;
  }
  case 2:
    return sg_get_unaligned_le16(data);
  case 4:
    return sg_get_unaligned_le32(data);
  case 8:
    return sg_get_unaligned_le64(data);
  default:
    return 0;
  }
}

static int64_t ocp_get_int_val(void *data, size_t size)
{
  switch (size) {
  case 1: {
    int8_t *val = (int8_t *)data;
    return *val;
  }
  case 2:
    return (int16_t)sg_get_unaligned_le16(data);
  case 4:
    return (int32_t)sg_get_unaligned_le32(data);
  case 8:
    return (int64_t)sg_get_unaligned_le64(data);
  default:
    return 0;
  }
}

static void ocp_print_stat_value(json::ref jref_data, enum ocp_data_type type, void *data, size_t size)
{
  switch (type) {
  case OCP_DATA_TYPE_INT: {
    int64_t val64 = ocp_get_int_val(data, size);
    jout("%" PRId64, val64);
    jref_data = val64;
    break;
  }
  case OCP_DATA_TYPE_UINT: {
    uint64_t val64 = ocp_get_uint_val(data, size);;
    jout("%" PRIu64, val64);
    jref_data = val64;
    break;
  }
  case OCP_DATA_TYPE_ASCII: {
          char *str = (char *)malloc(size + 1);
    if (str == NULL)
      break;
    ocp_ascii_to_c_str(data, size, str, size + 1);
    jout("%s", str);
    jref_data = str;
    break;
  }
  case OCP_DATA_TYPE_FP:
    hex_dump_line(jref_data, (uint8_t *)data, size, false);
    break;
  case OCP_DATA_TYPE_NA:
    hex_dump_line(jref_data, data, size, false);
    break;
  }
}

static int ocp_get_stat_type(uint8_t info_0, enum ocp_stat_type *type)
{
  int field_value = (info_0 >> 4) & 0xf;
  if (field_value > OCP_STAT_TYPE_CUSTOM)
    return -1;
  *type = (enum ocp_stat_type)field_value;
  return 0;
}

static int ocp_get_data_type(uint8_t info_2, enum ocp_data_type *type)
{
  int field_value = info_2 & 0xf;
  if (field_value > OCP_DATA_TYPE_ASCII)
    return -1;
  *type = (enum ocp_data_type)field_value;
  return 0;
}

static void ocp_stat_id_to_str(ocp_string_def *string_def, uint16_t id, char *stat_str, size_t size)
{
  for (size_t i = 0; i < OCP_BUILTIN_STAT_STR_LEN; i++) {
    if (ocp_builtin_stat_str[i].id == id) {
      strncpy(stat_str, ocp_builtin_stat_str[i].desc, size - 1);
      return;
    }
  }

  if (id >= 0x8000) {
    std::map<uint16_t, struct ocp_statistic_id_string_table_entry>::iterator it =
      string_def->stat_id_string_map.find(id);
    if (it != string_def->stat_id_string_map.end()) {
      uint64_t offset = sg_get_unaligned_le64(it->second.ascii_id_offset);
      memcpy(stat_str, &string_def->ocp_string_ascii_table[offset], it->second.ascii_id_len);
      stat_str[it->second.ascii_id_len] = 0;
    } else
      snprintf(stat_str, size, "Vendor Unique ID");
  } else {
    snprintf(stat_str, size, "Reserved ID");
  }
}

static void print_ata_log_stat_desc(json::ref jref, struct ocp_statistic_descriptor *sp, unsigned indent)
{
  struct ocp_ata_log_stat_desc *desc = (struct ocp_ata_log_stat_desc *)sp;
  uint8_t *log_page_pos = desc->log_page_data;
  char header[OCP_STR_BUF_SIZE];

  set_indent_spaces(header, sizeof header, indent);

  jout("%sLog Address              : %" PRIx8 "\n", header, desc->log_addr);
  jref["log_address"] = desc->log_addr;
  jout("%sLog Page Count           : %" PRIx8 "\n", header, desc->log_page_count);
  jref["log_page_count"] = desc->log_page_count;
  jout("%sInitial Log Page         : %" PRIx16, header, desc->initial_log_page);
  jref["initial_log_page"] = desc->initial_log_page;

  json::ref jref1 = jref["log_page"];
  for (int i = 0; i < desc->log_page_count; i++) {
    jout("\n%sLog Page 0x%04" PRIx16 ":\n", header, i + desc->initial_log_page);
    hex_dump_lines(jref1[i], log_page_pos, 512, indent + 2);
    log_page_pos += 512;
  }
}

static void print_scsi_log_stat_desc(json::ref jref, struct ocp_statistic_descriptor *sp, unsigned indent)
{
  struct ocp_scsi_log_stat_desc *desc = (struct ocp_scsi_log_stat_desc *)sp;
  char header[OCP_STR_BUF_SIZE];

  set_indent_spaces(header, sizeof header, indent);

  jout("%sLog Page                 : 0x%04" PRIx8 "\n", header, desc->log_page);
  jref["log_page"] = desc->log_page;
  jout("%sLog Subpage              : 0x%04" PRIx8 "\n", header, desc->log_subpage);
  jref["log_subpage"] = desc->log_subpage;
  jout("%sLog Page Data            :\n", header);

  hex_dump_lines(jref["log_page_data"], desc->log_page_data, (desc->h.statistic_data_size - 1) << 2,
                 indent + 2);
}

static void print_hdd_spinup_stat_desc(json::ref jref, struct ocp_hdd_spinup_stat_desc *desc, unsigned indent)
{
  uint16_t spinup_val;
  char header[OCP_STR_BUF_SIZE];

  set_indent_spaces(header, sizeof header, indent);

  spinup_val = sg_get_unaligned_le16(&desc->spinup_max);
  if (spinup_val != 0) {
    jout("%sLifetime Spinup Max      : 0x%04" PRIx16 "\n", header, spinup_val);
    jref["lifetime_spinup_max"] = spinup_val;
  }
  spinup_val = sg_get_unaligned_le16(&desc->spinup_min);
  if (spinup_val != 0) {
    jout("%sLifetime Spinup Min      : 0x%04" PRIx16 "\n", header, spinup_val);
    jref["lifetime_spinup_min"] = spinup_val;
  }
  jout("%sSpinup History           :", header);
  for (int i = 0; i < 10; i++) {
    spinup_val = sg_get_unaligned_le16(&desc->spinup_hist[i]);
    if (spinup_val == 0) {
      if (i == 0)
        jout("None");
      break;
    }
    jout("%s0x%04" PRIx16, i > 0 ? ", " : " ", spinup_val);
    jref["spinup_history"][i] = spinup_val;
  }
}

static void print_custom_stat_desc(json::ref jref, struct ocp_statistic_descriptor *sp, enum ocp_data_type data_type,
                                   unsigned indent)
{
  switch (sp->h.statistics_id) {
  case 0x02:
    jout("\n");
    print_ata_log_stat_desc(jref, sp, indent);
    break;
  case 0x03:
    jout("\n");
    print_scsi_log_stat_desc(jref, sp, indent);
    break;
  case 0x6006:
    jout("\n");
    print_hdd_spinup_stat_desc(jref, (struct ocp_hdd_spinup_stat_desc *)sp, indent);
    break;
  default:
    ocp_print_stat_value(jref["data"], data_type, sp->custom.data, sp->h.statistic_data_size << 2);
  }
}

static void ocp_stat_type_to_str(char *type_str, size_t size, uint8_t stat_type)
{
  switch (stat_type) {
  case OCP_STAT_TYPE_SINGLE:
    strncpy(type_str, "Single", size);
    break;
  case OCP_STAT_TYPE_ARRAY:
    strncpy(type_str, "Array", size);
    break;
  case OCP_STAT_TYPE_CUSTOM:
    strncpy(type_str, "Custom", size);
    break;
  default:
    strncpy(type_str, "Reserved", size);
  }
}

static void ocp_behavior_type_to_str(char *type_str, size_t size, uint8_t behavior_type)
{
  switch (behavior_type) {
  case OCP_BEHV_TYPE_NA:
    strncpy(type_str, "N/A", size);
    break;
  case OCP_BEHV_TYPE_NONE:
    strncpy(type_str, "Runtime Value", size);
    break;
  case OCP_BEHV_TYPE_R_PC:
    strncpy(type_str, "Reset Persistent, Power Cycle Resistent", size);
    break;
  case OCP_BEHV_TYPE_SC_R:
    strncpy(type_str, "Saturating Counter, Reset Persistent", size);
    break;
  case OCP_BEHV_TYPE_SC_R_PC:
    strncpy(type_str, "Saturating Counter, Reset Persistent, Power Cycle Resistent", size);
    break;
  case OCP_BEHV_TYPE_SC:
    strncpy(type_str, "Saturating Counter", size);
    break;
  case OCP_BEHV_TYPE_R:
    strncpy(type_str, "Reset Persistent", size);
    break;
  default:
    strncpy(type_str, "Reserved", size);
  }
}

static void ocp_host_hint_type_to_str(char *type_str, size_t size, uint8_t hint_type)
{
  switch (hint_type) {
  case 0x00:
    strncpy(type_str, "No Host Hint", size);
    break;
  case 0x01:
    strncpy(type_str, "Host Hint Type 1", size);
    break;
  default:
    strncpy(type_str, "Reserved", size);
  }
}

static void ocp_data_type_to_str(char *type_str, size_t size, uint8_t stat_type)
{
  switch (stat_type) {
  case OCP_DATA_TYPE_NA:
    strncpy(type_str, "No Data Type Information", size);
    break;
  case OCP_DATA_TYPE_INT:
    strncpy(type_str, "Signed Integer", size);
    break;
  case OCP_DATA_TYPE_UINT:
    strncpy(type_str, "Unsigned Integer", size);
    break;
  case OCP_DATA_TYPE_FP:
    strncpy(type_str, "Floating Point", size);
    break;
  case OCP_DATA_TYPE_ASCII:
    strncpy(type_str, "ASCII (7-bit)", size);
    break;
  default:
    strncpy(type_str, "Reserved", size);
  }
}

static void ocp_print_stat_desc_info(json::ref jref, struct ocp_statistic_descriptor *sp,
                                     const char *header)
{
  char buffer[OCP_STR_BUF_SIZE];

  uint8_t val = sp->h.statistics_info[0] >> 4;
  ocp_stat_type_to_str(buffer, sizeof buffer, val);
  jout("%sStatistic Type           : 0x%" PRIx8 ", %s\n", header, val, buffer);
  jref["statistic type"] = val;

  val = sp->h.statistics_info[0] & 0xf;
  ocp_behavior_type_to_str(buffer, sizeof buffer, val);
  jout("%sBehavior Type            : 0x%02" PRIx8 ", %s\n", header, val, buffer);
  jref["behavior type"] = val;

  snprintf(buffer, sizeof buffer, "0x%02" PRIx8 ", %s",
           sp->h.statistics_info[1],
           sp->h.statistics_info[1] > OCP_UNIT_TYPE_MAX ?
           "Reserved" :
           ocp_stat_data_unit_str[sp->h.statistics_info[1]]);
  jout("%sUnit                     : %s\n", header, buffer);
  jref["unit"] = buffer;

  val = (sp->h.statistics_info[2] >> 4) & 0x3;
  ocp_host_hint_type_to_str(buffer, sizeof buffer, val);
  jout("%sHost Hint Type           : 0x%" PRIx8 ", %s\n", header, val, buffer);
  jref["host hint type"] = val;

  val = sp->h.statistics_info[2] & 0xf;
  ocp_data_type_to_str(buffer, sizeof buffer, val);
  jout("%sData Type                : 0x%" PRIx8 ", %s\n", header, val, buffer);
  jref["data type"] = val;
}

static bool ocp_print_stat_desc(json::ref jref, struct ocp_statistic_descriptor *sp, unsigned indent,
                                ocp_string_def *string_def)
{
  enum ocp_stat_type stat_type;
  enum ocp_data_type data_type;
  char stat_id_str[OCP_STR_BUF_SIZE];
  char header[OCP_STR_BUF_SIZE];

  set_indent_spaces(header, sizeof header, indent);

  if (ocp_get_stat_type(sp->h.statistics_info[0], &stat_type)) {
    jout("Malformed statistics descriptor skipped - statistics type not supported\n");
    return false;
  }

  if (ocp_get_data_type(sp->h.statistics_info[2], &data_type)) {
    jout("Malformed statistic descriptor skipped - data type not supported\n");
    return false;
  }

  ocp_stat_id_to_str(string_def, sp->h.statistics_id, stat_id_str, sizeof stat_id_str);
  jout("%sStatistic ID             : 0x%04" PRIx16 ", %s\n", header, sp->h.statistics_id, stat_id_str);
  jref["ID"] = stat_id_str;

  ocp_print_stat_desc_info(jref, sp, header);

  jout("%sStatistic Data Size      : 0x%" PRIx16 "\n", header, sg_get_unaligned_le16(&sp->h.statistic_data_size));
  jref["data size"] = sg_get_unaligned_le16(&sp->h.statistic_data_size);

  jout("%sData                     : ", header);

  switch (stat_type) {
  case OCP_STAT_TYPE_SINGLE:
    ocp_print_stat_value(jref["data"], data_type, sp->single.data, sp->h.statistic_data_size << 2);
    break;
  case OCP_STAT_TYPE_ARRAY: {
    uint8_t *data = sp->array.data;
    jout("[ ");
    for (int elem = 0; elem < (sp->array.number_of_elements + 1); ++elem) {
      if (elem > 0)
        jout(", ");
      ocp_print_stat_value(jref["data"][elem], data_type, data, sp->array.element_size + 1);
      data += sp->array.element_size + 1;
    }
    jout(" ]");
    break;
  }
  case OCP_STAT_TYPE_CUSTOM:
    print_custom_stat_desc(jref, sp, data_type, indent + 2);
    break;
  }

  jout("\n");

  return true;
}

static void ocp_print_telemetry_statistics(json::ref stat_list, void *log_page, size_t dwords,
                                           ocp_string_def *string_def)
{
  uint32_t *log_page_dwords = (uint32_t *)log_page;
  unsigned idx = 0;
  char buffer[OCP_STR_BUF_SIZE];

  while (dwords) {
    struct ocp_statistic_descriptor *sp;
    size_t dwords_consumed;

    sp = (struct ocp_statistic_descriptor *)log_page_dwords;
    if (sp->h.statistics_id == 0)
      break;
    dwords_consumed = (sizeof(sp->h) >> 2) + sp->h.statistic_data_size;

    snprintf(buffer, sizeof buffer, "Statistic Descriptor %i", idx);
    jout("  %s\n", buffer);
    json::ref jref_desc = stat_list[idx];

    if (ocp_print_stat_desc(jref_desc, sp, 4, string_def))
      idx++;

    dwords -= dwords_consumed;
    log_page_dwords += dwords_consumed;
  }
  jout("\n");
}

static void event_class_to_str(uint8_t dbg_class, char *class_str, size_t size)
{
  switch (dbg_class) {
  case OCP_EVENT_CLASS_TIMESTAMP:
    strncpy(class_str, "Timestamp Class", size);
    break;
  case OCP_EVENT_CLASS_RESET:
    strncpy(class_str, "Reset Class", size);
    break;
  case OCP_EVENT_CLASS_BOOT_SEQ:
    strncpy(class_str, "Boot Sequence Class", size);
    break;
  case OCP_EVENT_CLASS_FIRMWARE_ASSERT:
    strncpy(class_str, "Firmware Assert Class", size);
    break;
  case OCP_EVENT_CLASS_TEMPERATURE:
    strncpy(class_str, "Temperature Class", size);
    break;
  case OCP_EVENT_CLASS_MEDIA:
    strncpy(class_str, "Media Class", size);
    break;
  case OCP_EVENT_CLASS_MEDIA_WEAR:
    strncpy(class_str, "Media Wear Class", size);
    break;
  case OCP_EVENT_CLASS_STATISTIC_SNAP:
    strncpy(class_str, "Statistic Snapshot Class", size);
    break;
  case OCP_EVENT_CLASS_VIRTUAL_FIFO:
    strncpy(class_str, "Virtual FIFO Event Class", size);
    break;
  case OCP_EVENT_CLASS_SATA_PHY_LINK:
    strncpy(class_str, "SATA Phy/Link Class", size);
    break;
  case OCP_EVENT_CLASS_SATA_TRANSPORT:
    strncpy(class_str, "SATA Transport Class", size);
    break;
  case OCP_EVENT_CLASS_SAS_PHY_LINK:
    strncpy(class_str, "SAS Phy/Link Class", size);
    break;
  case OCP_EVENT_CLASS_SAS_TRANSPORT:
    strncpy(class_str, "SAS Transport Class", size);
    break;
  default:
    if (dbg_class < 0x80) {
      snprintf(class_str, size, "Unknown Class %02" PRIx8, dbg_class);
      break;
    }
    snprintf(class_str, size, "Vendor Unique Class %02" PRIx8, dbg_class);
  }
}

static bool timestamp_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_TIMESTAMP_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_timestamp_event_id_str[event_id]);
    return true;
  }
  return false;
}

static bool reset_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_RESET_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_reset_event_id_str[event_id]);
    return true;
  }
  return false;
}

static bool boot_seq_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_BOOT_SEQ_EVENT_FTL_READY) {
    snprintf(event_str, size, "%s", ocp_ssd_boot_seq_event_id_str[event_id]);
  } else  if (event_id >= OCP_BOOT_SEQ_EVENT_HDD_MAIN_FW_BOOT_COMPLETE &&
              event_id <= OCP_BOOT_SEQ_EVENT_DEVICE_READY) {
    snprintf(event_str, size, "%s",
             ocp_hdd_boot_seq_event_id_str[event_id -
                                           OCP_BOOT_SEQ_EVENT_HDD_MAIN_FW_BOOT_COMPLETE]);
  } else {
    return false;
  }
  return true;
}

static bool fw_assert_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_FW_ASSERT_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_fw_assert_event_id_str[event_id]);
    return true;
  }
  return false;
}

static bool temperature_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_TEMPERATURE_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_temperature_event_id_str[event_id]);
    return true;
  }
  return false;
}

static bool media_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_MEDIA_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_media_event_id_str[event_id]);
    return true;
  }
  return false;
}

static bool media_wear_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_MEDIA_WEAR_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_media_wear_event_id_str[event_id]);
    return true;
  }
  return false;
}

static bool virtual_fifo_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_VIRTUAL_FIFO_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_virtual_fifo_event_id_str[event_id]);
    return true;
  }
  // The Virtual event FIFO names are stored with the virtual FIFO marker as the ID
  // in the event string DB.
  return false;
}

static bool sata_phy_link_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_SATA_PHY_LINK_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_sata_phy_link_event_id_str[event_id]);
    return true;
  }
  return false;
}

static bool sata_transport_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_SATA_TRANSPORT_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_sata_transport_event_id_str[event_id]);
    return true;
  }
  return false;
}

static bool sas_phy_link_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_SAS_PHY_LINK_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_sas_phy_link_event_id_str[event_id]);
    return true;
  }
  return false;
}

static bool sas_transport_event_id_to_str(uint16_t event_id, char *event_str, size_t size)
{
  if (event_id <= OCP_SAS_TRANSPORT_EVENT_MAX) {
    snprintf(event_str, size, "%s", ocp_sas_transport_event_id_str[event_id]);
    return true;
  }
  return false;
}

static bool event_id_to_str(uint8_t dbg_class, uint8_t id[2], char *event_str, size_t size,
                            ocp_string_def *string_def)
{
  uint16_t event_id = sg_get_unaligned_le16(id);
  bool success = false;

  switch (dbg_class) {
  case OCP_EVENT_CLASS_TIMESTAMP:
    success = timestamp_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_RESET:
    success = reset_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_BOOT_SEQ:
    success = boot_seq_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_FIRMWARE_ASSERT:
    success = fw_assert_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_TEMPERATURE:
    success = temperature_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_MEDIA:
    success = media_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_MEDIA_WEAR:
    success = media_wear_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_STATISTIC_SNAP:
    return false;
  case OCP_EVENT_CLASS_VIRTUAL_FIFO:
    success = virtual_fifo_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_SATA_PHY_LINK:
    success = sata_phy_link_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_SATA_TRANSPORT:
    success = sata_transport_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_SAS_PHY_LINK:
    success = sas_phy_link_event_id_to_str(event_id, event_str, size);
    break;
  case OCP_EVENT_CLASS_SAS_TRANSPORT:
    success = sas_transport_event_id_to_str(event_id, event_str, size);
    break;
  default:
    break;
  }

  if (success)
    return true;

  std::map<uint32_t, struct ocp_event_id_string_table_entry>::iterator it =
    string_def->event_string_map.find(OCP_EVENT_KEY(dbg_class, id));
  if (it != string_def->event_string_map.end()) {
    uint64_t offset = sg_get_unaligned_le64(it->second.ascii_id_offset);
    size_t to_copy = MIN(size -1, it->second.ascii_id_len);
    memcpy(event_str, &string_def->ocp_string_ascii_table[offset], to_copy);
    event_str[to_copy] = 0;
  } else  if (event_id >= 0x8000) {
    strncpy(event_str, "Vendor Unique ID", size - 1);
  } else {
    strncpy(event_str, "Reserved ID", size - 1);
  }
  return true;
}

static void print_event_desc(json::ref jref, uint8_t dbg_class, uint8_t id[2], uint8_t *data, uint8_t size,
                             unsigned indent, ocp_string_def *string_def)
{
  char buffer[OCP_STR_BUF_SIZE];
  char header[OCP_STR_BUF_SIZE];

  set_indent_spaces(header, sizeof header, indent);

  event_class_to_str(dbg_class, buffer, sizeof buffer);
  jout("%sClass                    : 0x%02" PRIx8 ", %s\n", header, dbg_class, buffer);
  jref["Class"] = buffer;
  if (event_id_to_str(dbg_class, id, buffer, sizeof buffer, string_def)) {
    jout("%sId                       : 0x%04" PRIx16 ", %s\n", header, sg_get_unaligned_le16(id), buffer);
    jref["ID"] = buffer;
  }

  switch (dbg_class) {
  case OCP_EVENT_CLASS_TIMESTAMP: {
    struct ocp_event_timestamp *ts = (struct ocp_event_timestamp *)data;
    uint64_t timestamp = sg_get_unaligned_le64(ts->timestamp);
    jout("%sTimestamp                : 0x%04" PRIx64 "\n", header, timestamp);
    jref["Timestamp"] = timestamp;
    data += sizeof *ts;
    size -= sizeof *ts;
    break;
  }
  case OCP_EVENT_CLASS_MEDIA_WEAR:
    if (sg_get_unaligned_le16(id) == OCP_MEDIA_WEAR_EVENT_MEDIA_WEAR) {
      struct ocp_event_media_wear *wear = (struct ocp_event_media_wear *)data;

      uint32_t tb = sg_get_unaligned_le64(&wear->host_tb_written);
      jout("%sHost TB Written          : 0x%04" PRIx32 "\n", header, tb);
      jref["Host TB written"] = tb;
      tb = sg_get_unaligned_le64(&wear->media_tb_written);
      jout("%sMedia TB Written         : 0x%04" PRIx32 "\n", header, tb);
      jref["media TB written"] = tb;
      tb = sg_get_unaligned_le64(&wear->ssd_media_tb_erased);
      jout("%sSSD Media TB Erased      : 0x%04" PRIx32 "\n", header, tb);
      jref["SSD media TB erased"] = tb;
    }
    data += sizeof(struct ocp_event_media_wear);
    size -= sizeof(struct ocp_event_media_wear);
    break;
  case OCP_EVENT_CLASS_STATISTIC_SNAP: {
    struct ocp_statistic_descriptor *sp = (struct ocp_statistic_descriptor *)data;
    jout("%sStatistic Descriptor Snapshot:\n", header);
    ocp_print_stat_desc(jref["Statistic descriptor"], sp, indent + 2, string_def);
    size = 0;
    break;
  }
  case OCP_EVENT_CLASS_VIRTUAL_FIFO: {
    struct ocp_event_virtual_fifo *vf = (struct ocp_event_virtual_fifo *)data;
    uint16_t marker = sg_get_unaligned_le16(vf->marker);
    uint16_t number = marker & 0x7ff;
    uint8_t data_area = marker >> 11 & 0x7;
    jout("%sVirtual FIFO Data Area   : 0x%04" PRIx8 "\n", header, data_area);
    jref["data area"] = data_area;
    if (event_id_to_str(dbg_class, vf->marker, buffer, sizeof buffer, string_def)) {
      jout("%sVirtual FIFO Number      : 0x%04" PRIx16 "\n", header, number);
      jout("%sVirtual FIFO Name        : %s\n", header, buffer);
      jref["virtual fifo number"] = number;
      jref["virtual fifo name"] = buffer;
    }
    data += sizeof *vf;
    size -= sizeof *vf;
    break;
  }
  case OCP_EVENT_CLASS_SATA_TRANSPORT: {
    struct ocp_event_class_0Dh *class_0Dh = (struct ocp_event_class_0Dh *)data;
    jout("%sFIS                      : ", header);
    hex_dump_line(jref["FIS"], class_0Dh->fis, sizeof class_0Dh->fis, true);
    data += sizeof class_0Dh->fis;
    size -= sizeof class_0Dh->fis;
    break;
  }
  }

  if (size > 0 && dbg_class < 0x80) {
    struct ocp_event_vu *vu = (struct ocp_event_vu *)data;

    event_id_to_str(dbg_class, vu->id, buffer, sizeof buffer, string_def);
    jout("%sVU Event ID              : 0x%04" PRIx16 ", %s\n", header, sg_get_unaligned_le16(vu->id), buffer);
    jref["VU ID"] = sg_get_unaligned_le16(vu->id);
    data += sizeof *vu;
    size -= sizeof *vu;
  }
  if (size > 0) {
    jout("%sVU Data                  : ", header);
    hex_dump_line(jref["vu data"], data, size, true);
  }
}

static size_t ocp_get_event_desc_dwords(struct ocp_event_descriptor *e, size_t max_size)
{
  if (e->debug_event_class_type == OCP_EVENT_CLASS_STATISTIC_SNAP) {
    struct ocp_statistic_header *s;
    /* Need the statistics descriptor header in the snapshot to
     * determine the complete length. */
    if (max_size < sizeof *e + sizeof *s) {
      return sizeof *e + sizeof *s;
    }
    s = (struct ocp_statistic_header *)e->data;
    return (sizeof *e >> 2) + (sizeof *s >> 2) + s->statistic_data_size;
  } else
    return (sizeof *e >> 2) + e->data_size;
}

static void ocp_print_telemetry_events(json::ref event_list, void *log_page, size_t dwords,
                                       ocp_string_def *string_def)
{
  uint32_t *log_page_dwords = (uint32_t *)log_page;
  char buffer[OCP_STR_BUF_SIZE];
  unsigned idx = 0;

  while (dwords) {
    struct ocp_event_descriptor *ep;
    size_t dwords_consumed;

    ep = (struct ocp_event_descriptor *)log_page_dwords;
    if (ep->debug_event_class_type == 0)
      // End of FIFO
      break;
    dwords_consumed = ocp_get_event_desc_dwords(ep, dwords << 2);

    snprintf(buffer, sizeof(buffer), "Event Descriptor %i", idx);
    jout("  %s\n", buffer);
    json::ref jref_desc = event_list[idx];

    print_event_desc(jref_desc, ep->debug_event_class_type, ep->event_id, ep->data,
                     ep->data_size << 2, 4, string_def);

    idx++;
    dwords -= dwords_consumed;
    log_page_dwords += dwords_consumed;
  }
  jout("\n");
}

static void ocp_print_telemetry_data_header(json::ref stat_log, struct ocp_telemetry_data_header *header)
{
  jout("OCP Telemetry Data Header\n");
  json::ref jref = stat_log["ocp_telemetry_data_header"];

  jout("  Major Version            : 0x%04" PRIx16 "\n", sg_get_unaligned_le16(&header->major_version));
  jref["major_version"] = sg_get_unaligned_le16(&header->major_version);
  jout("  Minor Version            : 0x%04" PRIx16 "\n", sg_get_unaligned_le16(&header->minor_version));
  jref["minor_version"] = sg_get_unaligned_le16(&header->minor_version);
  uint64_t timestamp = ocp_telemetry_timestamp_to_uint64(header->timestamp,
                                                         header->timestamp_info);
  jout("  Timestamp                : 0x%04" PRIx64 "\n", timestamp);
  jref["timestamp"] = timestamp;
  char guid_str[OCP_GUID_LEN * 2 + 2];
  ocp_guid_to_str(guid_str, header->guid);
  jout("  GUID                     : %s\n", guid_str);
  jref["guid"] = guid_str;
  jout("  Device String Data Size  : 0x%04" PRIx16 "\n", sg_get_unaligned_le16(&header->device_string_data_size));
  jref["device_string_data_size"] = sg_get_unaligned_le16(&header->device_string_data_size);
  char fw_str[8 + 1];
  ata_format_id_string(fw_str, header->firmware_version, sizeof(fw_str)-1);
  jout("  Firmware version         : %s\n", fw_str);
  jref["firmware_version"] = fw_str;
  jout("  Statistic Area 1:\n");
  jout("    Start                  : 0x%04" PRIx64 "\n", header->statistic1_start_dword);
  jout("    Size                   : 0x%04" PRIx64 "\n", header->statistic1_size_dword);
  jout("  Statistic Area 2:\n");
  jout("    Start                  : 0x%04" PRIx64 "\n", header->statistic2_start_dword);
  jout("    Size                   : 0x%04" PRIx64 "\n", header->statistic2_size_dword);
  jout("  Event FIFO 1:\n");
  jout("    Start                  : 0x%04" PRIx64 "\n", header->event1_FIFO_start_dword);
  jout("    Size                   : 0x%04" PRIx64 "\n", header->event1_FIFO_size_dword);
  jout("  Event FIFO 2:\n");
  jout("    Start                  : 0x%04" PRIx64 "\n", header->event2_FIFO_start_dword);
  jout("    Size                   : 0x%04" PRIx64 "\n", header->event2_FIFO_size_dword);
  jout("\n");
}

void print_ata_device_internal_status(json::ref jref, struct ata_device_internal_status *log, bool current)
{
  json::ref jref_ata = current ? jref["ata current device internal status"] :
                                       jref["ata saved device internal status"];
  if (current)
    jout("Current Device Internal Status log (GP Log 0x24)\n");
  else
    jout("Saved Device Internal Status log (GP Log 0x25)\n");

  jout("  Organization ID             : 0x%08" PRIx32 "\n", log->organization_id);
  jref_ata["organization_id"] = log->organization_id;
  jout("  Area 1 Last Log Page        : 0x%04" PRIx16 "\n", log->area1_last_log_page);
  jref_ata["area1_last_log_page"] = log->area1_last_log_page;
  jout("  Area 2 Last Log Page        : 0x%04" PRIx16 "\n", log->area2_last_log_page);
  jref_ata["area2_last_log_page"] = log->area2_last_log_page;
  jout("  Area 3 Last Log Page        : 0x%04" PRIx16 "\n", log->area3_last_log_page);
  jref_ata["area3_last_log_page"] = log->area3_last_log_page;
  jout("  Saved Data Available        : %s\n", log->saved_data_available ? "true" : "false");
  jref_ata["saved_data_available"] = log->saved_data_available;
  jout("  Saved Data Generation Number: 0x%04" PRIx8 "\n", log->saved_data_generation_number);
  jref_ata["saved_data_generation_number"] = log->saved_data_generation_number;

  struct ocp_reason_id *rid = (struct ocp_reason_id *)log->reason_id;
  json::ref jref1 = jref_ata["reason id"];
  jout("  Reason ID:\n");
  jout("    Valid Flags         : 0x%" PRIx8 "\n", rid->valid_flags & 0xf);
  jref1["valid flags"] = rid->valid_flags & 0xf;
  if (rid->valid_flags & OCP_REASON_ID_ERROR_ID) {
    jout("    Error ID            : ");
    hex_dump_line(jref1["error id"], rid->error_id, sizeof rid->error_id, true);
  }
  if (rid->valid_flags & OCP_REASON_ID_FILE_ID) {
    jout("    File ID             : ");
    hex_dump_line(jref1["file id"], rid->file_id, sizeof rid->file_id, true);
  }
  if (rid->valid_flags & OCP_REASON_ID_LINE_NUMBER) {
    jout("    Line number         : 0x%04" PRIx16 "\n", rid->line_number);
    jref1["line number"] = rid->line_number;
  }
  if (rid->valid_flags & OCP_REASON_ID_VU_EXT) {
    jout("    VU Reason Extension : ");
    hex_dump_line(jref1["vu reason extension"], rid->vu_reason_extension,
                  sizeof rid->vu_reason_extension, true);
  }
  jout("\n");
}

static void ocp_print_telemetry_strings_header(json::ref stat_log,
                                               struct ocp_telemetry_strings_header *header,
                                               ocp_string_def *string_def)
{
  jout("OCP Telemetry Strings Header\n");
  json::ref jref = stat_log["ocp_telemetry_strings_header"];
  jout("  Log Page Version         : 0x%04" PRIx8 "\n", header->log_page_version);
  jref["log_page_version"] = header->log_page_version;
  char guid_str[OCP_GUID_LEN * 2 + 2];
  ocp_guid_to_str(guid_str, header->guid);
  jout("  GUID                     : %s\n", guid_str);
  jref["guid"] = guid_str;
  jout("  Statistics ID String Table:\n");
  jout("    Start                  : 0x%04" PRIx64 "\n", header->statistics_id_string_table_start);
  jout("    Size                   : 0x%04" PRIx64 "\n", header->statistics_id_string_table_size);
  jout("  Event String Table:\n");
  jout("    Start                  : 0x%04" PRIx64 "\n", header->event_string_table_start);
  jout("    Size                   : 0x%04" PRIx64 "\n", header->event_string_table_size);
  jout("  VU Event String Table:\n");
  jout("    Start                  : 0x%04" PRIx64 "\n", header->vu_event_string_table_start);
  jout("    Size                   : 0x%04" PRIx64 "\n", header->vu_event_string_table_size);
  jout("  ASCII Table:\n");
  jout("    Start                  : 0x%04" PRIx64 "\n", header->ascii_table_start);
  jout("    Size                   : 0x%04" PRIx64 "\n", header->ascii_table_size);

  ocp_ascii_to_c_str(header->event_fifo_1_name, sizeof header->event_fifo_1_name,
                     string_def->event_fifo_1_name, sizeof string_def->event_fifo_1_name);
  jout("  Event FIFO 1 Name        : %s\n", string_def->event_fifo_1_name);
  jref["event fifo 1 name"] = string_def->event_fifo_1_name;
  ocp_ascii_to_c_str(header->event_fifo_2_name, sizeof header->event_fifo_2_name,
                     string_def->event_fifo_2_name, sizeof string_def->event_fifo_2_name);
  jout("  Event FIFO 2 Name        : %s\n", string_def->event_fifo_2_name);
  jref["event fifo 2 name"] = string_def->event_fifo_2_name;
  jout("\n");
}

///////////////////////////////////////////////////////////////////////
// Print OCP Telemetry Log Pages

bool print_ata_ocp_telemetry_log(ata_device * device, unsigned nsectors_0x24, unsigned nsectors_0x25)
{
  struct ata_device_internal_status internal_status;
  struct ocp_telemetry_strings_header ocp_strings_header;
  ocp_string_def ocp_strings;

  if (!read_ata_ocp_telemetry_string_state(device, nsectors_0x25, &internal_status,
                                           &ocp_strings_header, &ocp_strings)) {
    return false;
  }

  json::ref jref_strings = jglb["ocp_telemetry_strings"];
  print_ata_device_internal_status(jref_strings, &internal_status, false);
  ocp_print_telemetry_strings_header(jref_strings, &ocp_strings_header, &ocp_strings);

  struct ocp_telemetry_data_header ocp_data_header;
  char *logs = NULL;

  if (!read_ata_ocp_telemetry_statistics(device, nsectors_0x24, &internal_status,
                                         &ocp_data_header, &logs)) {
    return false;
  }

  json::ref jref = jglb["ocp_telemetry_data"];
  print_ata_device_internal_status(jref, &internal_status, true);
  ocp_print_telemetry_data_header(jref, &ocp_data_header);

  size_t bytes_printed = 0;
  if (ocp_data_header.statistic1_size_dword > 0) {
    json::ref jref1 = jref["statistic_area_1"];
    jout("OCP Statistics Area 1\n");
    ocp_print_telemetry_statistics(jref1, logs,
                                   ocp_data_header.statistic1_size_dword,
                                   &ocp_strings);
    bytes_printed = ocp_data_header.statistic1_size_dword << 2;
  }
  if (ocp_data_header.statistic2_size_dword > 0) {
    json::ref jref1 = jref["statistic_area_2"];
    jout("OCP Statistics Area 2\n");
    ocp_print_telemetry_statistics(jref1, &logs[bytes_printed],
                                   ocp_data_header.statistic2_size_dword,
                                   &ocp_strings);
    bytes_printed += ocp_data_header.statistic2_size_dword << 2;
  }
  if (ocp_data_header.event1_FIFO_size_dword > 0) {
    json::ref jref1 = jref["event_fifo_1"];
    jout("OCP Event Fifo 1");
    if (strlen(ocp_strings.event_fifo_1_name) > 0) {
      jout(": %s", ocp_strings.event_fifo_1_name);
      jref1["name"] = ocp_strings.event_fifo_1_name;
    }
    jout("\n");
    json::ref jref2 = jref1["events"];
    ocp_print_telemetry_events(jref2, &logs[bytes_printed],
                               ocp_data_header.event1_FIFO_size_dword,
                               &ocp_strings);
    bytes_printed += ocp_data_header.event1_FIFO_size_dword << 2;
  }
  if (ocp_data_header.event2_FIFO_size_dword > 0) {
    json::ref jref1 = jref["event_fifo_2"];
    jout("OCP Event Fifo 2");
    if (strlen(ocp_strings.event_fifo_2_name) > 0) {
      jout(": %s", ocp_strings.event_fifo_2_name);
      jref1["name"] = ocp_strings.event_fifo_2_name;
    }
    jout("\n");
    json::ref jref2 = jref1["events"];
    ocp_print_telemetry_events(jref2, &logs[bytes_printed],
                               ocp_data_header.event2_FIFO_size_dword,
                               &ocp_strings);
  }

  free(logs);

  return true;
}
