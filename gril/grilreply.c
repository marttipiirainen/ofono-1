/*
 *
 *  RIL library with GLib integration
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
 *  Copyright (C) 2013 Jolla Ltd
 *  Contact: Jussi Kangas <jussi.kangas@tieto.com>
 *  Copyright (C) 2012-2014  Canonical Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <glib.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/call-forwarding.h>
#include <ofono/gprs-context.h>

#include "common.h"
#include "util.h"
#include "grilreply.h"
#include "grilutil.h"

#define OPERATOR_NUM_PARAMS 3

static void ril_reply_free_operator(gpointer data)
{
	struct reply_operator *reply = data;

	if (reply) {
		g_free(reply->lalpha);
		g_free(reply->salpha);
		g_free(reply->numeric);
		g_free(reply->status);
		g_free(reply);
	}
}

void g_ril_reply_free_avail_ops(struct reply_avail_ops *reply)
{
	if (reply) {
		g_slist_free_full(reply->list, ril_reply_free_operator);
		g_free(reply);
	}
}

struct reply_avail_ops *g_ril_reply_parse_avail_ops(GRil *gril,
						const struct ril_msg *message)
{
	struct parcel rilp;
	struct reply_operator *operator;
	struct reply_avail_ops *reply = NULL;
	unsigned int num_ops, num_strings;
	unsigned int i;
	int strings_per_opt;

	if (g_ril_vendor(gril) == OFONO_RIL_VENDOR_MTK)
		strings_per_opt = 5;
	else
		strings_per_opt = 4;

	/*
	 * Minimum message length is 4:
	 * - array size
	 */
	if (message->buf_len < 4) {
		ofono_error("%s: invalid QUERY_AVAIL_NETWORKS reply: "
				"size too small (< 4): %d ",
				__func__,
				(int) message->buf_len);
		goto error;
	}

	g_ril_init_parcel(message, &rilp);
	g_ril_append_print_buf(gril, "{");

	/* Number of operators at the list */
	num_strings = (unsigned int) parcel_r_int32(&rilp);
	if (num_strings % strings_per_opt) {
		ofono_error("%s: invalid QUERY_AVAIL_NETWORKS reply: "
				"num_strings (%d) MOD %d != 0",
				__func__,
				num_strings, strings_per_opt);
		goto error;
	}

	num_ops = num_strings / strings_per_opt;
	DBG("noperators = %d", num_ops);

	reply = g_try_new0(struct reply_avail_ops, 1);
	if (reply == NULL) {
		ofono_error("%s: can't allocate reply struct", __func__);
		goto error;
	}

	reply->num_ops = num_ops;
	for (i = 0; i < num_ops; i++) {
		operator = g_try_new0(struct reply_operator, 1);
		if (operator == NULL) {
			ofono_error("%s: can't allocate reply struct",
					__func__);
			goto error;
		}

		operator->lalpha = parcel_r_string(&rilp);
		operator->salpha = parcel_r_string(&rilp);
		operator->numeric = parcel_r_string(&rilp);
		operator->status = parcel_r_string(&rilp);

		/*
		 * MTK: additional string with technology: 2G/3G are the only
		 * valid values currently.
		 */
		if (g_ril_vendor(gril) == OFONO_RIL_VENDOR_MTK) {
			char *tech = parcel_r_string(&rilp);
			if (strcmp(tech, "3G") == 0)
				operator->tech = RADIO_TECH_UMTS;
			else
				operator->tech = RADIO_TECH_GSM;
			g_free(tech);
		} else {
			operator->tech = RADIO_TECH_GSM;
		}

		if (operator->lalpha == NULL && operator->salpha == NULL) {
			ofono_error("%s: operator (%s) doesn't specify names",
					operator->numeric,
					__func__);
			g_ril_reply_free_operator(operator);
			continue;
		}

		if (operator->numeric == NULL) {
			ofono_error("%s: operator (%s/%s) "
					"doesn't specify numeric",
					operator->lalpha,
					operator->salpha,
					__func__);
			g_ril_reply_free_operator(operator);
			continue;
		}

		if (operator->status == NULL) {
			ofono_error("%s: operator (%s/%s) "
					"doesn't specify status",
					operator->lalpha,
					operator->salpha,
					__func__);
			g_ril_reply_free_operator(operator);
			continue;
		}

		reply->list = g_slist_append(reply->list, operator);

		g_ril_append_print_buf(gril, "%s [lalpha=%s, salpha=%s, "
				" numeric=%s status=%s tech=%s]",
				print_buf,
				operator->lalpha,
				operator->salpha,
				operator->numeric,
				operator->status,
				ril_radio_tech_to_string(operator->tech));
	}

	g_ril_append_print_buf(gril, "%s}", print_buf);
	g_ril_print_response(gril, message);

	return reply;

