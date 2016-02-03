/*
 * Copyright (C) 2013 by Holger Hans Peter Freyther
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "tbf.h"
#include "bts.h"
#include "gprs_debug.h"

#include <errno.h>

extern "C" {
#include <osmocom/core/utils.h>
}


uint8_t *gprs_rlc_data::prepare(size_t block_data_len)
{
	/* todo.. only set it once if it turns out to be a bottleneck */
	memset(block, 0x0, sizeof(block));
	memset(block, 0x2b, block_data_len);

	return block;
}

void gprs_rlc_data::put_data(const uint8_t *data, size_t data_len)
{
	memcpy(block, data, data_len);
	len = data_len;
}

void gprs_rlc_v_b::reset()
{
	for (size_t i = 0; i < ARRAY_SIZE(m_v_b); ++i)
		mark_invalid(i);
}

void gprs_rlc_dl_window::reset()
{
	m_v_s = 0;
	m_v_a = 0;
	m_v_b.reset();
}

int gprs_rlc_dl_window::resend_needed()
{
	for (uint16_t bsn = v_a(); bsn != v_s(); bsn = mod_sns(bsn + 1)) {
		if (m_v_b.is_nacked(bsn) || m_v_b.is_resend(bsn))
			return bsn;
	}

	return -1;
}

int gprs_rlc_dl_window::mark_for_resend()
{
	int resend = 0;

	for (uint16_t bsn = v_a(); bsn != v_s(); bsn = mod_sns(bsn + 1)) {
		if (m_v_b.is_unacked(bsn)) {
			/* mark to be re-send */
			m_v_b.mark_resend(bsn);
			resend += 1;
		}
	}

	return resend;
}

int gprs_rlc_dl_window::count_unacked()
{
	uint16_t unacked = 0;
	uint16_t bsn;

	for (bsn = v_a(); bsn != v_s(); bsn = mod_sns(bsn + 1)) {
		if (!m_v_b.is_acked(bsn))
			unacked += 1;
	}

	return unacked;
}

static uint16_t bitnum_to_bsn(int bitnum, uint16_t ssn)
{
	return (ssn - 1 - bitnum);
}

void gprs_rlc_dl_window::update(BTS *bts, const struct bitvec *rbb,
			uint16_t first_bsn, uint16_t *lost,
			uint16_t *received)
{
	unsigned num_blocks = rbb->cur_bit;
	unsigned bsn;

	/* first_bsn is in range V(A)..V(S) */

	for (unsigned int bitpos = 0; bitpos < num_blocks; bitpos++) {
		bool is_ack;
		bsn = mod_sns(first_bsn + bitpos);
		if (bsn == mod_sns(v_a() - 1))
			break;

		is_ack = bitvec_get_bit_pos(rbb, bitpos) == 1;

		if (is_ack) {
			LOGP(DRLCMACDL, LOGL_DEBUG, "- got ack for BSN=%d\n", bsn);
			if (!m_v_b.is_acked(bsn))
				*received += 1;
			m_v_b.mark_acked(bsn);
		} else {
			LOGP(DRLCMACDL, LOGL_DEBUG, "- got NACK for BSN=%d\n", bsn);
			m_v_b.mark_nacked(bsn);
			bts->rlc_nacked();
			*lost += 1;
		}
	}
}

void gprs_rlc_dl_window::update(BTS *bts, char *show_rbb, uint16_t ssn,
			uint16_t *lost, uint16_t *received)
{
	/* SSN - 1 is in range V(A)..V(S)-1 */
	for (int bitpos = 0; bitpos < ws(); bitpos++) {
		uint16_t bsn = mod_sns(bitnum_to_bsn(bitpos, ssn));

		if (bsn == mod_sns(v_a() - 1))
			break;

		if (show_rbb[ws() - 1 - bitpos] == 'R') {
			LOGP(DRLCMACDL, LOGL_DEBUG, "- got ack for BSN=%d\n", bsn);
			if (!m_v_b.is_acked(bsn))
				*received += 1;
			m_v_b.mark_acked(bsn);
		} else {
			LOGP(DRLCMACDL, LOGL_DEBUG, "- got NACK for BSN=%d\n", bsn);
			m_v_b.mark_nacked(bsn);
			bts->rlc_nacked();
			*lost += 1;
		}
	}
}

