/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AJV4L2_OUTPUT_H
#define AJV4L2_OUTPUT_H

#include "ajv4l2.h"

int ajv4l2_output_start(struct ajv4l2_port *port);
void ajv4l2_output_stop(struct ajv4l2_port *port);
void ajv4l2_output_kick(struct ajv4l2_port *port);
const char *ajv4l2_output_describe(const struct ajv4l2_port *port, char *buf, size_t len);

#endif