error:
	if (reply)
		g_ril_reply_free_avail_ops(reply);

	return NULL;
}

void g_ril_reply_free_operator(struct reply_operator *reply)
{
	ril_reply_free_operator(reply);
}

struct reply_operator *g_ril_reply_parse_operator(GRil *gril,
						const struct ril_msg *message)
{
	struct parcel rilp;
	int num_params;
	struct reply_operator *reply = NULL;

	/*
	 * Minimum message length is 16:
	 * - array size
	 * - 3 NULL strings
	 */
	if (message->buf_len < 16) {
		ofono_error("%s: invalid OPERATOR reply: "
				"size too small (< 16): %d ",
				__func__,
				(int) message->buf_len);
		goto error;
	}

	g_ril_init_parcel(message, &rilp);

	if ((num_params = parcel_r_int32(&rilp)) != OPERATOR_NUM_PARAMS) {
		ofono_error("%s: invalid OPERATOR reply: "
				"number of params is %d; should be 3.",
				__func__,
				num_params);
		goto error;
	}

	reply =	g_new0(struct reply_operator, 1);

	reply->lalpha = parcel_r_string(&rilp);
	reply->salpha = parcel_r_string(&rilp);
	reply->numeric = parcel_r_string(&rilp);

	if (reply->lalpha == NULL && reply->salpha == NULL) {
		ofono_error("%s: invalid OPERATOR reply: "
				" no names returned.",
				__func__);

		goto error;
	}

	if (reply->numeric == NULL) {
		ofono_error("%s: invalid OPERATOR reply: "
				" no numeric returned.",
				__func__);
		goto error;
	}

	g_ril_append_print_buf(gril,
				"(lalpha=%s, salpha=%s, numeric=%s)",
				reply->lalpha, reply->salpha, reply->numeric);

	g_ril_print_response(gril, message);

	return reply;

error:
	if (reply)
		g_ril_reply_free_operator(reply);

	return NULL;
}

struct reply_reg_state *g_ril_reply_parse_reg_state(GRil *gril,
						const struct ril_msg *message)

{
	struct parcel rilp;
	int tmp;
	char *sstatus = NULL, *slac = NULL, *sci = NULL;
	char *stech = NULL, *sreason = NULL, *smax = NULL;
	struct reply_reg_state *reply;
	char *endp;

	DBG("");

	/*
	 * If no array length is present, reply is invalid.
	 */
	if (message->buf_len < 4) {
		ofono_error("%s: invalid %s reply: "
				"size too small (< 4): %d ",
				__func__,
				ril_request_id_to_string(message->req),
				(int) message->buf_len);
		return NULL;
	}

	g_ril_init_parcel(message, &rilp);

	reply =	g_new0(struct reply_reg_state, 1);

	/*
	 * Size of response string array
	 * Should be:
	 *   >= 4 for VOICE_REG reply
	 *   >= 5 for DATA_REG reply
	 * But we allow a minimum of 1 (infineon, not registered case)
	 */
	if ((tmp = parcel_r_int32(&rilp)) < 1) {
		ofono_error("%s: invalid %s; response array is too small: %d",
				__func__,
				ril_request_id_to_string(message->req),
				tmp);
		goto error;
	}

	sstatus = parcel_r_string(&rilp);
	--tmp;
	if (tmp > 0) {
		slac = parcel_r_string(&rilp);
		--tmp;
	}
	if (tmp > 0) {
		sci = parcel_r_string(&rilp);
		--tmp;
	}
	if (tmp > 0) {
		stech = parcel_r_string(&rilp);
		--tmp;
	}

	/*
	 * FIXME: need to review VOICE_REGISTRATION response
	 * as it returns ~15 parameters ( vs. 6 for DATA ).
	 *
	 * The first four parameters are the same for both
	 * responses ( although status includes values for
	 * emergency calls for VOICE response ).
	 *
	 * Parameters 5 & 6 have different meanings for
	 * voice & data response.
	 */
	if (tmp--) {
		sreason = parcel_r_string(&rilp);        /* TODO: different use for CDMA */

		if (tmp--) {
			smax = parcel_r_string(&rilp);           /* TODO: different use for CDMA */

			if (smax)
				reply->max_cids = atoi(smax);
		}
	}


	if (sstatus == NULL) {
		ofono_error("%s: no status included in %s reply",
				__func__,
				ril_request_id_to_string(message->req));
		goto error;
	}

	reply->status = atoi(sstatus);


	if (slac)
		reply->lac = strtol(slac, NULL, 16);
	else
		reply->lac = -1;

	if (sci)
		reply->ci = strtol(sci, NULL, 16);
	else
		reply->ci = -1;

	if (stech && *stech != '\0') {
		reply->tech = (int) strtol(stech, &endp, 10);
		if (*endp != '\0') {
			ofono_error("%s: cannot parse tech: %s in %s reply",
					__func__, stech,
					ril_request_id_to_string(message->req));
			reply->tech = RADIO_TECH_UNKNOWN;
		}
	} else {
		ofono_error("%s: no tech included in %s reply",
				__func__,
				ril_request_id_to_string(message->req));
		reply->tech = RADIO_TECH_UNKNOWN;
	}

	g_ril_append_print_buf(gril,
				"{%s,%s,%s,%s,%s,%s}",
				registration_status_to_string(reply->status),
				slac,
				sci,
				ril_radio_tech_to_string(reply->tech),
				sreason,
				smax);

	g_ril_print_response(gril, message);

	/* Free our parcel handlers */
	g_free(sstatus);
	g_free(slac);
	g_free(sci);
	g_free(stech);
	g_free(sreason);
	g_free(smax);

	return reply;

error:
	g_free(reply);
	return NULL;
}