int gprs_rlc_dl_window::move_window()
{
	int i;
	uint16_t bsn;
	int moved = 0;

	for (i = 0, bsn = v_a(); bsn != v_s(); i++, bsn = mod_sns(bsn + 1)) {
		if (m_v_b.is_acked(bsn)) {
			m_v_b.mark_invalid(bsn);
			moved += 1;
		} else
			break;
	}

	return moved;
}

void gprs_rlc_dl_window::show_state(char *show_v_b)
{
	int i;
	uint16_t bsn;

	for (i = 0, bsn = v_a(); bsn != v_s(); i++, bsn = mod_sns(bsn + 1)) {
		uint16_t index = bsn & mod_sns_half();
		switch(m_v_b.get_state(index)) {
		case GPRS_RLC_DL_BSN_INVALID:
			show_v_b[i] = 'I';
			break;
		case GPRS_RLC_DL_BSN_ACKED:
			show_v_b[i] = 'A';
			break;
		case GPRS_RLC_DL_BSN_RESEND:
			show_v_b[i] = 'X';
			break;
		case GPRS_RLC_DL_BSN_NACKED:
			show_v_b[i] = 'N';
			break;
		default:
			show_v_b[i] = '?';
		}
	}
	show_v_b[i] = '\0';
}

void gprs_rlc_v_n::reset()
{
	for (size_t i = 0; i < ARRAY_SIZE(m_v_n); ++i)
		m_v_n[i] = GPRS_RLC_UL_BSN_INVALID;
}

void gprs_rlc_window::set_sns(uint16_t sns)
{
	OSMO_ASSERT(sns >= RLC_GPRS_SNS);
	OSMO_ASSERT(sns <= RLC_MAX_SNS);
	/* check for 2^n */
	OSMO_ASSERT((sns & (-sns)) == sns);
	m_sns = sns;
}

void gprs_rlc_window::set_ws(uint16_t ws)
{
	OSMO_ASSERT(ws >= RLC_GPRS_SNS/2);
	OSMO_ASSERT(ws <= RLC_MAX_SNS/2);
	m_ws = ws;
}

/* Update the receive block bitmap */
void gprs_rlc_ul_window::update_rbb(char *rbb)
{
	int i;
	for (i=0; i < ws(); i++) {
		if (m_v_n.is_received(ssn()-1-i))
			rbb[ws()-1-i] = 'R';
		else
			rbb[ws()-1-i] = 'I';
	}
}

/* Raise V(R) to highest received sequence number not received. */
void gprs_rlc_ul_window::raise_v_r(const uint16_t bsn)
{
	uint16_t offset_v_r;
	offset_v_r = mod_sns(bsn + 1 - v_r());
	/* Positive offset, so raise. */
	if (offset_v_r < (sns() >> 1)) {
		while (offset_v_r--) {
			if (offset_v_r) /* all except the received block */
				m_v_n.mark_missing(v_r());
			raise_v_r_to(1);
		}
		LOGP(DRLCMACUL, LOGL_DEBUG, "- Raising V(R) to %d\n", v_r());
	}
}

/*
 * Raise V(Q) if possible. This is looped until there is a gap
 * (non received block) or the window is empty.
 */
uint16_t gprs_rlc_ul_window::raise_v_q()
{
	uint16_t count = 0;

	while (v_q() != v_r()) {
		if (!m_v_n.is_received(v_q()))
			break;
		LOGP(DRLCMACUL, LOGL_DEBUG, "- Taking block %d out, raising "
			"V(Q) to %d\n", v_q(), mod_sns(v_q() + 1));
		raise_v_q(1);
		count += 1;
	}

	return count;
}

void gprs_rlc_ul_window::receive_bsn(const uint16_t bsn)
{
	m_v_n.mark_received(bsn);
	raise_v_r(bsn);
}

bool gprs_rlc_ul_window::invalidate_bsn(const uint16_t bsn)
{
	bool was_valid = m_v_n.is_received(bsn);
	m_v_n.mark_missing(bsn);

	return was_valid;
}

