/*
 * $Id$
 *
 * sip msg. header proxy parser
 *
 *
 * Copyright (C) 2001-2003 FhG Fokus
 *
 * This file is part of ser, a free SIP server.
 *
 * ser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * For a license to use the ser software under conditions
 * other than those described here, or to purchase support for this
 * software, please contact iptel.org by e-mail at the following addresses:
 *    info@iptel.org
 *
 * ser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * ---------
 *  2003-02-28  scratchpad compatibility abandoned (jiri)
 *  2003-01-29  scrathcpad removed (jiri)
 *  2003-01-27  next baby-step to removing ZT - PRESERVE_ZT (jiri)
 *  2003-03-31  removed msg->repl_add_rm (andrei)
 *  2003-04-26 ZSW (jiri)
 *  2003-05-01  parser extended to support Accept header field (janakj)
 *  2005-02-23  parse_headers uses hdr_flags_t now (andrei)
 *  2005-03-02  free_via_list(vb) on via parse error (andrei)
 *  2007-01-26  parser extended to support Identity, Identity-info and Date
 *		header fields (gergo)
 */

/** Parser :: SIP Message header proxy parser.
 * @file
 * @ingroup parser
 */

/*! \defgroup parser SIP-router SIP message parser
 * 
 * The SIP message parser
 *
 */


#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#include "../comp_defs.h"
#include "msg_parser.h"
#include "parser_f.h"
#include "../ut.h"
#include "../error.h"
#include "../dprint.h"
#include "../data_lump_rpl.h"
#include "../mem/mem.h"
#include "../error.h"
#include "../core_stats.h"
#include "../globals.h"
#include "parse_hname2.h"
#include "parse_uri.h"
#include "parse_content.h"
#include "parse_to.h"
#include "../compiler_opt.h"

#ifdef DEBUG_DMALLOC
#include <mem/dmalloc.h>
#endif


#define parse_hname(_b,_e,_h) parse_hname2((_b),(_e),(_h))

/* number of via's encountered */
int via_cnt;
/* global request flags */
unsigned int global_req_flags = 0;

/* returns pointer to next header line, and fill hdr_f ;
 * if at end of header returns pointer to the last crlf  (always buf)*/
