/*
 * ocptelemetryprint.h
 *
 * Copyright (c) 2026 Western Digital Corporation or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef OCPTELEMETRYPRINT_H
#define OCPTELEMETRYPRINT_H

#include <smartmon/dev_interface.h>

bool print_ata_ocp_telemetry_log(smartmon::ata_device * device, unsigned nsectors_0x24, unsigned nsectors_0x25);

#endif // OCPTELEMETRYPRINT_H