static void gprs_rlc_data_header_init(struct gprs_rlc_data_info *rlc,
	GprsCodingScheme cs, bool with_padding, unsigned int header_bits)
{
	unsigned int i;
	unsigned int padding_bits = with_padding ? cs.optionalPaddingBits() : 0;

	memset(rlc, 0, sizeof(*rlc));

	rlc->cs = cs;
	rlc->with_padding = with_padding;
	rlc->num_data_blocks = cs.numDataBlocks();

	OSMO_ASSERT(rlc->num_data_blocks <= ARRAY_SIZE(rlc->block_info));

	for (i = 0; i < rlc->num_data_blocks; i++) {
		gprs_rlc_data_block_info_init(&rlc->block_info[i], cs,
			with_padding);

		rlc->data_offs_bits[i] =
			header_bits + padding_bits +
			(i+1) * cs.numDataBlockHeaderBits() +
			i * 8 * rlc->block_info[0].data_len;
	}
}

void gprs_rlc_data_info_init_dl(struct gprs_rlc_data_info *rlc,
	GprsCodingScheme cs, bool with_padding)
{
	return gprs_rlc_data_header_init(rlc, cs, with_padding,
		cs.numDataHeaderBitsDL());
}

void gprs_rlc_data_info_init_ul(struct gprs_rlc_data_info *rlc,
	GprsCodingScheme cs, bool with_padding)
{
	return gprs_rlc_data_header_init(rlc, cs, with_padding,
		cs.numDataHeaderBitsUL());
}

void gprs_rlc_data_block_info_init(struct gprs_rlc_data_block_info *rdbi,
	GprsCodingScheme cs, bool with_padding)
{
	unsigned int data_len = cs.maxDataBlockBytes();
	if (with_padding)
		data_len -= cs.optionalPaddingBits() / 8;

	rdbi->data_len = data_len;
	rdbi->bsn = 0;
	rdbi->ti  = 0;
	rdbi->e   = 1;
	rdbi->cv  = 15;
	rdbi->pi  = 0;
	rdbi->spb = 0;
}

unsigned int gprs_rlc_mcs_cps(GprsCodingScheme cs, int punct, int punct2,
	int with_padding)
{
	switch (GprsCodingScheme::Scheme(cs)) {
	case GprsCodingScheme::MCS1: return 0b1011 + punct % 2;
	case GprsCodingScheme::MCS2: return 0b1001 + punct % 2;
	case GprsCodingScheme::MCS3: return (with_padding ? 0b0110 : 0b0011) +
					    punct % 3;
	case GprsCodingScheme::MCS4: return 0b0000 + punct % 3;
	case GprsCodingScheme::MCS5: return  0b100 + punct % 2;
	case GprsCodingScheme::MCS6: return (with_padding ? 0b010 : 0b000) +
					    punct % 2;
	case GprsCodingScheme::MCS7: return 0b10100 + 3 * (punct % 3) + punct2 % 3;
	case GprsCodingScheme::MCS8: return 0b01011 + 3 * (punct % 3) + punct2 % 3;
	case GprsCodingScheme::MCS9: return 0b00000 + 4 * (punct % 3) + punct2 % 3;
	default: ;
	}

	return -1;
}

void gprs_rlc_mcs_cps_decode(unsigned int cps,
	GprsCodingScheme cs, int *punct, int *punct2, int *with_padding)
{
	*punct2 = -1;
	*with_padding = 0;

	switch (GprsCodingScheme::Scheme(cs)) {
	case GprsCodingScheme::MCS1:
		cps -= 0b1011; *punct = cps % 2; break;
	case GprsCodingScheme::MCS2:
		cps -= 0b1001; *punct = cps % 2; break;
	case GprsCodingScheme::MCS3:
		cps -= 0b0011; *punct = cps % 3; *with_padding = cps >= 3; break;
	case GprsCodingScheme::MCS4:
		cps -= 0b0000; *punct = cps % 3; break;
	case GprsCodingScheme::MCS5:
		cps -= 0b100; *punct = cps % 2; break;
	case GprsCodingScheme::MCS6:
		cps -= 0b000; *punct = cps % 2; *with_padding = cps >= 2; break;
	case GprsCodingScheme::MCS7:
		cps -= 0b10100; *punct = cps / 3; *punct2 = cps % 3; break;
	case GprsCodingScheme::MCS8:
		cps -= 0b01011; *punct = cps / 3; *punct2 = cps % 3; break;
	case GprsCodingScheme::MCS9:
		cps -= 0b00000; *punct = cps / 4; *punct2 = cps % 3; break;
	default: ;
	}
}