char* get_hdr_field(char* buf, char* end, struct hdr_field* hdr)
{

	char *tmp = 0;
	char *match;
	struct via_body *vb;
	struct cseq_body* cseq_b;
	struct to_body* to_b;
	int integer, err;
	unsigned uval;

	if ((*buf)=='\n' || (*buf)=='\r'){
		/* double crlf or lflf or crcr */
		DBG("found end of header\n");
		hdr->type=HDR_EOH_T;
		return buf;
	}

	tmp=parse_hname(buf, end, hdr);
	if (hdr->type==HDR_ERROR_T){
		LOG(L_ERR, "ERROR: get_hdr_field: bad header\n");
		goto error;
	}

	/* eliminate leading whitespace */
	tmp=eat_lws_end(tmp, end);
	if (tmp>=end) {
		LOG(L_ERR, "ERROR: get_hdr_field: HF empty\n");
		goto error;
	}

	/* if header-field well-known, parse it, find its end otherwise ;
	 * after leaving the hdr->type switch, tmp should be set to the
	 * next header field
	 */
	switch(hdr->type){
		case HDR_VIA_T:
			/* keep number of vias parsed -- we want to report it in
			   replies for diagnostic purposes */
			via_cnt++;
			vb=pkg_malloc(sizeof(struct via_body));
			if (vb==0){
				LOG(L_ERR, "get_hdr_field: out of memory\n");
				goto error;
			}
			memset(vb,0,sizeof(struct via_body));
			hdr->body.s=tmp;
			tmp=parse_via(tmp, end, vb);
			if (vb->error==PARSE_ERROR){
				LOG(L_ERR, "ERROR: get_hdr_field: bad via\n");
				free_via_list(vb);
				goto error;
			}
			hdr->parsed=vb;
			vb->hdr.s=hdr->name.s;
			vb->hdr.len=hdr->name.len;
			hdr->body.len=tmp-hdr->body.s;
			break;
		case HDR_CSEQ_T:
			cseq_b=pkg_malloc(sizeof(struct cseq_body));
			if (cseq_b==0){
				LOG(L_ERR, "get_hdr_field: out of memory\n");
				goto error;
			}
			memset(cseq_b, 0, sizeof(struct cseq_body));
			hdr->body.s=tmp;
			tmp=parse_cseq(tmp, end, cseq_b);
			if (cseq_b->error==PARSE_ERROR){
				LOG(L_ERR, "ERROR: get_hdr_field: bad cseq\n");
				free_cseq(cseq_b);
				goto error;
			}
			hdr->parsed=cseq_b;
			hdr->body.len=tmp-hdr->body.s;
			DBG("get_hdr_field: cseq <%.*s>: <%.*s> <%.*s>\n",
					hdr->name.len, ZSW(hdr->name.s),
					cseq_b->number.len, ZSW(cseq_b->number.s),
					cseq_b->method.len, cseq_b->method.s);
			break;
		case HDR_TO_T:
			to_b=pkg_malloc(sizeof(struct to_body));
			if (to_b==0){
				LOG(L_ERR, "get_hdr_field: out of memory\n");
				goto error;
			}
			memset(to_b, 0, sizeof(struct to_body));
			hdr->body.s=tmp;
			tmp=parse_to(tmp, end,to_b);
			if (to_b->error==PARSE_ERROR){
				LOG(L_ERR, "ERROR: get_hdr_field: bad to header\n");
				free_to(to_b);
				goto error;
			}
			hdr->parsed=to_b;
			hdr->body.len=tmp-hdr->body.s;
			DBG("DEBUG: get_hdr_field: <%.*s> [%d]; uri=[%.*s] \n",
				hdr->name.len, ZSW(hdr->name.s),
				hdr->body.len, to_b->uri.len,ZSW(to_b->uri.s));
			DBG("DEBUG: to body [%.*s]\n",to_b->body.len,
				ZSW(to_b->body.s));
			break;
		case HDR_CONTENTLENGTH_T:
			hdr->body.s=tmp;
			tmp=parse_content_length(tmp,end, &integer);
			if (tmp==0){
				LOG(L_ERR, "ERROR:get_hdr_field: bad content_length header\n");
				goto error;
			}
			hdr->parsed=(void*)(long)integer;
			hdr->body.len=tmp-hdr->body.s;
			DBG("DEBUG: get_hdr_body : content_length=%d\n",
					(int)(long)hdr->parsed);
			break;
		case HDR_RETRY_AFTER_T:
			hdr->body.s=tmp;
			tmp=parse_retry_after(tmp,end, &uval, &err);
			if (err){
				LOG(L_ERR, "ERROR:get_hdr_field: bad retry_after header\n");
				goto error;
			}
			hdr->parsed=(void*)(unsigned long)uval;
			hdr->body.len=tmp-hdr->body.s;
			DBG("DEBUG: get_hdr_body : retry_after=%d\n",
					(unsigned)(long)hdr->parsed);
			break;
		case HDR_IDENTITY_T:
		case HDR_DATE_T:
		case HDR_IDENTITY_INFO_T:
		case HDR_SUPPORTED_T:
		case HDR_REQUIRE_T:
		case HDR_CONTENTTYPE_T:
		case HDR_FROM_T:
		case HDR_CALLID_T:
		case HDR_CONTACT_T:
		case HDR_ROUTE_T:
		case HDR_RECORDROUTE_T:
		case HDR_MAXFORWARDS_T:
		case HDR_AUTHORIZATION_T:
		case HDR_EXPIRES_T:
		case HDR_PROXYAUTH_T:
		case HDR_PROXYREQUIRE_T:
		case HDR_UNSUPPORTED_T:
		case HDR_ALLOW_T:
		case HDR_EVENT_T:
		case HDR_ACCEPT_T:
		case HDR_ACCEPTLANGUAGE_T:
		case HDR_ORGANIZATION_T:
		case HDR_PRIORITY_T:
		case HDR_SUBJECT_T:
		case HDR_USERAGENT_T:
		case HDR_SERVER_T:
		case HDR_CONTENTDISPOSITION_T:
		case HDR_DIVERSION_T:
		case HDR_RPID_T:
		case HDR_SIPIFMATCH_T:
		case HDR_REFER_TO_T:
		case HDR_SESSIONEXPIRES_T:
		case HDR_MIN_SE_T:
		case HDR_SUBSCRIPTION_STATE_T:
		case HDR_ACCEPTCONTACT_T:
		case HDR_ALLOWEVENTS_T:
		case HDR_CONTENTENCODING_T:
		case HDR_REFERREDBY_T:
		case HDR_REJECTCONTACT_T:
		case HDR_REQUESTDISPOSITION_T:
		case HDR_WWW_AUTHENTICATE_T:
		case HDR_PROXY_AUTHENTICATE_T:
		case HDR_PATH_T:
		case HDR_PRIVACY_T:
		case HDR_PAI_T:
		case HDR_PPI_T:
		case HDR_REASON_T:
		case HDR_OTHER_T:
			/* just skip over it */
			hdr->body.s=tmp;
			/* find end of header */
			/* find lf */
			do{
				match=q_memchr(tmp, '\n', end-tmp);
				if (match){
					match++;
				}else {
					LOG(L_ERR,
							"ERROR: get_hdr_field: bad body for <%s>(%d)\n",
							hdr->name.s, hdr->type);
					/* abort(); */
					tmp=end;
					goto error;
				}
				tmp=match;
			}while( match<end &&( (*match==' ')||(*match=='\t') ) );
			tmp=match;
			hdr->body.len=match-hdr->body.s;
			break;
		default:
			LOG(L_CRIT, "BUG: get_hdr_field: unknown header type %d\n",
					hdr->type);
			goto error;
	}
	/* jku: if \r covered by current length, shrink it */
	trim_r( hdr->body );
	hdr->len=tmp-hdr->name.s;
	return tmp;
error:
	DBG("get_hdr_field: error exit\n");
	STATS_BAD_MSG_HDR();
	hdr->type=HDR_ERROR_T;
	hdr->len=tmp-hdr->name.s;
	return tmp;
}



