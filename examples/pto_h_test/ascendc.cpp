#include "tl_templates/ascend/common.h"
#include "acl/acl.h"
#include <runtime/rt_ffts.h>
using namespace Catlass;
using uint = unsigned int;
using uchar = unsigned char;
using ushort = unsigned short;

extern "C" __global__ __aicore__ void main_kernel( GM_ADDR K_handle,  GM_ADDR W_handle,  GM_ADDR U_handle,  GM_ADDR G_handle,  GM_ADDR workspace_1_handle,  GM_ADDR workspace_2_handle,  GM_ADDR workspace_3_handle,  GM_ADDR workspace_4_handle,  GM_ADDR S_handle,  GM_ADDR V_handle,  GM_ADDR FS_handle, uint64_t fftsAddr) {
  KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_MIX_AIC_1_2);
  AscendC::TPipe pipe;

  AscendC::GlobalTensor<half> K;
  K.SetGlobalBuffer((__gm__ half*)K_handle);
  AscendC::GlobalTensor<half> W;
  W.SetGlobalBuffer((__gm__ half*)W_handle);
  AscendC::GlobalTensor<half> U;
  U.SetGlobalBuffer((__gm__ half*)U_handle);
  AscendC::GlobalTensor<float> G;
  G.SetGlobalBuffer((__gm__ float*)G_handle);
  AscendC::GlobalTensor<half> workspace_1;
  workspace_1.SetGlobalBuffer((__gm__ half*)workspace_1_handle);
  AscendC::GlobalTensor<half> workspace_2;
  workspace_2.SetGlobalBuffer((__gm__ half*)workspace_2_handle);
  AscendC::GlobalTensor<half> workspace_3;
  workspace_3.SetGlobalBuffer((__gm__ half*)workspace_3_handle);
  AscendC::GlobalTensor<half> workspace_4;
  workspace_4.SetGlobalBuffer((__gm__ half*)workspace_4_handle);
  AscendC::GlobalTensor<half> S;
  S.SetGlobalBuffer((__gm__ half*)S_handle);
  AscendC::GlobalTensor<half> V;
  V.SetGlobalBuffer((__gm__ half*)V_handle);
  AscendC::GlobalTensor<half> FS;
  FS.SetGlobalBuffer((__gm__ half*)FS_handle);

  AscendC::TBuf<AscendC::TPosition::A2> ascend_l0a;
  pipe.InitBuffer(ascend_l0a, 65536);
  AscendC::TBuf<AscendC::TPosition::B2> ascend_l0b;
  pipe.InitBuffer(ascend_l0b, 65536);
  AscendC::TBuf<AscendC::TPosition::A1> ascend_l1; pipe.InitBuffer(ascend_l1, 524032);
  AscendC::TBuf<AscendC::TPosition::CO1> ascend_l0c; pipe.InitBuffer(ascend_l0c, 131072);
  AscendC::TBuf<AscendC::TPosition::VECCALC> ascend_ub; pipe.InitBuffer(ascend_ub, 196352);
  pipe.Destroy();
  auto cid = AscendC::GetBlockIdx();
  if ASCEND_IS_AIV {
    cid = cid / 2;
  }
  auto s_l1 = ascend_l1.GetWithOffset<half>(16384, 0);
  auto w_l1 = ascend_l1.GetWithOffset<half>(16384, 32768);
  auto ws_l0 = ascend_l0c.GetWithOffset<float>(16384, 0);
  auto k_l1 = ascend_l1.GetWithOffset<half>(16384, 65536);
  auto v_l1 = ascend_l1.GetWithOffset<half>(16384, 98304);
  auto kv_l0 = ascend_l0c.GetWithOffset<float>(16384, 65536);
  auto zero_ub = ascend_ub.GetWithOffset<float>(64, 0);
  auto s_ub = ascend_ub.GetWithOffset<float>(8192, 256);
  auto k_ub_half = ascend_ub.GetWithOffset<half>(8192, 33024);
  auto g_ub = ascend_ub.GetWithOffset<float>(128, 49408);
  auto s_ub_half = ascend_ub.GetWithOffset<half>(8192, 165120);
  auto u_ub_half = ascend_ub.GetWithOffset<half>(8192, 49920);
  auto k_ub = ascend_ub.GetWithOffset<float>(8192, 66304);
  auto g_v_ub = ascend_ub.GetWithOffset<float>(64, 99072);
  auto coeff_ub = ascend_ub.GetWithOffset<float>(64, 99328);
  auto u_ub = ascend_ub.GetWithOffset<float>(8192, 99584);
  auto ws_ub = ascend_ub.GetWithOffset<float>(8192, 132352);
  auto kv_ub = ascend_ub.GetWithOffset<float>(8192, 49920);
  auto vid = AscendC::GetSubBlockIdx();
  if ASCEND_IS_AIC {
    for (int32_t i = 0; i < 128; ++i) {
      AscendC::SetFlag<AscendC::HardEvent::FIX_MTE2>(5);
      AscendC::WaitFlag<AscendC::HardEvent::FIX_MTE2>(5);
      tl::ascend::copy_gm_to_l1<half, 128, 128>(s_l1[0], workspace_3[(cid * 16384)], 128);
      tl::ascend::copy_gm_to_l1<half, 128, 128>(w_l1[0], W[((cid * 2097152) + (i * 16384))], 128);
      AscendC::SetFlag<AscendC::HardEvent::MTE2_M>(1);
      AscendC::WaitFlag<AscendC::HardEvent::MTE2_M>(1);
      tl::ascend::gemm_v0<half, float, 128, 128, 128, false, false>(w_l1[0], s_l1[0], ws_l0[0], ascend_l0a, ascend_l0b, (bool)1);
      AscendC::SetFlag<AscendC::HardEvent::M_FIX>(2);
      AscendC::WaitFlag<AscendC::HardEvent::M_FIX>(2);
      AscendC::PipeBarrier<PIPE_FIX>();
      tl::ascend::copy_l0c_to_gm<float, half, layout::RowMajor, 128, 128, 0>(workspace_1[(cid * 16384)], ws_l0[0], 128);
      AscendC::CrossCoreSetFlag<0x2, PIPE_FIX>(0);
      AscendC::CrossCoreWaitFlag(1);
      tl::ascend::copy_gm_to_l1<half, 128, 128>(k_l1[0], workspace_2[(cid * 16384)], 128);
      tl::ascend::copy_gm_to_l1<half, 128, 128>(v_l1[0], V[((cid * 2097152) + (i * 16384))], 128);
      AscendC::SetFlag<AscendC::HardEvent::MTE2_M>(3);
      AscendC::WaitFlag<AscendC::HardEvent::MTE2_M>(3);
      tl::ascend::gemm_v0<half, float, 128, 128, 128, true, false>(k_l1[0], v_l1[0], kv_l0[0], ascend_l0a, ascend_l0b, (bool)1);
      AscendC::SetFlag<AscendC::HardEvent::M_FIX>(4);
      AscendC::WaitFlag<AscendC::HardEvent::M_FIX>(4);
      tl::ascend::copy_l0c_to_gm<float, half, layout::RowMajor, 128, 128, 0>(workspace_4[(cid * 16384)], kv_l0[0], 128);
      AscendC::CrossCoreSetFlag<0x2, PIPE_FIX>(2);
      AscendC::CrossCoreWaitFlag(3);
    }
  }
  if ASCEND_IS_AIV {
    tl::ascend::Fill<float>(zero_ub[0], 0.000000e+00f, 64);
    tl::ascend::Fill<float>(s_ub[0], 0.000000e+00f, 8192);
    tl::ascend::copy_gm_to_ub<half, 128, 64>(k_ub_half[0], K[((cid * 2097152) + (vid * 8192))], 128);
    tl::ascend::copy_gm_to_ub<float, 128>(g_ub[0], G[(cid * 16384)], 524288);
    AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(0);
    AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(0);
    for (int32_t i_1 = 0; i_1 < 128; ++i_1) {
      tl::ascend::copy_gm_to_ub<half, 128, 64>(u_ub_half[0], U[(((cid * 2097152) + (i_1 * 16384)) + (vid * 8192))], 128);
      AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(2);
      AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(2);
      tl::ascend::copy_ub_to_ub<float, half, 8192>(k_ub[0], k_ub_half[0]);
      tl::ascend::copy_ub_to_ub<float, float, 128>(g_v_ub[0], g_ub[(vid * 64)]);
      // DEBUG: Step 1 - Verify g_v_ub after copy
      if (cid == 0 && vid == 0 && i_1 == 0) {
        AscendC::DumpTensor(g_v_ub, 0, 64);
      }
      AscendC::PipeBarrier<PIPE_ALL>();
      float tmp = g_ub.GetValue(127);
      AscendC::PipeBarrier<PIPE_V>();
      // DEBUG: Step 2 - Verify coeff calculation
      if (cid == 0 && vid == 0 && i_1 == 0) {
        AscendC::DumpTensor(coeff_ub, 0, 64);
      }
      AscendC::Adds(coeff_ub[0], g_v_ub[0], -tmp, 64);
      AscendC::PipeBarrier<PIPE_V>();
      AscendC::Sub(coeff_ub[0], zero_ub[0], coeff_ub[0], 64);
      AscendC::PipeBarrier<PIPE_V>();
      AscendC::Exp(coeff_ub[0], coeff_ub[0], 64);
      // DEBUG: Step 3 - Verify coeff after exp
      if (cid == 0 && vid == 0 && i_1 == 0) {
        AscendC::DumpTensor(coeff_ub, 0, 64);
      }
      AscendC::Exp(g_ub[0], g_ub[0], 128);
      AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(0);
      AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(0);
      tl::ascend::copy_ub_to_ub<float, half, 8192>(u_ub[0], u_ub_half[0]);
      for (int32_t i_2 = 0; i_2 < 16; ++i_2) {
        AscendC::PipeBarrier<PIPE_ALL>();
        float tmp0 = coeff_ub.GetValue((i_2 * 4));
        AscendC::PipeBarrier<PIPE_ALL>();
        float tmp1 = coeff_ub.GetValue(((i_2 * 4) + 1));
        AscendC::PipeBarrier<PIPE_ALL>();
        float tmp2 = coeff_ub.GetValue(((i_2 * 4) + 2));
        AscendC::PipeBarrier<PIPE_ALL>();
        float tmp3 = coeff_ub.GetValue(((i_2 * 4) + 3));
        AscendC::Muls(k_ub[(i_2 * 512)], k_ub[(i_2 * 512)], tmp0, 128);
        AscendC::PipeBarrier<PIPE_V>();
        AscendC::Muls(k_ub[((i_2 * 512) + 128)], k_ub[((i_2 * 512) + 128)], tmp1, 128);
        AscendC::PipeBarrier<PIPE_V>();
        AscendC::Muls(k_ub[((i_2 * 512) + 256)], k_ub[((i_2 * 512) + 256)], tmp2, 128);
        AscendC::PipeBarrier<PIPE_V>();
        AscendC::Muls(k_ub[((i_2 * 512) + 384)], k_ub[((i_2 * 512) + 384)], tmp3, 128);
      }
      // DEBUG: Step 4 - Verify k_ub after multiplication
      if (cid == 0 && vid == 0 && i_1 == 0) {
        AscendC::DumpTensor(k_ub, 0, 8192);
      }
      AscendC::CrossCoreWaitFlag(0);
      AscendC::SetFlag<AscendC::HardEvent::V_MTE2>(3);
      AscendC::WaitFlag<AscendC::HardEvent::V_MTE2>(3);
      tl::ascend::copy_gm_to_ub<half, 128, 64>(u_ub_half[0], workspace_1[((cid * 16384) + (vid * 8192))], 128);
      AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(0);
      AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(0);
      AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(4);
      AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(4);
      tl::ascend::copy_ub_to_ub<float, half, 8192>(ws_ub[0], u_ub_half[0]);
      AscendC::PipeBarrier<PIPE_V>();
      AscendC::Sub(u_ub[0], u_ub[0], ws_ub[0], 8192);
      AscendC::PipeBarrier<PIPE_V>();
      tl::ascend::copy_ub_to_ub<half, float, 8192>(u_ub_half[0], u_ub[0]);
      tl::ascend::copy_ub_to_ub<half, float, 8192>(k_ub_half[0], k_ub[0]);
      AscendC::SetFlag<AscendC::HardEvent::V_MTE3>(0);
      AscendC::WaitFlag<AscendC::HardEvent::V_MTE3>(0);
      AscendC::SetFlag<AscendC::HardEvent::V_MTE3>(5);
      AscendC::WaitFlag<AscendC::HardEvent::V_MTE3>(5);
      tl::ascend::copy_ub_to_gm<half, 128, 64>(V[(((cid * 2097152) + (i_1 * 16384)) + (vid * 8192))], u_ub_half[0], 128);
      tl::ascend::copy_ub_to_gm<half, 128, 64>(workspace_2[((cid * 16384) + (vid * 8192))], k_ub_half[0], 128);
      AscendC::CrossCoreSetFlag<0x2, PIPE_MTE3>(1);
      AscendC::PipeBarrier<PIPE_ALL>();
      float tmp_1 = g_ub.GetValue(127);
      AscendC::Muls(s_ub[0], s_ub[0], tmp_1, 8192);
      AscendC::SetFlag<AscendC::HardEvent::V_MTE2>(0);
      AscendC::WaitFlag<AscendC::HardEvent::V_MTE2>(0);
      AscendC::PipeBarrier<PIPE_ALL>();
      if (i_1 < 127) {
        tl::ascend::copy_gm_to_ub<half, 128, 64>(k_ub_half[0], K[((((cid * 2097152) + (i_1 * 16384)) + (vid * 8192)) + 16384)], 128);
        tl::ascend::copy_gm_to_ub<float, 128>(g_ub[0], G[(((cid * 16384) + (i_1 * 128)) + 128)], 524288);
      }
      AscendC::PipeBarrier<PIPE_ALL>();
      AscendC::CrossCoreWaitFlag(2);
      tl::ascend::copy_gm_to_ub<half, 128, 64>(s_ub_half[0], workspace_4[((cid * 16384) + (vid * 8192))], 128);
      AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(0);
      AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(0);
      AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(6);
      AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(6);
      tl::ascend::copy_ub_to_ub<float, half, 8192>(kv_ub[0], s_ub_half[0]);
      AscendC::PipeBarrier<PIPE_ALL>();
      AscendC::PipeBarrier<PIPE_V>();
      AscendC::Add(s_ub[0], s_ub[0], kv_ub[0], 8192);
      AscendC::PipeBarrier<PIPE_V>();
      // DEBUG: Step 5 - Verify s_ub (final result)
      if (cid == 0 && vid == 0 && i_1 == 127) {
        AscendC::DumpTensor(s_ub, 0, 8192);
      }
      tl::ascend::copy_ub_to_ub<half, float, 8192>(s_ub_half[0], s_ub[0]);
      AscendC::PipeBarrier<PIPE_ALL>();
      if (i_1 < 127) {
        AscendC::SetFlag<AscendC::HardEvent::V_MTE3>(0);
        AscendC::WaitFlag<AscendC::HardEvent::V_MTE3>(0);
        tl::ascend::copy_ub_to_gm<half, 128, 64>(workspace_3[((cid * 16384) + (vid * 8192))], s_ub_half[0], 128);
        tl::ascend::copy_ub_to_gm<half, 128, 64>(S[((((cid * 2097152) + (i_1 * 16384)) + (vid * 8192)) + 16384)], s_ub_half[0], 128);
      }
      AscendC::PipeBarrier<PIPE_ALL>();
      AscendC::CrossCoreSetFlag<0x2, PIPE_MTE3>(3);
    }
    AscendC::SetFlag<AscendC::HardEvent::V_MTE3>(0);
    AscendC::WaitFlag<AscendC::HardEvent::V_MTE3>(0);
    tl::ascend::copy_ub_to_gm<half, 128, 64>(FS[((cid * 16384) + (vid * 8192))], s_ub_half[0], 128);
  }
}

void main_kernel_tiling() {
}

extern "C" void call(uint8_t* K_handle, uint8_t* W_handle, uint8_t* U_handle, uint8_t* G_handle, uint8_t* workspace_1_handle, uint8_t* workspace_2_handle, uint8_t* workspace_3_handle, uint8_t* workspace_4_handle, uint8_t* S_handle, uint8_t* V_handle, uint8_t* FS_handle, aclrtStream stream) {
  uint32_t fftsLen{0};
  uint64_t fftsAddr{0};
  rtGetC2cCtrlAddr(&fftsAddr, &fftsLen);
  main_kernel_tiling();
  main_kernel<<<32, nullptr, stream>>>(K_handle, W_handle, U_handle, G_handle, workspace_1_handle, workspace_2_handle, workspace_3_handle, workspace_4_handle, S_handle, V_handle, FS_handle, fftsAddr);
}