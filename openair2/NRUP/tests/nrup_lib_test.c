/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include <stdint.h>

#include "common/config/config_load_configmodule.h"
#include "common/config/config_userapi.h"
#include "common/utils/assertions.h"
#include "common/utils/ds/byte_array_producer.h"
#include "common/utils/LOG/log.h"
#include "openair1/SIMULATION/TOOLS/sim.h"

#include "lib/nrup_dl_user_data.h"

configmodule_interface_t *uniqCfg = NULL;

#define rand_bool() ((taus() & 1) != 0)
#define rand_sn24() (taus() & NRUP_NR_U_SN_MAX)
#define rand_pdcp_sn() (taus() & NRUP_NR_PDCP_SN_MAX)
#define rand_in_range(min, max) ((min) + taus() % ((max) - (min) + 1))

void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  UNUSED(assert);
  printf("detected error at %s:%d:%s: %s\n", file, line, function, s);
  abort();
}

static void nrup_dl_user_encdec_test(const nrup_dl_user_data_t *orig)
{
  uint8_t buf[32];
  byte_array_producer_t b = byte_array_producer_from_buffer(buf, sizeof(buf));
  AssertFatal(encode_nrup_dl_user_data(&b, orig) == 1, "encode_nrup_dl_user_data() failed\n");

  nrup_dl_user_data_t decoded;
  AssertFatal(decode_nrup_dl_user_data(buf, b.pos, &decoded), "decode_nrup_dl_user_data() failed\n");
  AssertFatal(eq_nrup_dl_user_data(orig, &decoded), "eq_nrup_dl_user_data(): decoded message doesn't match\n");

  /* TS 38.425 clause 5.5.1: ignore remaining octets */
  buf[b.pos] = 0xa5;
  AssertFatal(decode_nrup_dl_user_data(buf, b.pos + 1, &decoded), "decode_nrup_dl_user_data() failed\n");
  AssertFatal(eq_nrup_dl_user_data(orig, &decoded), "eq_nrup_dl_user_data(): decoded message doesn't match\n");
}

/** @brief DL USER DATA round-trip and Report Delivered truncation rejection (TS 38.425 Figure 5.5.2.1-1)
 * @note Random IEs use taus() */
static void test_dl_user_data(void)
{
  set_taus_seed(0);

  nrup_dl_user_encdec_test(&(const nrup_dl_user_data_t){
      .nru_sequence_number = rand_sn24(),
  });

  nrup_dl_user_data_t discard_msg = {
      .nru_sequence_number = rand_sn24(),
      .dl_discard_blocks_present = true,
      .n_dl_discard_blocks = 3,
  };
  for (uint8_t i = 0; i < discard_msg.n_dl_discard_blocks; i++) {
    discard_msg.dl_discard_blocks[i].dl_discard_nr_pdcp_pdu_sn_start = rand_pdcp_sn();
    discard_msg.dl_discard_blocks[i].discarded_block_size =
        rand_in_range(NRUP_DISCARDED_BLOCK_SIZE_MIN, NRUP_DISCARDED_BLOCK_SIZE_MAX);
  }
  nrup_dl_user_encdec_test(&discard_msg);

  nrup_dl_user_data_t full_msg = {
      .report_polling = rand_bool(),
      .retransmission = rand_bool(),
      .assistance_info_report_polling = rand_bool(),
      .user_data_existence = rand_bool(),
      .request_out_of_seq_report = rand_bool(),
      .nru_sequence_number = rand_sn24(),
  };
  if (rand_bool()) {
    full_msg.dl_flush = true;
    full_msg.dl_discard_nr_pdcp_pdu_sn = rand_pdcp_sn();
    LOG_I(NR_UP, "DL USER DATA random IE enabled: DL Flush\n");
  }
  if (rand_bool()) {
    full_msg.report_delivered = true;
    full_msg.nr_pdcp_pdu_sn = rand_sn24();
    LOG_I(NR_UP, "DL USER DATA random IE enabled: Report Delivered\n");
  }

  nrup_dl_user_encdec_test(&full_msg);

  uint8_t buf[32];
  byte_array_producer_t b = byte_array_producer_from_buffer(buf, sizeof(buf));
  const nrup_dl_user_data_t msg = {.nru_sequence_number = 1};
  AssertFatal(encode_nrup_dl_user_data(&b, &msg) == 1, "encode_nrup_dl_user_data() failed\n");
  AssertFatal(b.pos % 4 == 2, "TS 38.425 clause 5.5.3.24: NR-U PDU length shall be n*4-2\n");
  buf[1] |= 1u << NRUP_DL_USER_DATA_REPORT_DELIVERED; /* Report Delivered flag set without nr_pdcp_pdu_sn in orig */

  nrup_dl_user_data_t decoded;
  /* Report Delivered flag set but DL report NR PDCP PDU SN body missing (TS 38.425 §5.5.3.41-42) */
  LOG_A(NR_UP, "Expected failure: Report Delivered flag without NR PDCP PDU SN body\n");
  AssertFatal(!decode_nrup_dl_user_data(buf, b.pos, &decoded), "decode must reject Report Delivered without NR PDCP PDU SN\n");
}

int main(int argc, char **argv)
{
  uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY);
  AssertFatal(uniqCfg != NULL, "configuration module init failed\n");
  logInit();
  set_log(NR_UP, OAILOG_INFO);
  test_dl_user_data();
  return 0;
}