/* parse the headers and adds them to msg->headers and msg->to, from etc.
 * It stops when all the headers requested in flags were parsed, on error
 * (bad header) or end of headers
 * WARNING: parse_headers was changed to use hdr_flags_t (the flags are now
 *          different from the header types). Don't call it with a header type
 *          (HDR_xxx_T), only with header flags (HDR_xxx_F)!*/
/* note: it continues where it previously stopped and goes ahead until
   end is encountered or desired HFs are found; if you call it twice
   for the same HF which is present only once, it will fail the second
   time; if you call it twice and the HF is found on second time too,
   it's not replaced in the well-known HF pointer but just added to
   header list; if you want to use a dumb convenience function which will
   give you the first occurrence of a header you are interested in,
   look at check_transaction_quadruple
*/
int parse_headers(struct sip_msg* msg, hdr_flags_t flags, int next)
{
	struct hdr_field* hf;
	char* tmp;
	char* rest;
	char* end;
	hdr_flags_t orig_flag;

	end=msg->buf+msg->len;
	tmp=msg->unparsed;

	if (unlikely(next)) {
		orig_flag = msg->parsed_flag;
		msg->parsed_flag &= ~flags;
	}else
		orig_flag=0;

#ifdef EXTRA_DEBUG
	DBG("parse_headers: flags=%llx\n", (unsigned long long)flags);
#endif
	while( tmp<end && (flags & msg->parsed_flag) != flags){
		prefetch_loc_r(tmp+64, 1);
		hf=pkg_malloc(sizeof(struct hdr_field));
		if (unlikely(hf==0)){
			ser_error=E_OUT_OF_MEM;
			LOG(L_ERR, "ERROR:parse_headers: memory allocation error\n");
			goto error;
		}
		memset(hf,0, sizeof(struct hdr_field));
		hf->type=HDR_ERROR_T;
		rest=get_hdr_field(tmp, end, hf);
		switch (hf->type){
			case HDR_ERROR_T:
				LOG(L_INFO,"ERROR: bad header field [%.*s]\n",
					(end-tmp>20)?20:(int)(end-tmp), tmp);
				goto  error;
			case HDR_EOH_T:
				msg->eoh=tmp; /* or rest?*/
				msg->parsed_flag|=HDR_EOH_F;
				pkg_free(hf);
				goto skip;
			case HDR_ACCEPTCONTACT_T:
			case HDR_ALLOWEVENTS_T:
			case HDR_CONTENTENCODING_T:
			case HDR_REFERREDBY_T:
			case HDR_REJECTCONTACT_T:
			case HDR_REQUESTDISPOSITION_T:
			case HDR_WWW_AUTHENTICATE_T:
			case HDR_PROXY_AUTHENTICATE_T:
			case HDR_RETRY_AFTER_T:
			case HDR_OTHER_T: /* mark the type as found/parsed*/
				msg->parsed_flag|=HDR_T2F(hf->type);
				break;
			case HDR_CALLID_T:
				if (msg->callid==0) msg->callid=hf;
				msg->parsed_flag|=HDR_CALLID_F;
				break;
			case HDR_SIPIFMATCH_T:
				if (msg->sipifmatch==0) msg->sipifmatch=hf;
				msg->parsed_flag|=HDR_SIPIFMATCH_F;
				break;
			case HDR_TO_T:
				if (msg->to==0) msg->to=hf;
				msg->parsed_flag|=HDR_TO_F;
				break;
			case HDR_CSEQ_T:
				if (msg->cseq==0) msg->cseq=hf;
				msg->parsed_flag|=HDR_CSEQ_F;
				break;
			case HDR_FROM_T:
				if (msg->from==0) msg->from=hf;
				msg->parsed_flag|=HDR_FROM_F;
				break;
			case HDR_CONTACT_T:
				if (msg->contact==0) msg->contact=hf;
				msg->parsed_flag|=HDR_CONTACT_F;
				break;
			case HDR_MAXFORWARDS_T:
				if(msg->maxforwards==0) msg->maxforwards=hf;
				msg->parsed_flag|=HDR_MAXFORWARDS_F;
				break;
			case HDR_ROUTE_T:
				if (msg->route==0) msg->route=hf;
				msg->parsed_flag|=HDR_ROUTE_F;
				break;
			case HDR_RECORDROUTE_T:
				if (msg->record_route==0) msg->record_route = hf;
				msg->parsed_flag|=HDR_RECORDROUTE_F;
				break;
			case HDR_CONTENTTYPE_T:
				if (msg->content_type==0) msg->content_type = hf;
				msg->parsed_flag|=HDR_CONTENTTYPE_F;
				break;
			case HDR_CONTENTLENGTH_T:
				if (msg->content_length==0) msg->content_length = hf;
				msg->parsed_flag|=HDR_CONTENTLENGTH_F;
				break;
			case HDR_AUTHORIZATION_T:
				if (msg->authorization==0) msg->authorization = hf;
				msg->parsed_flag|=HDR_AUTHORIZATION_F;
				break;
			case HDR_EXPIRES_T:
				if (msg->expires==0) msg->expires = hf;
				msg->parsed_flag|=HDR_EXPIRES_F;
				break;
			case HDR_PROXYAUTH_T:
				if (msg->proxy_auth==0) msg->proxy_auth = hf;
				msg->parsed_flag|=HDR_PROXYAUTH_F;
				break;
			case HDR_PROXYREQUIRE_T:
				if (msg->proxy_require==0) msg->proxy_require = hf;
				msg->parsed_flag|=HDR_PROXYREQUIRE_F;
				break;
			case HDR_SUPPORTED_T:
				if (msg->supported==0) msg->supported=hf;
				msg->parsed_flag|=HDR_SUPPORTED_F;
				break;
			case HDR_REQUIRE_T:
				if (msg->require==0) msg->require=hf;
				msg->parsed_flag|=HDR_REQUIRE_F;
				break;
			case HDR_UNSUPPORTED_T:
				if (msg->unsupported==0) msg->unsupported=hf;
				msg->parsed_flag|=HDR_UNSUPPORTED_F;
				break;
			case HDR_ALLOW_T:
				if (msg->allow==0) msg->allow = hf;
				msg->parsed_flag|=HDR_ALLOW_F;
				break;
			case HDR_EVENT_T:
				if (msg->event==0) msg->event = hf;
				msg->parsed_flag|=HDR_EVENT_F;
				break;
			case HDR_ACCEPT_T:
				if (msg->accept==0) msg->accept = hf;
				msg->parsed_flag|=HDR_ACCEPT_F;
				break;
			case HDR_ACCEPTLANGUAGE_T:
				if (msg->accept_language==0) msg->accept_language = hf;
				msg->parsed_flag|=HDR_ACCEPTLANGUAGE_F;
				break;
			case HDR_ORGANIZATION_T:
				if (msg->organization==0) msg->organization = hf;
				msg->parsed_flag|=HDR_ORGANIZATION_F;
				break;
			case HDR_PRIORITY_T:
				if (msg->priority==0) msg->priority = hf;
				msg->parsed_flag|=HDR_PRIORITY_F;
				break;
			case HDR_SUBJECT_T:
				if (msg->subject==0) msg->subject = hf;
				msg->parsed_flag|=HDR_SUBJECT_F;
				break;
			case HDR_USERAGENT_T:
				if (msg->user_agent==0) msg->user_agent = hf;
				msg->parsed_flag|=HDR_USERAGENT_F;
				break;
			case HDR_SERVER_T:
				if (msg->server==0) msg->server = hf;
				msg->parsed_flag|=HDR_SERVER_F;
				break;
			case HDR_CONTENTDISPOSITION_T:
				if (msg->content_disposition==0) msg->content_disposition = hf;
				msg->parsed_flag|=HDR_CONTENTDISPOSITION_F;
				break;
			case HDR_DIVERSION_T:
				if (msg->diversion==0) msg->diversion = hf;
				msg->parsed_flag|=HDR_DIVERSION_F;
				break;
			case HDR_RPID_T:
				if (msg->rpid==0) msg->rpid = hf;
				msg->parsed_flag|=HDR_RPID_F;
				break;
			case HDR_REFER_TO_T:
				if (msg->refer_to==0) msg->refer_to = hf;
				msg->parsed_flag|=HDR_REFER_TO_F;
				break;
			case HDR_SESSIONEXPIRES_T:
				if (msg->session_expires==0) msg->session_expires = hf;
				msg->parsed_flag|=HDR_SESSIONEXPIRES_F;
				break;
			case HDR_MIN_SE_T:
				if (msg->min_se==0) msg->min_se = hf;
				msg->parsed_flag|=HDR_MIN_SE_F;
				break;
			case HDR_SUBSCRIPTION_STATE_T:
				if (msg->subscription_state==0) msg->subscription_state = hf;
				msg->parsed_flag|=HDR_SUBSCRIPTION_STATE_F;
				break;
			case HDR_VIA_T:
				msg->parsed_flag|=HDR_VIA_F;
				DBG("parse_headers: Via found, flags=%llx\n",
						(unsigned long long)flags);
				if (msg->via1==0) {
					DBG("parse_headers: this is the first via\n");
					msg->h_via1=hf;
					msg->via1=hf->parsed;
					if (msg->via1->next){
						msg->via2=msg->via1->next;
						msg->parsed_flag|=HDR_VIA2_F;
					}
				}else if (msg->via2==0){
					msg->h_via2=hf;
					msg->via2=hf->parsed;
					msg->parsed_flag|=HDR_VIA2_F;
					DBG("parse_headers: this is the second via\n");
				}
				break;
			case HDR_DATE_T:
				if (msg->date==0) msg->date=hf;
				msg->parsed_flag|=HDR_DATE_F;
				break;
			case HDR_IDENTITY_T:
				if (msg->identity==0) msg->identity=hf;
				msg->parsed_flag|=HDR_IDENTITY_F;
				break;
			case HDR_IDENTITY_INFO_T:
				if (msg->identity_info==0) msg->identity_info=hf;
				msg->parsed_flag|=HDR_IDENTITY_INFO_F;
				break;
		    case HDR_PATH_T:
				if (msg->path==0) msg->path=hf;
				msg->parsed_flag|=HDR_PATH_F;
				break;
		    case HDR_PRIVACY_T:
				if (msg->privacy==0) msg->privacy=hf;
				msg->parsed_flag|=HDR_PRIVACY_F;
				break;
		    case HDR_PAI_T:
				if (msg->pai==0) msg->pai=hf;
				msg->parsed_flag|=HDR_PAI_F;
				break;
		    case HDR_PPI_T:
				if (msg->ppi==0) msg->ppi=hf;
				msg->parsed_flag|=HDR_PPI_F;
				break;
		    case HDR_REASON_T:
				msg->parsed_flag|=HDR_REASON_F;
				break;
			default:
				LOG(L_CRIT, "BUG: parse_headers: unknown header type %d\n",
							hf->type);
				goto error;
		}
		/* add the header to the list*/
		if (msg->last_header==0){
			msg->headers=hf;
			msg->last_header=hf;
		}else{
			msg->last_header->next=hf;
			msg->last_header=hf;
		}
#ifdef EXTRA_DEBUG
		DBG("header field type %d, name=<%.*s>, body=<%.*s>\n",
			hf->type,
			hf->name.len, ZSW(hf->name.s),
			hf->body.len, ZSW(hf->body.s));
#endif
		tmp=rest;
	}
skip:
	msg->unparsed=tmp;
	/* restore original flags */
	msg->parsed_flag |= orig_flag;
	return 0;

error:
	ser_error=E_BAD_REQ;
	if (hf) pkg_free(hf);
	/* restore original flags */
	msg->parsed_flag |= orig_flag;
	return -1;
}





