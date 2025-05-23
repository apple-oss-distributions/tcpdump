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

#ifdef __APPLE__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>


#define __APPLE_PCAP_NG_API
#include <net/pktap.h>
#include <pcap.h>

#ifdef DLT_PCAPNG
#include <pcap/pcap-ng.h>
#include <pcap/pcap-util.h>
#endif /* DLT_PCAPNG */

#include <sys/queue.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>

#include <os/log.h>

#include "netdissect.h"
#include "interface.h"
#include "pktmetadatafilter.h"

extern node_t *pkt_meta_data_expression;

extern int pktap_if_count;

extern u_int packets_mtdt_fltr_drop;

extern char *svc2str(uint32_t);

/*
 * Returns:
 * -1: pktap header is truncated and cannot be parsed
 *  0: pktap header doesn't match
 *  other values: pktap header matches the filter
 */
int
pktap_filter_packet(netdissect_options *ndo, struct pcap_if_info *if_info,
					const struct pcap_pkthdr *h, const u_char *sp)
{
	struct pktap_header *pktp_hdr;
	const u_char *pkt_data;
	int match = 0;
	pcap_t *pcap = ndo->ndo_pcap;

	pktp_hdr = (struct pktap_header *)sp;

	if (h->len < sizeof(struct pktap_header) ||
		h->caplen < sizeof(struct pktap_header) ||
		pktp_hdr->pth_length > h->caplen) {
		return (-1);
	}
	/*
	 * Filter on packet metadata
	 */
	if (pkt_meta_data_expression != NULL) {
		struct pkt_meta_data pmd = {};

		pmd.itf = &pktp_hdr->pth_ifname[0];
		pmd.dlt = pktp_hdr->pth_dlt;
		pmd.proc = &pktp_hdr->pth_comm[0];
		pmd.eproc = &pktp_hdr->pth_ecomm[0];
		pmd.pid = pktp_hdr->pth_pid;
		pmd.epid = pktp_hdr->pth_epid;
		pmd.svc = svc2str(pktp_hdr->pth_svc);
		pmd.dir = (pktp_hdr->pth_flags & PTH_FLAG_DIR_IN) ? "in" :
			(pktp_hdr->pth_flags & PTH_FLAG_DIR_OUT) ? "out" : "";
		pmd.flowid = pktp_hdr->pth_flowid;

		match = evaluate_expression(pkt_meta_data_expression, &pmd);
		if (match == 0) {
			packets_mtdt_fltr_drop++;

			return 0;
		}
	}

	if (if_info == NULL) {
		if_info = pcap_find_if_info_by_name(pcap, pktp_hdr->pth_ifname);
		/*
		 * New interface
		 */
		if (if_info == NULL) {
			if_info = pcap_add_if_info(pcap, pktp_hdr->pth_ifname, -1,
						   pktp_hdr->pth_dlt, ndo->ndo_snaplen);
			if (if_info == NULL) {
				fprintf(stderr, "%s: pcap_add_if_info(%s, %u) failed: %s\n",
					__func__, pktp_hdr->pth_ifname, pktp_hdr->pth_dlt, pcap_geterr(pcap));
				kill(getpid(), SIGTERM);
				return 0;
			}
		}
	}
	
	if (if_info->if_filter_program.bf_insns == NULL) {
		match = 1;
	} else {
		/*
		 * The actual data packet is past the packet tap header
		 */
		struct pcap_pkthdr tmp_hdr;
        
		bcopy(h, &tmp_hdr, sizeof(struct pcap_pkthdr));
        
		tmp_hdr.caplen -= pktp_hdr->pth_length;
		tmp_hdr.len -= pktp_hdr->pth_length;

		pkt_data = sp + pktp_hdr->pth_length;
        
		match = pcap_offline_filter(&if_info->if_filter_program, &tmp_hdr, pkt_data);
	}
	
	return (match);
}

/*
 * Returns:
 * -1: pktap header is truncated and cannot be parsed
 *  0: pktap header doesn't match
 *  other values: pktap header matches the filter
 */
