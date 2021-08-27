#include "testbenches/test_v_kernel.h"
#include "dma/axis_lib.h"

#ifdef __VITIS_HLS__
#include "hls_vector.h"
#endif
#include "ap_int.h"
#include "hls_stream.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char const *argv[]) {
#ifdef COSIM_DESIGN
  srand(1);
#else
  srand(1);
  // srand(time(NULL));
#endif
  std::cout << "[INFO] Starting HlsKernelV test." << std::endl;
#ifndef __VITIS_HLS__
  return 0;
#else
  int num_active_inputs = testv::params::N;
  int output_size = testv::params::H;
  int num_refinements = testv::params::R;
  if (argc >= 2) {
    num_active_inputs = atoi(argv[1]);
  }
  if (argc >= 3) {
    output_size = atoi(argv[2]);
  }
  if (argc >= 4) {
    num_refinements = atoi(argv[3]);
  }
  const int kMaxRefinements = num_refinements;
  hls::vector<int, testv::params::N> num_refinements_vect = hls::vector<int, testv::params::N>(kMaxRefinements);
  for (int i = testv::params::N; i >= 0; --i) {
    num_refinements_vect[i] = kMaxRefinements;
    // int R_tmp = kMaxRefinements - 2 * (testv::params::N - i - 1);
    // num_refinements_vect[i] = R_tmp > 0 ? R_tmp : 1;
  }
  const int kNumTests = 2;
  const int kNumActiveInputs = (num_active_inputs > testv::params::N) ? testv::params::N : num_active_inputs;
  const int kOutputSize = (output_size > testv::params::H) ? testv::params::H : output_size;
  const int kNumTilesV = kOutputSize / testv::params::Tv;
  typedef typename testv::params::ActivationD ActivationType;
  assert(testv::params::H == testv::params::PrunedSizeV); // No pruning.

  ActivationType xus[kMaxRefinements][testv::params::N][testv::params::G] = {ActivationType(0.001)};
  ActivationType v[kMaxRefinements][testv::params::PrunedSizeV][testv::params::G] = {ActivationType(0.001)};
  ActivationType y_gold[testv::params::N][testv::params::G][testv::params::H] = {0};

  for (int i = 0; i < kMaxRefinements; ++i) {
    for (int j = 0; j < testv::params::G; ++j) {
      for (int k = 0; k < testv::params::N; ++k) {
        xus[i][k][j] = rand() * 0.00001;
      }
      for (int k = 0; k < testv::params::PrunedSizeV; ++k) {
        v[i][k][j] = rand() * 0.00001;
      }
    }
  }

  for (int i = 0; i < kMaxRefinements; ++i) {
    for (int j = 0; j < kNumActiveInputs; ++j) {
      if (i < num_refinements_vect[j]) {
        for (int k = 0; k < kOutputSize; ++k) {
          for (int ii = 0; ii < testv::params::G; ++ii) {
            y_gold[j][ii][k] += v[i][k][ii] * xus[i][j][ii];
          }
        }
      }
    }
  }

  hls::stream<typename testv::params::VectG_AxiPacketType> xus_port("xus_port");
  hls::stream<typename testv::params::VectTvAxiPacketType> v_port("v_port");
  hls::stream<typename testv::params::VectGTvAxiPacketType> y_port("y_port");

  auto xus_axis = svd::AxiStreamPort<testv::params::VectG_AxiWidth>(xus_port);
  auto v_axis = svd::AxiStreamPort<testv::params::VectTvAxiWidth>(v_port);
  auto y_axis = svd::AxiStreamPort<testv::params::VectGTvAxiWidth>(y_port);

  int num_errors = 0;
  for (int t = 0; t < kNumTests; ++t) {
    // Stream XUS
    typename testv::params::VectG_Type xus_val;
    for (int i = 0; i < kMaxRefinements; ++i) {
      for (int j = 0; j < kNumActiveInputs; ++j) {
        if (i < num_refinements_vect[j]) {
          for (int k = 0; k < testv::params::G; ++k) {
            xus_val[k] = xus[i][j][k];
          }
          xus_axis.PushVector<ActivationType, testv::params::G>(xus_val);
        }
      }
    }
    // Stream V
    typename testv::params::VectTvType v_val;
    for (int i = 0; i < kMaxRefinements; ++i) {
      for (int k = 0; k < kNumTilesV; ++k) {
        for (int j = 0; j < testv::params::G; ++j) {
          for (int ii = 0; ii < testv::params::Tv; ++ii) {
            v_val[ii] = v[i][k * testv::params::Tv + ii][j];
          }
          v_axis.PushVector<ActivationType, testv::params::Tv>(v_val);
        }
      }
    }
  }
  std::cout << "[INFO] Starting HlsKernelV." << std::endl;
  for (int t = 0; t < kNumTests; ++t) {
    HlsKernelV(kNumActiveInputs, kOutputSize, num_refinements_vect, xus_port, v_port, y_port);
  }
  for (int t = 0; t < kNumTests; ++t) {
    std::cout << "[INFO] Checking results test n." << t << std::endl;
    int test_errors = 0;
    int num_elems = 0;
      for (int i = 0; i < kNumActiveInputs; ++i) {
    for (int j = 0; j < kNumTilesV; ++j) {
        const int kGTv = testv::params::G * testv::params::Tv;
        auto y_val = y_axis.PopVector<ActivationType, kGTv>();
        for (int k = 0; k < testv::params::Tv; ++k) {
          for (int ii = 0; ii < testv::params::G; ++ii) {
            // ActivationType y_gold[testv::params::N][testv::params::G][testv::params::H] = {0};
            if (y_val[k * testv::params::G + ii] != y_gold[i][ii][j * testv::params::Tv + k]) {
              std::cout << "N:" << i << "][NTv:" << j << "][Tv:" << k << "][G:"
                        << ii << "] test/gold: "
                        << y_val[k * testv::params::G + ii] << " / "
                        << y_gold[i][ii][j * testv::params::Tv + k] << std::endl;
              ++test_errors;
            } else {
              std::cout << "\tN:" << i << "][NTv:" << j << "][Tv:" << k << "][G:"
                        << ii << "] test/gold: "
                        << y_val[k * testv::params::G + ii] << " / "
                        << y_gold[i][ii][j * testv::params::Tv + k] << std::endl;
            }
            ++num_elems;
          }
        }
      }
    }
    std::cout << "[INFO] Number of mismatches per test / total: " << test_errors
              << " / " << num_elems << std::endl;
    num_errors += test_errors;
  }
  std::cout << "[INFO] Total number of mismatches: " << num_errors << std::endl;
  for (int i = testv::params::N; i >= 0; --i) {
    std::cout << num_refinements_vect[i] << std::endl;
    // int R_tmp = kMaxRefinements - 2 * (testv::params::N - i - 1);
    // num_refinements_vect[i] = R_tmp > 0 ? R_tmp : 1;
  }
  return 0; // num_errors;
#endif // end __VITIS_HLS__
}