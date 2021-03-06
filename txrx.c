/*
 * Contact: Eugene Krasnikov <k.eugene.e@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "txrx.h"
#include <linux/ieee80211.h>

#define RSSI0(x) (100 - ((x->phy_stat0 >> 24) & 0xff))

int wcn36xx_rx_skb(struct wcn36xx *wcn, struct sk_buff *skb)
{
	struct ieee80211_rx_status status;
	struct ieee80211_hdr *hdr;
	struct wcn36xx_rx_bd *bd;
	u16 fc, sn;
	/*
	 * All fields must be 0, otherwise it can lead to
	 * unexpected consequences.
	 */
	memset(&status, 0, sizeof(status));

	bd = (struct wcn36xx_rx_bd *)skb->data;
	buff_to_be((u32 *)bd, sizeof(*bd)/sizeof(u32));

	skb_put(skb, bd->pdu.mpdu_header_off + bd->pdu.mpdu_len);
	skb_pull(skb, bd->pdu.mpdu_header_off);

	status.mactime = 10;
	status.freq = wcn->current_channel->center_freq;
	status.band = wcn->current_channel->band;
	status.signal = -RSSI0(bd);
	status.antenna = 1;
	status.rate_idx = 1;
	status.flag = 0;
	status.rx_flags = 0;
	status.flag |= RX_FLAG_IV_STRIPPED |
		       RX_FLAG_MMIC_STRIPPED |
		       RX_FLAG_DECRYPTED;
	wcn36xx_dbg(WCN36XX_DBG_RX, "status.flags=%x "
		    "status->vendor_radiotap_len=%x",
		    status.flag,  status.vendor_radiotap_len);

	memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));

	hdr = (struct ieee80211_hdr *) skb->data;
	fc = __le16_to_cpu(hdr->frame_control);
	sn = IEEE80211_SEQ_TO_SN(__le16_to_cpu(hdr->seq_ctrl));

	if (ieee80211_is_beacon(hdr->frame_control)) {
		wcn36xx_dbg(WCN36XX_DBG_BEACON, "beacon skb %p len %d fc %04x sn %d",
			    skb, skb->len, fc, sn);
		wcn36xx_dbg_dump(WCN36XX_DBG_BEACON_DUMP, "SKB <<< ",
				 (char *)skb->data, skb->len);
	} else {
		wcn36xx_dbg(WCN36XX_DBG_RX, "rx skb %p len %d fc %04x sn %d",
			    skb, skb->len, fc, sn);
		wcn36xx_dbg_dump(WCN36XX_DBG_RX_DUMP, "SKB <<< ",
				 (char *)skb->data, skb->len);
	}

	ieee80211_rx_ni(wcn->hw, skb);

	return 0;
}
void wcn36xx_prepare_tx_bd(struct wcn36xx_tx_bd *bd, u32 len, u32 header_len)
{
	memset(bd, 0, sizeof(*bd));
	bd->pdu.mpdu_header_len = header_len;
	bd->pdu.mpdu_header_off = sizeof(*bd);
	bd->pdu.mpdu_data_off = bd->pdu.mpdu_header_len +
		bd->pdu.mpdu_header_off;
	bd->pdu.mpdu_len = len;
}
void wcn36xx_fill_tx_bd(struct wcn36xx *wcn, struct wcn36xx_tx_bd *bd,
			u8 broadcast, u8 encrypt, struct ieee80211_hdr *hdr,
			bool tx_compl)
{
	bd->dpu_rf = WCN36XX_BMU_WQ_TX;
	bd->pdu.tid   = WCN36XX_TID;
	bd->pdu.reserved3 = 0xd;

	if (broadcast) {
		/* broadcast */
		bd->ub = 1;
		bd->queue_id = WCN36XX_TX_B_WQ_ID;

		/* default rate for broadcast */
		bd->bd_rate = 0;

		/* No ack needed not unicast */
		bd->ack_policy = 1;
	} else {
		bd->queue_id = WCN36XX_TX_U_WQ_ID;
		/* default rate for unicast */
		bd->ack_policy = 0;
		if (ieee80211_is_data(hdr->frame_control))
			bd->bd_rate = WCN36XX_BD_RATE_DATA;
		else if (ieee80211_is_mgmt(hdr->frame_control))
			bd->bd_rate = WCN36XX_BD_RATE_MGMT;
		else if (ieee80211_is_ctl(hdr->frame_control))
			bd->bd_rate = WCN36XX_BD_RATE_CTRL;
		else
			wcn36xx_warn("frame control type unknown");
	}

	bd->sta_index = wcn->current_vif->sta_index;
	bd->dpu_desc_idx = wcn->current_vif->dpu_desc_index;

	bd->dpu_ne = encrypt;
	bd->tx_comp = tx_compl;

	buff_to_be((u32 *)bd, sizeof(*bd)/sizeof(u32));
	bd->tx_bd_sign = 0xbdbdbdbd;
}