/* returns 0 if ok, -1 for errors */
int parse_msg(char* buf, unsigned int len, struct sip_msg* msg)
{

	char *tmp;
	char* rest;
	struct msg_start *fl;
	int offset;
	hdr_flags_t flags;

	/* eat crlf from the beginning */
	for (tmp=buf; (*tmp=='\n' || *tmp=='\r')&&
			tmp-buf < len ; tmp++);
	offset=tmp-buf;
	fl=&(msg->first_line);
	rest=parse_first_line(tmp, len-offset, fl);
#if 0
	rest=parse_fline(tmp, buf+len, fl);
#endif
	offset+=rest-tmp;
	tmp=rest;
	switch(fl->type){
		case SIP_INVALID:
			DBG("parse_msg: invalid message\n");
			goto error;
			break;
		case SIP_REQUEST:
			DBG("SIP Request:\n");
			DBG(" method:  <%.*s>\n",fl->u.request.method.len,
				ZSW(fl->u.request.method.s));
			DBG(" uri:     <%.*s>\n",fl->u.request.uri.len,
				ZSW(fl->u.request.uri.s));
			DBG(" version: <%.*s>\n",fl->u.request.version.len,
				ZSW(fl->u.request.version.s));
			flags=HDR_VIA_F;
			break;
		case SIP_REPLY:
			DBG("SIP Reply  (status):\n");
			DBG(" version: <%.*s>\n",fl->u.reply.version.len,
					ZSW(fl->u.reply.version.s));
			DBG(" status:  <%.*s>\n", fl->u.reply.status.len,
					ZSW(fl->u.reply.status.s));
			DBG(" reason:  <%.*s>\n", fl->u.reply.reason.len,
					ZSW(fl->u.reply.reason.s));
			/* flags=HDR_VIA | HDR_VIA2; */
			/* we don't try to parse VIA2 for local messages; -Jiri */
			flags=HDR_VIA_F;
			break;
		default:
			DBG("unknown type %d\n",fl->type);
			goto error;
	}
	msg->unparsed=tmp;
	/*find first Via: */
	if (parse_headers(msg, flags, 0)==-1) goto error;

#ifdef EXTRA_DEBUG
	/* dump parsed data */
	if (msg->via1){
		DBG("first via: <%.*s/%.*s/%.*s> <%.*s:%.*s(%d)>",
			msg->via1->name.len,
			ZSW(msg->via1->name.s),
			msg->via1->version.len,
			ZSW(msg->via1->version.s),
			msg->via1->transport.len,
			ZSW(msg->via1->transport.s),
			msg->via1->host.len,
			ZSW(msg->via1->host.s),
			msg->via1->port_str.len,
			ZSW(msg->via1->port_str.s),
			msg->via1->port);
		if (msg->via1->params.s)  DBG(";<%.*s>",
				msg->via1->params.len, ZSW(msg->via1->params.s));
		if (msg->via1->comment.s)
				DBG(" <%.*s>",
					msg->via1->comment.len, ZSW(msg->via1->comment.s));
		DBG ("\n");
	}
	if (msg->via2){
		DBG("second via: <%.*s/%.*s/%.*s> <%.*s:%.*s(%d)>",
			msg->via2->name.len,
			ZSW(msg->via2->name.s),
			msg->via2->version.len,
			ZSW(msg->via2->version.s),
			msg->via2->transport.len,
			ZSW(msg->via2->transport.s),
			msg->via2->host.len,
			ZSW(msg->via2->host.s),
			msg->via2->port_str.len,
			ZSW(msg->via2->port_str.s),
			msg->via2->port);
		if (msg->via2->params.s)  DBG(";<%.*s>",
				msg->via2->params.len, ZSW(msg->via2->params.s));
		if (msg->via2->comment.s) DBG(" <%.*s>",
				msg->via2->comment.len, ZSW(msg->via2->comment.s));
		DBG ("\n");
	}
#endif


#ifdef EXTRA_DEBUG
	DBG("exiting parse_msg\n");
#endif

	return 0;

error:
	/* more debugging, msg->orig is/should be null terminated*/
	LOG(cfg_get(core, core_cfg, corelog), "ERROR: parse_msg: message=<%.*s>\n",
			(int)msg->len, ZSW(msg->buf));
	return -1;
}