void g_ril_reply_free_sim_io(struct reply_sim_io *reply)
{
	if (reply) {
		g_free(reply->hex_response);
		g_free(reply);
	}
}

struct reply_sim_io *g_ril_reply_parse_sim_io(GRil *gril,
						const struct ril_msg *message)
{
	struct parcel rilp;
	char *response = NULL;
	struct reply_sim_io *reply;

	/*
	 * Minimum length of SIM_IO_Response is 12:
	 * sw1 (int32)
	 * sw2 (int32)
	 * simResponse (string)
	 */
	if (message->buf_len < 12) {
		ofono_error("Invalid SIM IO reply: size too small (< 12): %d ",
			    (int) message->buf_len);
		return NULL;
	}

	reply =	g_new0(struct reply_sim_io, 1);

	g_ril_init_parcel(message, &rilp);
	reply->sw1 = parcel_r_int32(&rilp);
	reply->sw2 = parcel_r_int32(&rilp);

	response = parcel_r_string(&rilp);

	g_ril_append_print_buf(gril,
				"(sw1=0x%.2X,sw2=0x%.2X,%s)",
				reply->sw1,
				reply->sw2,
				response);
	g_ril_print_response(gril, message);

	if (rilp.malformed)
		goto error;

	if (response != NULL) {
		reply->hex_response =
			decode_hex(response, strlen(response),
					(long *) &reply->hex_len, -1);
		g_free(response);

		if (reply->hex_response == NULL)
			goto error;
	}

	return reply;

error:
	g_free(reply);

	return NULL;
}

gchar *g_ril_reply_parse_imsi(GRil *gril, const struct ril_msg *message)
{
	struct parcel rilp;
	gchar *imsi;

	g_ril_init_parcel(message, &rilp);

	/* 15 is the max length of IMSI
	 * add 4 bytes for string length */
	/* FIXME: g_assert(message->buf_len <= 19); */
	imsi = parcel_r_string(&rilp);

	g_ril_append_print_buf(gril, "{%s}", imsi);
	g_ril_print_response(gril, message);

	return imsi;
}

void g_ril_reply_free_sim_status(struct reply_sim_status *status)
{
	if (status) {
		guint i;

		for (i = 0; i < status->num_apps; i++) {
			if (status->apps[i] != NULL) {
				g_free(status->apps[i]->aid_str);
				g_free(status->apps[i]->app_str);
				g_free(status->apps[i]);
			}
		}

		g_free(status);
	}
}

