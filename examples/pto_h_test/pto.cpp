#include "tl_templates/pto/common.h"
#include <pto/pto-inst.hpp>
#include "acl/acl.h"
#include <runtime/rt_ffts.h>
using namespace pto;

AICORE void main_kernel(__gm__ half *K_handle, __gm__ half *W_handle, __gm__ half *U_handle, __gm__ float *G_handle, __gm__ half *workspace_1_handle, __gm__ half *workspace_2_handle, __gm__ half *workspace_3_handle, __gm__ half *workspace_4_handle, __gm__ half *S_handle, __gm__ half *V_handle, __gm__ half *FS_handle, uint64_t ffts_Addr) {
  auto cid = get_block_idx();
  set_ffts_base_addr(ffts_Addr);

  tl::ascend_pto::TileMatL1<half, 128, 128, 128, 128> s_l1;
  TASSIGN(s_l1, 0);
  tl::ascend_pto::TileMatL1<half, 128, 128, 128, 128> w_l1;
  TASSIGN(w_l1, 32768);
  TileAcc<float, 128, 128, 128, 128> ws_l0;
  TASSIGN(ws_l0, 0);
  tl::ascend_pto::TileMatL1<half, 128, 128, 128, 128> k_l1;
  TASSIGN(k_l1, 65536);
  tl::ascend_pto::TileMatL1<half, 128, 128, 128, 128> v_l1;
  TASSIGN(v_l1, 98304);
  TileAcc<float, 128, 128, 128, 128> kv_l0;
  TASSIGN(kv_l0, 65536);
  tl::ascend_pto::TileUbDataND<float, 1, 64, 1, 64> zero_ub;
  TASSIGN(zero_ub, 0);
  tl::ascend_pto::TileUbDataND<float, 64, 128, 64, 128> s_ub;
  TASSIGN(s_ub, 256);
  tl::ascend_pto::TileUbDataND<half, 64, 128, 64, 128> k_ub_half;
  TASSIGN(k_ub_half, 33024);
  tl::ascend_pto::TileUbDataND<float, 1, 128, 1, 128> g_ub;
  TASSIGN(g_ub, 49408);
  tl::ascend_pto::TileUbDataND<half, 64, 128, 64, 128> s_ub_half;
  TASSIGN(s_ub_half, 165120);
  tl::ascend_pto::TileUbDataND<half, 64, 128, 64, 128> u_ub_half;
  TASSIGN(u_ub_half, 49920);
  tl::ascend_pto::TileUbDataND<float, 64, 128, 64, 128> k_ub;
  TASSIGN(k_ub, 66304);
  tl::ascend_pto::TileUbDataND<float, 1, 64, 1, 64> g_v_ub;
  TASSIGN(g_v_ub, 99072);
  tl::ascend_pto::TileUbDataND<float, 1, 64, 1, 64> coeff_ub;
  TASSIGN(coeff_ub, 99328);
  tl::ascend_pto::TileUbDataND<float, 64, 128, 64, 128> u_ub;
  TASSIGN(u_ub, 99584);
  tl::ascend_pto::TileUbDataND<float, 64, 128, 64, 128> ws_ub;
  TASSIGN(ws_ub, 132352);
  tl::ascend_pto::TileUbDataND<float, 64, 128, 64, 128> kv_ub;
  TASSIGN(kv_ub, 49920);
  auto vid = get_subblockid();
#if defined(__DAV_C220_CUBE__)

  for (int32_t i = 0; i < 128; ++i) {
      set_flag(PIPE_FIX, PIPE_MTE2, EVENT_ID5);
      wait_flag(PIPE_FIX, PIPE_MTE2, EVENT_ID5);
      tl::ascend_pto::copy_gm_to_l1<half, half, 1, 1, 1, 128, 128, 1, 128 * 128 * 32, 128 * 128, 128, 1, 128, 128>(workspace_3_handle + (cid * 16384), s_l1);
      tl::ascend_pto::copy_gm_to_l1<half, half, 1, 1, 1, 128, 128, 128 * 16384 * 16 * 2, 128 * 16384 * 16, 128 * 16384, 128, 1, 128, 128>(W_handle + ((cid * 2097152) + (i * 16384)), w_l1);
      set_flag(PIPE_MTE2, PIPE_M, EVENT_ID1);
      wait_flag(PIPE_MTE2, PIPE_M, EVENT_ID1);
      tl::ascend_pto::gemm_v0<half, float, 128, 128, 128, 128, 128, 128, false, false>(w_l1, s_l1, ws_l0, (bool)1);
      set_flag(PIPE_M, PIPE_FIX, EVENT_ID2);
      wait_flag(PIPE_M, PIPE_FIX, EVENT_ID2);
      pipe_barrier(PIPE_FIX);
      tl::ascend_pto::copy_l0c_to_gm<half, float, 1, 1, 1, 128, 128, 1, 128 * 128 * 32, 128 * 128, 128, 1, 128, 128>(workspace_1_handle + (cid * 16384), ws_l0);
      ffts_cross_core_sync(PIPE_FIX, 33);
      wait_flag_dev(1);
      tl::ascend_pto::copy_gm_to_l1<half, half, 1, 1, 1, 128, 128, 1, 128 * 128 * 32, 128 * 128, 128, 1, 128, 128>(workspace_2_handle + (cid * 16384), k_l1);
      tl::ascend_pto::copy_gm_to_l1<half, half, 1, 1, 1, 128, 128, 128 * 16384 * 16 * 2, 128 * 16384 * 16, 128 * 16384, 128, 1, 128, 128>(V_handle + ((cid * 2097152) + (i * 16384)), v_l1);
      set_flag(PIPE_MTE2, PIPE_M, EVENT_ID3);
      wait_flag(PIPE_MTE2, PIPE_M, EVENT_ID3);
      tl::ascend_pto::gemm_v0<half, float, 128, 128, 128, 128, 128, 128, true, false>(k_l1, v_l1, kv_l0, (bool)1);
      set_flag(PIPE_M, PIPE_FIX, EVENT_ID4);
      wait_flag(PIPE_M, PIPE_FIX, EVENT_ID4);
      tl::ascend_pto::copy_l0c_to_gm<half, float, 1, 1, 1, 128, 128, 1, 128 * 128 * 32, 128 * 128, 128, 1, 128, 128>(workspace_4_handle + (cid * 16384), kv_l0);
      ffts_cross_core_sync(PIPE_FIX, 545);
      wait_flag_dev(3);
    }
#endif
#if defined(__DAV_C220_VEC__)
    set_mask_norm();
    set_vector_mask(-1, -1);
    TEXPANDS(zero_ub, 0.000000e+00f);
    TEXPANDS(s_ub, 0.000000e+00f);
    tl::ascend_pto::copy_gm_to_ub<half, half, 1, 1, 1, 64, 128, 128 * 16384 * 16 * 2, 128 * 16384 * 16, 128 * 16384, 128, 1, 64, 128>(K_handle + ((cid * 2097152) + (vid * 8192)), k_ub_half);
    tl::ascend_pto::copy_gm_to_ub<float, float, 1, 1, 1, 1, 128, 1, 16384 * 16 * 2, 16384 * 16, 16384, 1, 1, 128>(G_handle + (cid * 16384), g_ub);
    tl::ascend_pto::set_flag_pipeline<PIPE_MTE2, PIPE_V> (0);
    tl::ascend_pto::wait_flag_pipeline<PIPE_MTE2, PIPE_V> (0);

  for (int32_t i_1 = 0; i_1 < 128; ++i_1) {
      tl::ascend_pto::copy_gm_to_ub<half, half, 1, 1, 1, 64, 128, 128 * 16384 * 16 * 2, 128 * 16384 * 16, 128 * 16384, 128, 1, 64, 128>(U_handle + (((cid * 2097152) + (i_1 * 16384)) + (vid * 8192)), u_ub_half);
      set_flag(PIPE_MTE2, PIPE_V, EVENT_ID2);
      wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID2);
      TCVT(k_ub, k_ub_half, pto::RoundMode::CAST_NONE);
      tl::ascend_pto::mov_tile<float, 128>(49408, 99072, (vid * 64), 0, 4);
      // DEBUG: Step 1 - Verify g_v_ub after mov_tile
      if (cid == 0 && vid == 0 && i_1 == 0) {
        TPRINT(g_v_ub);
      }
      pipe_barrier(PIPE_ALL);
      float tmp = g_ub.GetValue(127);
      pipe_barrier(PIPE_V);
      // DEBUG: Step 2 - Verify coeff before calculation
      if (cid == 0 && vid == 0 && i_1 == 0) {
        TPRINT(coeff_ub);
      }
      TADDS(coeff_ub, g_v_ub, -tmp);
      pipe_barrier(PIPE_V);
      TSUB(coeff_ub, zero_ub, coeff_ub);
      pipe_barrier(PIPE_V);
      TEXP(coeff_ub, coeff_ub);
      // DEBUG: Step 3 - Verify coeff after exp
      if (cid == 0 && vid == 0 && i_1 == 0) {
        TPRINT(coeff_ub);
      }
      TEXP(g_ub, g_ub);
      tl::ascend_pto::set_flag_pipeline<PIPE_MTE2, PIPE_V> (0);
      tl::ascend_pto::wait_flag_pipeline<PIPE_MTE2, PIPE_V> (0);
      TCVT(u_ub, u_ub_half, pto::RoundMode::CAST_NONE);

  for (int32_t i_2 = 0; i_2 < 16; ++i_2) {
        pipe_barrier(PIPE_ALL);
        float tmp0 = coeff_ub.GetValue((i_2 * 4));
        pipe_barrier(PIPE_ALL);
        float tmp1 = coeff_ub.GetValue(((i_2 * 4) + 1));
        pipe_barrier(PIPE_ALL);
        float tmp2 = coeff_ub.GetValue(((i_2 * 4) + 2));
        pipe_barrier(PIPE_ALL);
        float tmp3 = coeff_ub.GetValue(((i_2 * 4) + 3));
        TMULS(k_ub, k_ub, tmp0);
        pipe_barrier(PIPE_V);
        TMULS(k_ub, k_ub, tmp1);
        pipe_barrier(PIPE_V);
        TMULS(k_ub, k_ub, tmp2);
        pipe_barrier(PIPE_V);
        TMULS(k_ub, k_ub, tmp3);
      }
      // DEBUG: Step 4 - Verify k_ub after multiplication
      if (cid == 0 && vid == 0 && i_1 == 0) {
        TPRINT(k_ub);
      }
      wait_flag_dev(0);
      set_flag(PIPE_V, PIPE_MTE2, EVENT_ID3);
      wait_flag(PIPE_V, PIPE_MTE2, EVENT_ID3);
      tl::ascend_pto::copy_gm_to_ub<half, half, 1, 1, 1, 64, 128, 1, 128 * 128 * 32, 128 * 128, 128, 1, 64, 128>(workspace_1_handle + ((cid * 16384) + (vid * 8192)), u_ub_half);
      // DEBUG: Verify ws_ub after loading from workspace_1 (print for multiple iterations)
      if (cid == 0 && vid == 0) {
        TPRINT(ws_ub);
      }
      tl::ascend_pto::set_flag_pipeline<PIPE_MTE2, PIPE_V> (0);
      tl::ascend_pto::wait_flag_pipeline<PIPE_MTE2, PIPE_V> (0);
      set_flag(PIPE_MTE2, PIPE_V, EVENT_ID4);
      wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID4);
      TCVT(ws_ub, u_ub_half, pto::RoundMode::CAST_NONE);
      pipe_barrier(PIPE_V);
      // DEBUG: Verify u_ub and ws_ub before subtraction
      if (cid == 0 && vid == 0 && i_1 == 0) {
        TPRINT(u_ub);
        TPRINT(ws_ub);
      }
      TSUB(u_ub, u_ub, ws_ub);
      pipe_barrier(PIPE_V);
      TCVT(u_ub_half, u_ub, pto::RoundMode::CAST_NONE);
      TCVT(k_ub_half, k_ub, pto::RoundMode::CAST_NONE);
      tl::ascend_pto::set_flag_pipeline<PIPE_V, PIPE_MTE3> (0);
      tl::ascend_pto::wait_flag_pipeline<PIPE_V, PIPE_MTE3> (0);
      set_flag(PIPE_V, PIPE_MTE3, EVENT_ID5);
      wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID5);
      tl::ascend_pto::copy_ub_to_gm<half, half, 1, 1, 1, 64, 128, 128 * 16384 * 16 * 2, 128 * 16384 * 16, 128 * 16384, 128, 1, 64, 128, 64, 128>(V_handle + (((cid * 2097152) + (i_1 * 16384)) + (vid * 8192)), 49920, 0,2);
      tl::ascend_pto::copy_ub_to_gm<half, half, 1, 1, 1, 64, 128, 1, 128 * 128 * 32, 128 * 128, 128, 1, 64, 128, 64, 128>(workspace_2_handle + ((cid * 16384) + (vid * 8192)), 33024, 0,2);
      ffts_cross_core_sync(PIPE_MTE3, 289);
      pipe_barrier(PIPE_ALL);
      float tmp_1 = g_ub.GetValue(127);
      TMULS(s_ub, s_ub, tmp_1);
      tl::ascend_pto::set_flag_pipeline<PIPE_V, PIPE_MTE2> (0);
      tl::ascend_pto::wait_flag_pipeline<PIPE_V, PIPE_MTE2> (0);
      pipe_barrier(PIPE_ALL);
      if (i_1 < 127) {
        tl::ascend_pto::copy_gm_to_ub<half, half, 1, 1, 1, 64, 128, 128 * 16384 * 16 * 2, 128 * 16384 * 16, 128 * 16384, 128, 1, 64, 128>(K_handle + ((((cid * 2097152) + (i_1 * 16384)) + (vid * 8192)) + 16384), k_ub_half);
        tl::ascend_pto::copy_gm_to_ub<float, float, 1, 1, 1, 1, 128, 1, 16384 * 16 * 2, 16384 * 16, 16384, 1, 1, 128>(G_handle + (((cid * 16384) + (i_1 * 128)) + 128), g_ub);
      }
      pipe_barrier(PIPE_ALL);
      wait_flag_dev(2);
      tl::ascend_pto::copy_gm_to_ub<half, half, 1, 1, 1, 64, 128, 1, 128 * 128 * 32, 128 * 128, 128, 1, 64, 128>(workspace_4_handle + ((cid * 16384) + (vid * 8192)), s_ub_half);
      tl::ascend_pto::set_flag_pipeline<PIPE_MTE2, PIPE_V> (0);
      tl::ascend_pto::wait_flag_pipeline<PIPE_MTE2, PIPE_V> (0);
      set_flag(PIPE_MTE2, PIPE_V, EVENT_ID6);
      wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID6);
      TCVT(kv_ub, s_ub_half, pto::RoundMode::CAST_NONE);
      pipe_barrier(PIPE_ALL);
      pipe_barrier(PIPE_V);
      // DEBUG: Verify s_ub and kv_ub before final add
      if (cid == 0 && vid == 0 && i_1 == 127) {
        TPRINT(s_ub);
        TPRINT(kv_ub);
      }
      TADD(s_ub, s_ub, kv_ub);
      pipe_barrier(PIPE_V);
      TCVT(s_ub_half, s_ub, pto::RoundMode::CAST_NONE);
      // DEBUG: Step 5 - Verify s_ub (final result)
      if (cid == 0 && vid == 0 && i_1 == 127) {
        TPRINT(s_ub);
      }
      pipe_barrier(PIPE_ALL);
      if (i_1 < 127) {
        tl::ascend_pto::set_flag_pipeline<PIPE_V, PIPE_MTE3> (0);
        tl::ascend_pto::wait_flag_pipeline<PIPE_V, PIPE_MTE3> (0);
        tl::ascend_pto::copy_ub_to_gm<half, half, 1, 1, 1, 64, 128, 1, 128 * 128 * 32, 128 * 128, 128, 1, 64, 128, 64, 128>(workspace_3_handle + ((cid * 16384) + (vid * 8192)), 165120, 0,2);
        tl::ascend_pto::copy_ub_to_gm<half, half, 1, 1, 1, 64, 128, 128 * 128 * 128 * 16, 128 * 128 * 128, 128 * 128, 128, 1, 64, 128, 64, 128>(S_handle + ((((cid * 2097152) + (i_1 * 16384)) + (vid * 8192)) + 16384), 165120, 0,2);
      }
      pipe_barrier(PIPE_ALL);
      ffts_cross_core_sync(PIPE_MTE3, 801);
    }
    tl::ascend_pto::set_flag_pipeline<PIPE_V, PIPE_MTE3> (0);
    tl::ascend_pto::wait_flag_pipeline<PIPE_V, PIPE_MTE3> (0);
    tl::ascend_pto::copy_ub_to_gm<half, half, 1, 1, 1, 64, 128, 128 * 128 * 16 * 2, 128 * 128 * 16, 128 * 128, 128, 1, 64, 128, 64, 128>(FS_handle + ((cid * 16384) + (vid * 8192)), 165120, 0,2);
