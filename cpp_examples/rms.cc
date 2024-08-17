#include "mirage/kernel/graph.h"
#include "mirage/search/search.h"
#include "mirage/threadblock/graph.h"

using namespace mirage;

int main(int argc, char **argv) {
  kernel::Graph ref_graph;
  {
    kernel::DTensor X = ref_graph.new_input(
        {8, 4096}, type::DT_FLOAT16, layout::DmemRowMajor);
    kernel::DTensor W = ref_graph.new_input(
        {4096, 4096}, type::DT_FLOAT16, layout::DmemRowMajor);
    kernel::DTensor D = ref_graph.rms_norm(X, {X.dim[1]});
    ref_graph.matmul(D, W);
    for (auto const &op : ref_graph.operators) {
      op->fingerprint();
    }
    ProfileResult result;
    float total_runtime = 0.0f;
    for (auto const &op : ref_graph.operators) {
      op->profile(result);
      total_runtime = total_runtime + result.run_time;
    }
    printf("[cudnn kernel graph] Total runtime = %.4lfms\n", total_runtime);
  }
  mirage::cpu::CTensor ref_fp = ref_graph.operators.back()
                                    ->output_tensors[0]
                                    .copy_fingerprint_to_ctensor();
  kernel::Graph graph;
  kernel::DTensor X =
      graph.new_input({8, 4096}, type::DT_FLOAT16, layout::DmemRowMajor);
  kernel::DTensor W = graph.new_input(
      {4096, 4096}, type::DT_FLOAT16, layout::DmemRowMajor);

  {
    threadblock::ExecutionPlan plan;
    plan.ops.push_back({mirage::type::TB_MATMUL_OP, {{0, 0}, {1, 0}}});
    plan.ops.push_back({mirage::type::TB_FORLOOP_ACCUM_RED_LD_RMS_OP, {{0, 0}}});
    plan.ops.push_back({mirage::type::TB_FORLOOP_ACCUM_NO_RED_OP, {{2, 0}}});
    plan.ops.push_back({mirage::type::TB_DIV_OP, {{4, 0}, {3, 0}}});
    plan.input_map.push_back({-1, -1, -1});
    plan.input_map.push_back({1, -1, -1});
    plan.input_smem_layouts = {
        layout::SmemRowMajor,
        layout::SmemRowMajor,
    };
    plan.input_forloop_dim = {1, 0};
    plan.output_map = {1, -1, -1};
    plan.grid_dim = {64, 1, 1};
    plan.block_dim = {128, 1, 1};
    plan.forloop_range = 64;
    plan.reduction_dimx = 64;
    std::vector<kernel::DTensor> outputs = graph.customized({X, W}, plan);
    assert(outputs.size() == 1);
  }

  for (auto const &op : graph.operators) {
    op->fingerprint();
  }
  assert(
      graph.operators.back()->output_tensors[0].has_same_fingerprint(ref_fp));
  ProfileResult result;
  float total_ms = 0.0f;
  for (auto const &op : graph.operators) {
    op->profile(result);
    total_ms = total_ms + result.run_time;
  }
  printf("[2 Block Graphs] Total runtime = %.4lfms\n", total_ms);

  return 0;
}