struct reply_sim_status *g_ril_reply_parse_sim_status(GRil *gril,
						const struct ril_msg *message)
{
	struct parcel rilp;
	unsigned int i;
	struct reply_sim_status *status;

	g_ril_append_print_buf(gril, "[%d,%04d]< %s",
			g_ril_get_slot(gril), message->serial_no,
			ril_request_id_to_string(message->req));

	g_ril_init_parcel(message, &rilp);

	/*
	 * FIXME: Need to come up with a common scheme for verifying the
	 * size of RIL message and properly reacting to bad messages.
	 * This could be a runtime assertion, disconnect, drop/ignore
	 * the message, ...
	 *
	 * 20 is the min length of RIL_CardStatus_v6 as the AppState
	 * array can be 0-length.
	 */
	if (message->buf_len < 20) {
		ofono_error("Size of SIM_STATUS reply too small: %d bytes",
			    (int) message->buf_len);
		return NULL;
	}

	status = g_new0(struct reply_sim_status, 1);

	status->card_state = parcel_r_int32(&rilp);

	/*
	 * NOTE:
	 *
	 * The global pin_status is used for multi-application
	 * UICC cards.  For example, there are SIM cards that
	 * can be used in both GSM and CDMA phones.  Instead
	 * of managed PINs for both applications, a global PIN
	 * is set instead.  It's not clear at this point if
	 * such SIM cards are supported by ofono or RILD.
	 */

	status->pin_state = parcel_r_int32(&rilp);
	status->gsm_umts_index = parcel_r_int32(&rilp);
	status->cdma_index = parcel_r_int32(&rilp);
	status->ims_index = parcel_r_int32(&rilp);
	status->num_apps = parcel_r_int32(&rilp);

	/* TODO:
	 * How do we handle long (>80 chars) ril_append_print_buf strings?
	 * Using line wrapping ( via '\' ) introduces spaces in the output.
	 * Do we just make a style-guide exception for PrintBuf operations?
	 */
	g_ril_append_print_buf(gril,
				"(card_state=%d,universal_pin_state=%d,"
				"gsm_umts_index=%d,cdma_index=%d,"
				"ims_index=%d, ",
				status->card_state,
				status->pin_state,
				status->gsm_umts_index,
				status->cdma_index,
				status->ims_index);

	if (status->card_state != RIL_CARDSTATE_PRESENT)
		goto done;

	if (status->num_apps > MAX_UICC_APPS) {
		ofono_error("SIM error; too many apps: %d", status->num_apps);
		status->num_apps = MAX_UICC_APPS;
	}

	for (i = 0; i < status->num_apps; i++) {
		struct reply_sim_app *app;
		DBG("processing app[%d]", i);
		status->apps[i] = g_try_new0(struct reply_sim_app, 1);
		app = status->apps[i];
		if (app == NULL) {
			ofono_error("Can't allocate app_data");
			goto error;
		}

		app->app_type = parcel_r_int32(&rilp);
		app->app_state = parcel_r_int32(&rilp);
		app->perso_substate = parcel_r_int32(&rilp);

		/* TODO: we need a way to instruct parcel to skip
		 * a string, without allocating memory...
		 */
		app->aid_str = parcel_r_string(&rilp); /* application ID (AID) */
		app->app_str = parcel_r_string(&rilp); /* application label */

		app->pin_replaced = parcel_r_int32(&rilp);
		app->pin1_state = parcel_r_int32(&rilp);
		app->pin2_state = parcel_r_int32(&rilp);

		g_ril_append_print_buf(gril,
					"%s[app_type=%d,app_state=%d,"
					"perso_substate=%d,aid_ptr=%s,"
					"app_label_ptr=%s,pin1_replaced=%d,"
					"pin1=%d,pin2=%d],",
					print_buf,
					app->app_type,
					app->app_state,
					app->perso_substate,
					app->aid_str,
					app->app_str,
					app->pin_replaced,
					app->pin1_state,
					app->pin2_state);
	}

done:
	g_ril_append_print_buf(gril, "%s}", print_buf);
	g_ril_print_response(gril, message);

	return status;

error:
	g_ril_reply_free_sim_status(status);

	return NULL;
}