int
pktapv2_filter_packet(netdissect_options *ndo, struct pcap_if_info *if_info,
					const struct pcap_pkthdr *h, const u_char *sp)
{
	struct pktap_v2_hdr *pktap_v2_hdr;
	const u_char *pkt_data = NULL;
	int match = 0;
	pcap_t *pcap = ndo->ndo_pcap;
	const char *ifname = NULL;
	const char *comm = NULL;
	const char *e_comm = NULL;

	pktap_v2_hdr = (struct pktap_v2_hdr *)sp;

	if (h->len < sizeof(struct pktap_v2_hdr) ||
	    h->caplen < sizeof(struct pktap_v2_hdr) ||
	    pktap_v2_hdr->pth_length > h->caplen) {
		return (-1);
	}

	ifname = ((char *) pktap_v2_hdr) + pktap_v2_hdr->pth_ifname_offset;
	if (pktap_v2_hdr->pth_ifname_offset == 0) {
		fprintf(stderr, "%s: interface name missing\n",
			__func__);
		return (0);
	}
	if (pktap_v2_hdr->pth_comm_offset != 0)
		comm = ((char *) pktap_v2_hdr) + pktap_v2_hdr->pth_comm_offset;
	if (pktap_v2_hdr->pth_e_comm_offset != 0)
		e_comm = ((char *) pktap_v2_hdr) + pktap_v2_hdr->pth_e_comm_offset;

	/*
	 * Filter on packet metadata
	 */
	if (match && pkt_meta_data_expression != NULL) {
		struct pkt_meta_data pmd = {};

		pmd.itf = ifname;
		pmd.dlt = pktap_v2_hdr->pth_dlt;
		pmd.proc = comm;
		pmd.eproc = e_comm;
		pmd.pid = pktap_v2_hdr->pth_pid;
		pmd.epid = pktap_v2_hdr->pth_e_pid;
		pmd.svc = svc2str(pktap_v2_hdr->pth_svc);
		pmd.dir = (pktap_v2_hdr->pth_flags & PTH_FLAG_DIR_IN) ? "in" :
			(pktap_v2_hdr->pth_flags & PTH_FLAG_DIR_OUT) ? "out" : "";
		pmd.flowid = pktap_v2_hdr->pth_flowid;

		match = evaluate_expression(pkt_meta_data_expression, &pmd);
		if (match == 0) {
			packets_mtdt_fltr_drop++;
			
			return 0;
		}
	}

	if (if_info == NULL) {
		if_info = pcap_find_if_info_by_name(pcap, ifname);
		/*
		 * New interface
		 */
		if (if_info == NULL) {
			if_info = pcap_add_if_info(pcap, ifname, -1,
						   pktap_v2_hdr->pth_dlt, ndo->ndo_snaplen);
			if (if_info == NULL) {
				fprintf(stderr, "%s: pcap_add_if_info(%s, %u) failed: %s\n",
					__func__, ifname, pktap_v2_hdr->pth_dlt, pcap_geterr(pcap));
				kill(getpid(), SIGTERM);
				return 0;
			}
		}
	}

	if (if_info->if_filter_program.bf_insns == NULL) {
		match = 1;
	} else {
		/*
		 * The actual data packet is past the packet tap header
		 */
		struct pcap_pkthdr tmp_hdr;

		bcopy(h, &tmp_hdr, sizeof(struct pcap_pkthdr));

		tmp_hdr.caplen -= pktap_v2_hdr->pth_length;
		tmp_hdr.len -= pktap_v2_hdr->pth_length;

		pkt_data = sp + pktap_v2_hdr->pth_length;

		match = pcap_offline_filter(&if_info->if_filter_program, &tmp_hdr, pkt_data);
	}

	return (match);
}


char *
svc2str(uint32_t svc)
{
	static char svcstr[10];

	switch (svc) {
		case SO_TC_BK_SYS:
			return "BK_SYS";
		case SO_TC_BK:
			return "BK";
		case SO_TC_BE:
			return "BE";
		case SO_TC_RD:
			return "RD";
		case SO_TC_OAM:
			return "OAM";
		case SO_TC_AV:
			return "AV";
		case SO_TC_RV:
			return "RV";
		case SO_TC_VI:
			return "VI";
		case SO_TC_VO:
			return "VO";
		case SO_TC_CTL:
			return "CTL";
		default:
			snprintf(svcstr, sizeof(svcstr), "%u", svc);
			return svcstr;
	}
}

bool os_log_inited = false;

struct nt_fun_cookie {
	FILE *fun_file;
	FILE *fun_old_file;
	char fun_buf[BUFSIZ];
};

struct nt_fun_cookie fun_err = {
	.fun_file = NULL,
	.fun_old_file = NULL,
};

static int
nt_fun_write(void *cookie, const char *buf, const int buflen)
{
	struct nt_fun_cookie *nt_fun_cookie = (struct nt_fun_cookie *)cookie;
	int consumed = 0;

	/*
	 * Use os_log first as only stdio cares about truncation
	 */
	if (buf != nt_fun_cookie->fun_buf) {
		__stderrp = fun_err.fun_old_file;
		err(EX_SOFTWARE, "nt_fun_write: buf %p != fun_buf %p", buf, nt_fun_cookie->fun_buf);
	}

	/* we don't care if the string has been truncated */
	char *eol_ptr = strchr(nt_fun_cookie->fun_buf, '\n');
	if (eol_ptr != NULL) {
		char saved = *eol_ptr;
		*eol_ptr = '\0';
		consumed = buflen;
		os_log(OS_LOG_DEFAULT, "%s", nt_fun_cookie->fun_buf);
		*eol_ptr = saved;
	}

	ssize_t n = write(STDERR_FILENO, buf, buflen);
	if (n > 0) {
		consumed = n < buflen ? buflen - (int)n : buflen;
	}
	return consumed;
}

static int
nt_fun_close(void *cookie)
{
	struct nt_fun_cookie *nt_fun_cookie = (struct nt_fun_cookie *)cookie;

	if (nt_fun_cookie->fun_old_file != NULL) {
		__stderrp = nt_fun_cookie->fun_old_file;
		nt_fun_cookie->fun_old_file = NULL;
	}

	nt_fun_cookie->fun_file = NULL;

	return 0;
}

bool
darwin_log_init(void)
{
	if (os_log_inited == false) {
		fun_err.fun_file = funopen(&fun_err, NULL, nt_fun_write, NULL, nt_fun_close);
		if (fun_err.fun_file == NULL) {
			warn("%s: funopen() failed", __func__);
			goto  failed;
		}
		setvbuf(fun_err.fun_file, fun_err.fun_buf, _IOLBF, sizeof(fun_err.fun_buf));

		fun_err.fun_old_file = __stderrp;
		__stderrp = fun_err.fun_file;

		os_log_inited = true;
	}
	return true;
failed:
	if (fun_err.fun_file != NULL) {
		fclose(fun_err.fun_file);
	}
	return false;
}

#endif /* __APPLE__ */
