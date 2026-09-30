#include "ovp.h"

#include "board.h"

uint32_t ovp_threshold_mv(uint32_t contract_mv) {
    uint32_t mv = (contract_mv * OVP_MARGIN_PCT) / 100;
    return mv > OVP_CEILING_MV ? OVP_CEILING_MV : mv;
}

uint32_t ovp_transition_mv(uint32_t from_mv, uint32_t to_mv) {
    return ovp_threshold_mv(from_mv > to_mv ? from_mv : to_mv);
}

uint16_t ovp_dac_code(uint32_t threshold_mv, uint32_t vdda_mv) {
    if (!vdda_mv) return 0;
    // rounded down: the trip point never lands above the one asked for
    uint32_t code = (threshold_mv * 4095u) / (SENSE_DIV_NUM * vdda_mv);
    return (uint16_t)(code > 4095 ? 4095 : code);
}
