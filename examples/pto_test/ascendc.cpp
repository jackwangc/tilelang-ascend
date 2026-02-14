#include "tl_templates/ascend/common.h"
#include "acl/acl.h"
#include <runtime/rt_ffts.h>
using namespace Catlass;
using uint = unsigned int;
using uchar = unsigned char;
using ushort = unsigned short;

extern "C" __global__ __aicore__ void main_kernel( GM_ADDR G_handle,  GM_ADDR S_handle, uint64_t fftsAddr) {
  KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_MIX_AIC_1_2);
  AscendC::TPipe pipe;

  AscendC::GlobalTensor<float> G;
  G.SetGlobalBuffer((__gm__ float*)G_handle);
  AscendC::GlobalTensor<float> S;
  S.SetGlobalBuffer((__gm__ float*)S_handle);

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
  auto s_ub = ascend_ub.GetWithOffset<float>(1024,0);
  auto g_ub = ascend_ub.GetWithOffset<float>(1024,4096);
  auto vid = AscendC::GetSubBlockIdx();
  if ASCEND_IS_AIV {
    tl::ascend::Fill<float>(s_ub[0], 0.000000e+00f, 1024);
    tl::ascend::copy_gm_to_ub<float, 1024>(g_ub[0], G[((((cid / 16) * 32768) + (vid * 16384)) + ((cid % 16) * 1024))], 524288);
    AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(0);
    AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(0);
    // gub 无问题

    for (int32_t ii = 0; ii < 8; ++ii) {
      s_ub.SetValue((ii * 128), g_ub.GetValue((ii * 128)));
      if (cid == 0 && vid == 0 && ii == 1) {
        AscendC::DumpTensor(s_ub[0],0,528);
      }
      for (int32_t i = 1; i < 128; ++i) {
        float tmp2 = (s_ub.GetValue((((ii * 128) + i) - 1)) + g_ub.GetValue(((ii * 128) + i)));
        s_ub.SetValue(((ii * 128) + i), tmp2);
      }
    }
    AscendC::SetFlag<AscendC::HardEvent::V_MTE3>(0);
    AscendC::WaitFlag<AscendC::HardEvent::V_MTE3>(0);

    tl::ascend::copy_ub_to_gm<float, 1024>(S[((((cid / 16) * 32768) + (vid * 16384)) + ((cid % 16) * 1024))], s_ub[0], 524288);
  }
}

void main_kernel_tiling() {
}

extern "C" void call(uint8_t* G_handle, uint8_t* S_handle, aclrtStream stream) {
  uint32_t fftsLen{0};
  uint64_t fftsAddr{0};
  rtGetC2cCtrlAddr(&fftsAddr, &fftsLen);
  main_kernel_tiling();
  main_kernel<<<256, nullptr, stream>>>(G_handle, S_handle, fftsAddr);
}