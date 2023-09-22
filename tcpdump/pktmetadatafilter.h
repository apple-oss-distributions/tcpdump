/*
 * Copyright (c) 2012-2013 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#ifndef tcpdump_pktmetadatafilter_h
#define tcpdump_pktmetadatafilter_h

struct node;
typedef struct node node_t;

struct pkt_meta_data {
	const char *itf;
	uint32_t dlt;
	const char *proc;
	const char *eproc;
	pid_t pid;
	pid_t epid;
	const char *dir;
	const char *svc;
	uint32_t flowid;
};


node_t * parse_expression(const char *);
void print_expression(node_t *);
int evaluate_expression(node_t *, struct pkt_meta_data *);
void free_expression(node_t *);

#ifdef DEBUG
void set_parse_verbose(int val);
#endif /* DEBUG */

#endif