void free_reply_lump( struct lump_rpl *lump)
{
	struct lump_rpl *foo, *bar;
	for(foo=lump;foo;)
	{
		bar=foo->next;
		free_lump_rpl(foo);
		foo = bar;
	}
}


/*only the content*/
void free_sip_msg(struct sip_msg* msg)
{
	if (msg->new_uri.s) { pkg_free(msg->new_uri.s); msg->new_uri.len=0; }
	if (msg->dst_uri.s) { pkg_free(msg->dst_uri.s); msg->dst_uri.len=0; }
	if (msg->path_vec.s) { pkg_free(msg->path_vec.s); msg->path_vec.len=0; }
	if (msg->headers)     free_hdr_field_lst(msg->headers);
	if (msg->body && msg->body->free) msg->body->free(&msg->body);
	if (msg->add_rm)      free_lump_list(msg->add_rm);
	if (msg->body_lumps)  free_lump_list(msg->body_lumps);
	if (msg->reply_lump)   free_reply_lump(msg->reply_lump);
	/* don't free anymore -- now a pointer to a static buffer */
#	ifdef DYN_BUF
	pkg_free(msg->buf);
#	endif
}


/*
 * Make a private copy of the string and assign it to dst_uri
 */
int set_dst_uri(struct sip_msg* msg, str* uri)
{
	char* ptr;

	if (unlikely(!msg || !uri)) {
		LOG(L_ERR, "set_dst_uri: Invalid parameter value\n");
		return -1;
	}

	if (unlikely(uri->len == 0)) {
		reset_dst_uri(msg);
	}else if (msg->dst_uri.s && (msg->dst_uri.len >= uri->len)) {
		memcpy(msg->dst_uri.s, uri->s, uri->len);
		msg->dst_uri.len = uri->len;
	} else {
		ptr = (char*)pkg_malloc(uri->len);
		if (!ptr) {
			LOG(L_ERR, "set_dst_uri: Not enough memory\n");
			return -1;
		}

		memcpy(ptr, uri->s, uri->len);
		if (msg->dst_uri.s) pkg_free(msg->dst_uri.s);
		msg->dst_uri.s = ptr;
		msg->dst_uri.len = uri->len;
	}
	return 0;
}