struct ofono_phone_number *g_ril_reply_parse_get_smsc_address(
						GRil *gril,
						const struct ril_msg *message)
{
	struct ofono_phone_number *sca;
	struct parcel rilp;
	char *number, *temp_buf;

	sca = g_new0(struct ofono_phone_number, 1);
	if (sca == NULL) {
		ofono_error("%s Out of memory", __func__);
		goto err_alloc;
	}

	g_ril_init_parcel(message, &rilp);

	temp_buf = parcel_r_string(&rilp);
	if (temp_buf == NULL) {
		ofono_error("%s Cannot read SMSC address", __func__);
		goto err_readsca;
	}

	/* RIL gives address in quotes */
	number = strtok(temp_buf, "\"");
	if (number == NULL || *number == '\0') {
		ofono_error("%s Invalid SMSC address", __func__);
		goto err_scaformat;
	}

	if (number[0] == '+') {
		number = number + 1;
		sca->type = OFONO_NUMBER_TYPE_INTERNATIONAL;
	} else {
		sca->type = OFONO_NUMBER_TYPE_UNKNOWN;
	}

	strncpy(sca->number, number, OFONO_MAX_PHONE_NUMBER_LENGTH);
	sca->number[OFONO_MAX_PHONE_NUMBER_LENGTH] = '\0';

	g_ril_append_print_buf(gril, "{type=%d,number=%s}",
				sca->type, sca->number);
	g_ril_print_response(gril, message);

	g_free(temp_buf);

	return sca;

err_scaformat:
	g_free(temp_buf);
err_readsca:
	g_free(sca);
err_alloc:
	return NULL;
}

int g_ril_reply_parse_sms_response(GRil *gril, const struct ril_msg *message)
{
	struct parcel rilp;
	int error, mr;
	char *ack_pdu;

	/* Set up Parcel struct for proper parsing */
	g_ril_init_parcel(message, &rilp);

	/*
	 * TP-Message-Reference for GSM/
	 * BearerData MessageId for CDMA
	 */
	mr = parcel_r_int32(&rilp);
	ack_pdu = parcel_r_string(&rilp);
	error = parcel_r_int32(&rilp);

	g_ril_append_print_buf(gril, "{%d,%s,%d}",
				mr, ack_pdu, error);
	g_ril_print_response(gril, message);

	g_free(ack_pdu);

	return mr;
}

static gint g_ril_call_compare(gconstpointer a, gconstpointer b)
{
	const struct ofono_call *ca = a;
	const struct ofono_call *cb = b;

	if (ca->id < cb->id)
		return -1;

	if (ca->id > cb->id)
		return 1;

	return 0;
}

GSList *g_ril_reply_parse_get_calls(GRil *gril, const struct ril_msg *message)
{
	struct ofono_call *call;
	struct parcel rilp;
	GSList *l = NULL;
	int num, i;
	gchar *number, *name;

	g_ril_init_parcel(message, &rilp);

	g_ril_append_print_buf(gril, "{");

	/* maguro signals no calls with empty event data */
	if (rilp.size < sizeof(int32_t))
		goto no_calls;

	/* Number of RIL_Call structs */
	num = parcel_r_int32(&rilp);
	for (i = 0; i < num; i++) {
		call = g_try_new(struct ofono_call, 1);
		if (call == NULL)
			break;

		ofono_call_init(call);
		call->status = parcel_r_int32(&rilp);
		call->id = parcel_r_int32(&rilp);
		call->phone_number.type = parcel_r_int32(&rilp);
		parcel_r_int32(&rilp); /* isMpty */
		parcel_r_int32(&rilp); /* isMT */
		parcel_r_int32(&rilp); /* als */
		call->type = parcel_r_int32(&rilp); /* isVoice */
		parcel_r_int32(&rilp); /* isVoicePrivacy */
		number = parcel_r_string(&rilp);
		if (number) {
			strncpy(call->phone_number.number, number,
				OFONO_MAX_PHONE_NUMBER_LENGTH);
			g_free(number);
		}

		parcel_r_int32(&rilp); /* numberPresentation */
		name = parcel_r_string(&rilp);
		if (name) {
			strncpy(call->name, name,
				OFONO_MAX_CALLER_NAME_LENGTH);
			g_free(name);
		}

		parcel_r_int32(&rilp); /* namePresentation */
		parcel_r_int32(&rilp); /* uusInfo */

		if (strlen(call->phone_number.number) > 0)
			call->clip_validity = 0;
		else
			call->clip_validity = 2;

		g_ril_append_print_buf(gril,
					"%s [id=%d,status=%d,type=%d,"
					"number=%s,name=%s]",
					print_buf,
					call->id, call->status, call->type,
					call->phone_number.number, call->name);

		l = g_slist_insert_sorted(l, call, g_ril_call_compare);
	}

no_calls:
	g_ril_append_print_buf(gril, "%s}", print_buf);
	g_ril_print_response(gril, message);

	return l;
}

