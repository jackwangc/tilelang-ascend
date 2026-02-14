import tilelang
from tilelang import language as T
import torch
import ctypes

'''
Functionality:
Chunkwisely calculate the prefix sum
'''

lib_path = "./ascendc.so"
lib_path = "./pto.so"

lib = ctypes.CDLL(lib_path)

stream = torch.npu.current_stream()._as_parameter_

def ref_chunk_cumsum(g, C):
	B, H, L = g.shape
	chunk_num = (L + C - 1) // C
	g = g.view(B, H, chunk_num, C)
	g_sum = torch.cumsum(g, dim = -1)
	g_sum = g_sum.view(B, H, L)
	return g_sum

# torch.manual_seed(0)
# g = torch.randn((2, 16, 16384)).npu().to(torch.float)
# steps = 16384
# g = torch.linspace(0, 16384, steps).view(1, 1, steps).expand(2, 16, steps).npu().to(torch.float)
g = torch.ones((2, 16, 16384)).npu().to(torch.float)
s = torch.zeros((2, 16, 16384)).npu().to(torch.float)


def cumsum_ker():
	return lib.call(
		ctypes.c_void_p(g.data_ptr()), 
		ctypes.c_void_p(s.data_ptr()),
		stream
	)

cumsum_ker()

ref_g_sum = ref_chunk_cumsum(g, 128)

torch.testing.assert_close(s.cpu(), ref_g_sum.cpu(), rtol=1e-5, atol=1e-5)
print("Test passed!")