void reset_dst_uri(struct sip_msg* msg)
{
	if(msg->dst_uri.s != 0) {
		pkg_free(msg->dst_uri.s);
	}
	msg->dst_uri.s = 0;
	msg->dst_uri.len = 0;
}

int set_path_vector(struct sip_msg* msg, str* path)
{
	char* ptr;

	if (unlikely(!msg || !path)) {
		LM_ERR("invalid parameter value\n");
		return -1;
	}

	if (unlikely(path->len == 0)) {
		reset_path_vector(msg);
	} else if (msg->path_vec.s && (msg->path_vec.len >= path->len)) {
		memcpy(msg->path_vec.s, path->s, path->len);
		msg->path_vec.len = path->len;
	} else {
		ptr = (char*)pkg_malloc(path->len);
		if (!ptr) {
			LM_ERR("not enough pkg memory\n");
			return -1;
		}

		memcpy(ptr, path->s, path->len);
		if (msg->path_vec.s) pkg_free(msg->path_vec.s);
		msg->path_vec.s = ptr;
		msg->path_vec.len = path->len;
	}
	return 0;
}


void reset_path_vector(struct sip_msg* msg)
{
	if(msg->path_vec.s != 0) {
		pkg_free(msg->path_vec.s);
	}
	msg->path_vec.s = 0;
	msg->path_vec.len = 0;
}