enum ofono_disconnect_reason g_ril_reply_parse_call_fail_cause(
				GRil *gril, const struct ril_msg *message)
{
	enum ofono_disconnect_reason reason = OFONO_DISCONNECT_REASON_ERROR;
	int last_cause = CALL_FAIL_ERROR_UNSPECIFIED;
	struct parcel rilp;

	g_ril_init_parcel(message, &rilp);

	if (rilp.size < sizeof(int32_t))
		ofono_error("%s: Parcel is too small", __func__);
	else if (parcel_r_int32(&rilp) > 0)
		last_cause = parcel_r_int32(&rilp);

	if (last_cause == CALL_FAIL_NORMAL || last_cause == CALL_FAIL_BUSY)
		reason = OFONO_DISCONNECT_REASON_REMOTE_HANGUP;

	g_ril_append_print_buf(gril, "{%d}", last_cause);
	g_ril_print_response(gril, message);

	return reason;
}

int g_ril_reply_parse_get_mute(GRil *gril, const struct ril_msg *message)
{
	struct parcel rilp;
	int muted;

	g_ril_init_parcel(message, &rilp);

	/* skip length of int[] */
	parcel_r_int32(&rilp);
	muted = parcel_r_int32(&rilp);

	g_ril_append_print_buf(gril, "{%d}", muted);
	g_ril_print_response(gril, message);

	return muted;

}

char *g_ril_reply_parse_baseband_version(GRil *gril,
						const struct ril_msg *message)
{
	struct parcel rilp;
	char *version;

	g_ril_init_parcel(message, &rilp);

	version = parcel_r_string(&rilp);

	g_ril_append_print_buf(gril, "{%s}", version);
	g_ril_print_response(gril, message);

	return version;
}

char *g_ril_reply_parse_get_imei(GRil *gril,
					const struct ril_msg *message)
{
	struct parcel rilp;
	char *imei;

	g_ril_init_parcel(message, &rilp);

	imei = parcel_r_string(&rilp);

	g_ril_append_print_buf(gril, "{%s}", imei);
	g_ril_print_response(gril, message);

	return imei;
}

int g_ril_reply_parse_query_call_waiting(GRil *gril,
						const struct ril_msg *message)
{
	struct parcel rilp;
	int numint, enabled, cls;

	g_ril_init_parcel(message, &rilp);

	numint = parcel_r_int32(&rilp);
	if (numint < 1) {
		ofono_error("%s Wrong format", __func__);
		goto error;
	}

	enabled = parcel_r_int32(&rilp);

	if (enabled > 0)
		cls = parcel_r_int32(&rilp);
	else
		cls = 0;

	g_ril_append_print_buf(gril, "{%d,0x%x}", enabled, cls);
	g_ril_print_response(gril, message);

	return cls;

error:
	return -1;
}

int g_ril_reply_parse_query_clip(GRil *gril,
					const struct ril_msg *message)
{
	struct parcel rilp;
	int clip_status, numint;

	g_ril_init_parcel(message, &rilp);

	numint = parcel_r_int32(&rilp);
	if (numint != 1) {
		ofono_error("%s Wrong format", __func__);
		goto error;
	}

	clip_status = parcel_r_int32(&rilp);

	g_ril_append_print_buf(gril, "{%d}", clip_status);
	g_ril_print_response(gril, message);

	return clip_status;

error:
	return -1;
}

void g_ril_reply_free_get_clir(struct reply_clir *rclir)
{
	g_free(rclir);
}

struct reply_clir *g_ril_reply_parse_get_clir(GRil *gril,
						const struct ril_msg *message)
{
	struct parcel rilp;
	struct reply_clir *rclir;
	int numint;

	rclir = g_try_malloc0(sizeof(*rclir));
	if (rclir == NULL) {
		ofono_error("%s Out of memory", __func__);
		goto error;
	}

	g_ril_init_parcel(message, &rilp);

	/* Length */
	numint = parcel_r_int32(&rilp);
	if (numint != 2) {
		ofono_error("%s Wrong format", __func__);
		goto error;
	}

	/* Set HideCallerId property from network */
	rclir->status = parcel_r_int32(&rilp);

	/* State of the CLIR supplementary service in the network */
	rclir->provisioned = parcel_r_int32(&rilp);

	g_ril_append_print_buf(gril, "{%d,%d}",
				rclir->status, rclir->provisioned);
	g_ril_print_response(gril, message);

	return rclir;

error:
	g_free(rclir);
	return NULL;
}