#endif
}

extern "C" __global__ AICORE void launch_kernel(__gm__ uint8_t *K_handle, __gm__ uint8_t *W_handle, __gm__ uint8_t *U_handle, __gm__ uint8_t *G_handle, __gm__ uint8_t *workspace_1_handle, __gm__ uint8_t *workspace_2_handle, __gm__ uint8_t *workspace_3_handle, __gm__ uint8_t *workspace_4_handle, __gm__ uint8_t *S_handle, __gm__ uint8_t *V_handle, __gm__ uint8_t *FS_handle, uint64_t fftsAddr)
{
    main_kernel(reinterpret_cast<__gm__ half *>(K_handle),
     reinterpret_cast<__gm__ half *>(W_handle),
     reinterpret_cast<__gm__ half *>(U_handle),
     reinterpret_cast<__gm__ float *>(G_handle),
     reinterpret_cast<__gm__ half *>(workspace_1_handle),
     reinterpret_cast<__gm__ half *>(workspace_2_handle),
     reinterpret_cast<__gm__ half *>(workspace_3_handle),
     reinterpret_cast<__gm__ half *>(workspace_4_handle),
     reinterpret_cast<__gm__ half *>(S_handle),
     reinterpret_cast<__gm__ half *>(V_handle),
     reinterpret_cast<__gm__ half *>(FS_handle),
     reinterpret_cast<uint64_t>(fftsAddr));
}

extern "C" void call(uint8_t *K_handle, uint8_t *W_handle, uint8_t *U_handle, uint8_t *G_handle, uint8_t *workspace_1_handle, uint8_t *workspace_2_handle, uint8_t *workspace_3_handle, uint8_t *workspace_4_handle, uint8_t *S_handle, uint8_t *V_handle, uint8_t *FS_handle, void *stream)
{
    uint32_t fftsLen{0};
    uint64_t fftsAddr{0};
    rtGetC2cCtrlAddr(&fftsAddr, &fftsLen);
    launch_kernel<<<32, nullptr, stream>>>(K_handle, W_handle, U_handle, G_handle, workspace_1_handle, workspace_2_handle, workspace_3_handle, workspace_4_handle, S_handle, V_handle, FS_handle, fftsAddr);
}