hdr_field_t* get_hdr(sip_msg_t *msg, enum _hdr_types_t ht)
{
	hdr_field_t *hdr;

	if (msg->parsed_flag & HDR_T2F(ht))
		for(hdr = msg->headers; hdr; hdr = hdr->next) {
			if(hdr->type == ht) return hdr;
		}
	return NULL;
}


hdr_field_t* next_sibling_hdr(hdr_field_t *hf)
{
	hdr_field_t *hdr;

	for(hdr = hf->next; hdr; hdr = hdr->next) {
		if(hdr->type == hf->type) return hdr;
	}
	return NULL;
}

hdr_field_t* get_hdr_by_name(sip_msg_t *msg, char *name, int name_len)
{
	hdr_field_t *hdr;

	for(hdr = msg->headers; hdr; hdr = hdr->next) {
		if(hdr->name.len == name_len && *hdr->name.s==*name
				&& strncmp(hdr->name.s, name, name_len)==0)
			return hdr;
	}
	return NULL;
}


hdr_field_t* next_sibling_hdr_by_name(hdr_field_t *hf)
{
	hdr_field_t *hdr;

	for(hdr = hf->next; hdr; hdr = hdr->next) {
		if(hdr->name.len == hf->name.len && *hdr->name.s==*hf->name.s
				&& strncmp(hdr->name.s, hf->name.s, hf->name.len)==0)
			return hdr;
	}
	return NULL;
}

/**
 * set msg context id
 * - return: -1 on error; 0 - on set
 */
int msg_ctx_id_set(sip_msg_t *msg, msg_ctx_id_t *mid)
{
	if(msg==NULL || mid==NULL)
		return -1;
	mid->msgid = msg->id;
	mid->pid = msg->pid;
	return 0;
}

/**
 * check msg context id
 * - return: -1 on error; 0 - on no match; 1 - on match
 */
int msg_ctx_id_match(sip_msg_t *msg, msg_ctx_id_t *mid)
{
	if(msg==NULL || mid==NULL)
		return -1;
	if(msg->id != mid->msgid || msg->pid!=mid->pid)
		return 0;
	return 1;
}

/**
 * set msg time value
 */
int msg_set_time(sip_msg_t *msg)
{
	if(unlikely(msg==NULL))
		return -2;
	if(msg->tval.tv_sec!=0)
		return 0;
	return gettimeofday(&msg->tval, NULL);
}