struct ofono_call_forwarding_condition
	*g_ril_reply_parse_query_call_fwd(GRil *gril,
						const struct ril_msg *message,
						unsigned int *list_size)
{
	struct ofono_call_forwarding_condition *list;
	struct parcel rilp;
	unsigned int i;

	if (list_size == NULL) {
		ofono_error("%s: list_size is NULL!", __func__);
		goto error;
	}

	g_ril_init_parcel(message, &rilp);

	if (rilp.size < sizeof(int32_t)) {
		ofono_error("%s: malformed parcel, can't read num params",
				__func__);
		goto error;
	}

	*list_size = parcel_r_int32(&rilp);
	if (*list_size == 0) {
		/* not really an error; handled in caller */
		goto error;
	}

	list = g_try_new0(struct ofono_call_forwarding_condition, *list_size);
	if (list == NULL) {
		ofono_error("%s: Out of memory", __func__);
		goto error;
	}

	g_ril_append_print_buf(gril, "{");

	for (i = 0; i < *list_size; i++) {
		char *str;

		list[i].status =  parcel_r_int32(&rilp);

		parcel_r_int32(&rilp); /* skip reason */

		list[i].cls = parcel_r_int32(&rilp);
		list[i].phone_number.type = parcel_r_int32(&rilp);

		str = parcel_r_string(&rilp);

		if (str != NULL) {
			strncpy(list[i].phone_number.number, str,
				OFONO_MAX_PHONE_NUMBER_LENGTH);
			g_free(str);

			list[i].phone_number.number[
				OFONO_MAX_PHONE_NUMBER_LENGTH] = '\0';
		}

		list[i].time = parcel_r_int32(&rilp);

		if (rilp.malformed) {
			ofono_error("%s: malformed parcel", __func__);
			g_free(list);
			goto error;
		}

		g_ril_append_print_buf(gril, "%s [%d,%d,%d,%s,%d]",
					print_buf,
					list[i].status,
					list[i].cls,
					list[i].phone_number.type,
					list[i].phone_number.number,
					list[i].time);

	}

	g_ril_append_print_buf(gril, "%s}", print_buf);
	g_ril_print_response(gril, message);

	return list;

error:
	return NULL;
}

int g_ril_reply_parse_get_preferred_network_type(GRil *gril,
						const struct ril_msg *message)
{
	struct parcel rilp;
	int numint, net_type;

	g_ril_init_parcel(message, &rilp);

	numint = parcel_r_int32(&rilp);
	if (numint != 1) {
		ofono_error("%s: Wrong format", __func__);
		goto error;
	}

	net_type = parcel_r_int32(&rilp);
	if (net_type < 0 || net_type > PREF_NET_TYPE_LTE_ONLY) {
		ofono_error("%s: unknown network type", __func__);
		goto error;
	}

	if (rilp.malformed) {
		ofono_error("%s: malformed parcel", __func__);
		goto error;
	}

	g_ril_append_print_buf(gril, "{%d}", net_type);
	g_ril_print_response(gril, message);

	return net_type;

error:
	return -1;
}

int g_ril_reply_parse_query_facility_lock(GRil *gril,
						const struct ril_msg *message)
{
	struct parcel rilp;
	int status, numint;

	g_ril_init_parcel(message, &rilp);

	/* infineon returns two integers */
	numint = parcel_r_int32(&rilp);
	if (numint < 1) {
		ofono_error("%s: wrong format", __func__);
		goto error;
	}

	status = parcel_r_int32(&rilp);

	if (rilp.malformed) {
		ofono_error("%s: malformed parcel", __func__);
		goto error;
	}

	g_ril_append_print_buf(gril, "{%d}", status);
	g_ril_print_response(gril, message);

	return status;

error:
	return -1;
}

int g_ril_reply_parse_set_facility_lock(GRil *gril,
					const struct ril_msg *message)
{
	struct parcel rilp;
	int retries = -1, numint;

	g_ril_init_parcel(message, &rilp);

	/* mako reply has no payload for call barring */
	if (parcel_data_avail(&rilp) == 0)
		goto end;

	numint = parcel_r_int32(&rilp);
	if (numint != 1) {
		ofono_error("%s: wrong format", __func__);
		goto end;
	}

	retries = parcel_r_int32(&rilp);

	if (rilp.malformed) {
		ofono_error("%s: malformed parcel", __func__);
		goto end;
	}

end:
	g_ril_append_print_buf(gril, "{%d}", retries);
	g_ril_print_response(gril, message);

	return retries;
}

int *g_ril_reply_parse_retries(GRil *gril, const struct ril_msg *message,
				enum ofono_sim_password_type passwd_type)
{
	struct parcel rilp;
	int i, numint;
	int *retries = g_try_malloc0(sizeof(int) * OFONO_SIM_PASSWORD_INVALID);

	if (retries == NULL) {
		ofono_error("%s: out of memory", __func__);
		goto no_data;
	}

	for (i = 0; i < OFONO_SIM_PASSWORD_INVALID; ++i)
		retries[i] = -1;

	g_ril_init_parcel(message, &rilp);

	/* maguro/infineon: no data is returned */
	if (parcel_data_avail(&rilp) == 0)
		goto no_data;

	numint = parcel_r_int32(&rilp);

	switch (g_ril_vendor(gril)) {
	case OFONO_RIL_VENDOR_AOSP:
		/*
		 * The number of retries is valid only when a wrong password has
		 * been introduced in Nexus 4. TODO: check Nexus 5 behaviour.
		 */
		if (message->error == RIL_E_PASSWORD_INCORRECT)
			retries[passwd_type] = parcel_r_int32(&rilp);

		g_ril_append_print_buf(gril, "{%d}", retries[passwd_type]);
		break;
	case OFONO_RIL_VENDOR_MTK:
		if (numint != 4) {
			ofono_error("%s: wrong format", __func__);
			goto no_data;
		}

		retries[OFONO_SIM_PASSWORD_SIM_PIN] = parcel_r_int32(&rilp);
		retries[OFONO_SIM_PASSWORD_SIM_PIN2] = parcel_r_int32(&rilp);
		retries[OFONO_SIM_PASSWORD_SIM_PUK] = parcel_r_int32(&rilp);
		retries[OFONO_SIM_PASSWORD_SIM_PUK2] = parcel_r_int32(&rilp);

		g_ril_append_print_buf(gril,
					"{pin %d, pin2 %d, puk %d, puk2 %d}",
					retries[OFONO_SIM_PASSWORD_SIM_PIN],
					retries[OFONO_SIM_PASSWORD_SIM_PIN2],
					retries[OFONO_SIM_PASSWORD_SIM_PUK],
					retries[OFONO_SIM_PASSWORD_SIM_PUK2]);
		break;
	case OFONO_RIL_VENDOR_INFINEON:
		ofono_error("%s: infineon type should not arrive here",
				__func__);
		g_assert(FALSE);
		break;
	}

	if (rilp.malformed) {
		ofono_error("%s: malformed parcel", __func__);
		goto no_data;
	}

	g_ril_print_response(gril, message);

	return retries;

no_data:
	g_free(retries);

	return NULL;
}

void g_ril_reply_free_oem_hook(struct reply_oem_hook *oem_hook)
{
	if (oem_hook) {
		g_free(oem_hook->data);
		g_free(oem_hook);
	}
}

struct reply_oem_hook *g_ril_reply_oem_hook_raw(GRil *gril,
						const struct ril_msg *message)
{
	struct reply_oem_hook *reply = NULL;
	struct parcel rilp;

	reply = g_try_malloc0(sizeof(*reply));
	if (reply == NULL) {
		ofono_error("%s: out of memory", __func__);
		goto end;
	}

	g_ril_init_parcel(message, &rilp);

	reply->data = parcel_r_raw(&rilp, &(reply->length));

	if (rilp.malformed) {
		ofono_error("%s: malformed parcel", __func__);
		g_ril_reply_free_oem_hook(reply);
		reply = NULL;
		goto end;
	}

	g_ril_append_print_buf(gril, "{%d", reply->length);

	if (reply->data != NULL) {
		char *hex_dump;
		hex_dump = encode_hex(reply->data, reply->length, '\0');
		g_ril_append_print_buf(gril, "%s,%s", print_buf, hex_dump);
		g_free(hex_dump);
	}

	g_ril_append_print_buf(gril, "%s}", print_buf);
	g_ril_print_response(gril, message);

end:
	return reply;
}
