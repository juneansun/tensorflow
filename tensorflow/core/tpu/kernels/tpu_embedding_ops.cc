/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/core/tpu/ops/tpu_embedding_ops.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "tensorflow/compiler/tf2xla/xla_op_kernel.h"
#include "tensorflow/compiler/tf2xla/xla_op_registry.h"
#include "tensorflow/compiler/xla/client/xla_builder.h"
#include "tensorflow/compiler/xla/literal_util.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/c_api_conversions.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/c_api_decl.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/proto_helper.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/status_helper.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_api.h"
#include "tensorflow/compiler/xla/xla_data.pb.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/op_requires.h"
#include "tensorflow/core/framework/tensor.pb.h"
#include "tensorflow/core/protobuf/tpu/tpu_embedding_configuration.pb.h"
#include "tensorflow/core/tpu/kernels/tpu_mesh_state_interface.h"
#include "tensorflow/core/tpu/tpu_configuration.h"

namespace tensorflow {

using xla::LiteralUtil;

namespace {

// This TensorFlow op receives a batch of activations from the
// TpuEmbeddingEngine.
class RecvTPUEmbeddingActivationsOp : public XlaOpKernel {
 public:
  explicit RecvTPUEmbeddingActivationsOp(OpKernelConstruction* ctx)
      : XlaOpKernel(ctx) {
    OP_REQUIRES_OK(ctx, ctx->GetAttr("config", &config_string_));

    OP_REQUIRES(
        ctx, tpu_embedding_config_.ParseFromString(config_string_),
        errors::InvalidArgument("Failed to parse TPUEmbeddingConfiguration "
                                "proto from config attr"));
  }

  ~RecvTPUEmbeddingActivationsOp() override = default;

  void Compile(XlaOpKernelContext* ctx) override {
    ResourceMgr* rm = GetTPUConfigResourceMgr();
    OP_REQUIRES(ctx, rm, errors::Internal("No TPUConfigResourceMgr."));

    tensorflow::tpu::TpuMeshStateInterface* mesh_state;
    OP_REQUIRES_OK(
        ctx, rm->Lookup(rm->default_container(),
                        tensorflow::tpu::kTpuMeshStateInterfaceResourceName,
                        &mesh_state));
    core::ScopedUnref mesh_state_unref(mesh_state);
    OP_REQUIRES(
        ctx, ctx->num_inputs() == 1,
        errors::Internal("Kernel has ", ctx->num_inputs(),
                         " inputs but configuration expects one input"));

    xla::XlaOp deduplication_data = ctx->Input("deduplication_data");

    TpuEmbeddingEngine_RecvActivationsComputation_Params params;
    params.tpu_embedding_config.bytes = config_string_.c_str();
    params.tpu_embedding_config.size = config_string_.size();
    StatusHelper status;
    params.status = status.c_status;
    params.tpu_mesh_state = mesh_state->data();
    auto builder = ctx->builder();
    OP_REQUIRES_VALUE(auto shape, ctx, builder->GetShape(deduplication_data));
    TpuSerializedProto xla_computation_serialized;
    auto proto_cleanup = absl::MakeCleanup([&xla_computation_serialized] {
      StreamExecutor_Tpu_FreeSerializedProto(&xla_computation_serialized);
    });
    params.xla_computation = &xla_computation_serialized;
    XLA_Shape c_shape;
    ApiConverter::ToC(shape, &c_shape);
    auto c_shape_cleanup =
        absl::MakeCleanup([&c_shape] { ApiConverter::Destroy(&c_shape); });
    params.deduplication_data_shape = &c_shape;

    TpuSerializedProto op_sharding_proto_serialized;
    if (ctx->builder()->sharding().has_value()) {
      stream_executor::tpu::SerializeProto(ctx->builder()->sharding().value(),
                                           &op_sharding_proto_serialized);
      params.op_sharding = &op_sharding_proto_serialized;
    } else {
      params.op_sharding = nullptr;
    }
    auto op_sharding_cleanup = absl::MakeCleanup([&] {
      if (params.op_sharding) {
        StreamExecutor_Tpu_FreeSerializedProto(&op_sharding_proto_serialized);
      }
    });

    stream_executor::tpu::OpsApiFn()
        ->TpuEmbeddingEngine_RecvActivationsComputationFn(&params);
    OP_REQUIRES_OK(ctx, status.status());
    auto xla_computation =
        stream_executor::tpu::DeserializeProto<xla::HloModuleProto>(
            xla_computation_serialized);
    auto final_activations =
        xla::Call(builder, xla_computation, {deduplication_data});

    // Ensure that the number of outputs is the same as the number of user
    // tables.
    const int32 output_count =
        (tpu_embedding_config_.feature_descriptor_size() == 0)
            ? tpu_embedding_config_.table_descriptor_size()
            : tpu_embedding_config_.feature_descriptor_size();
    OP_REQUIRES(
        ctx, ctx->num_outputs() == output_count,
        errors::InvalidArgument(
            "Kernel has %d outputs but configuration expects %d outputs.",
            ctx->num_outputs(), output_count));

    for (int32 output_id = 0; output_id < output_count; ++output_id) {
      ctx->SetOutput(output_id,
                     xla::GetTupleElement(final_activations, output_id));
    }
  }

 private:
  tensorflow::tpu::TPUEmbeddingConfiguration tpu_embedding_config_;
  std::string config_string_;

  TF_DISALLOW_COPY_AND_ASSIGN(RecvTPUEmbeddingActivationsOp);
};

REGISTER_XLA_OP(Name("XlaRecvTPUEmbeddingActivations").AllowVariantTypes(),
                RecvTPUEmbeddingActivationsOp);

// This TensorFlow op receives a batch of deduplication data from the
// TPUEmbeddingEngine. The output is a list of R1 tensors containing the weights
// and indices for gathering the embedding vectors.
class RecvTPUEmbeddingDeduplicationDataOp : public XlaOpKernel {
 public:
  explicit RecvTPUEmbeddingDeduplicationDataOp(OpKernelConstruction* ctx)
      : XlaOpKernel(ctx) {
    OP_REQUIRES_OK(ctx, ctx->GetAttr("config", &config_string_));
    OP_REQUIRES(
        ctx,
        tensorflow::tpu::TPUEmbeddingConfiguration().ParseFromString(
            config_string_),
        errors::InvalidArgument("Failed to parse TPUEmbeddingConfiguration "
                                "proto from config attr"));
  }

  ~RecvTPUEmbeddingDeduplicationDataOp() override = default;

  void Compile(XlaOpKernelContext* ctx) override {
    VLOG(1) << "Compile RecvTPUEmbeddingDeduplicationDataOp";

    ResourceMgr* rm = GetTPUConfigResourceMgr();
    OP_REQUIRES(ctx, rm, errors::Internal("No TPUConfigResourceMgr."));

    tensorflow::tpu::TpuMeshStateInterface* mesh_state;
    OP_REQUIRES_OK(
        ctx, rm->Lookup(rm->default_container(),
                        tensorflow::tpu::kTpuMeshStateInterfaceResourceName,
                        &mesh_state));
    core::ScopedUnref mesh_state_unref(mesh_state);

    TpuEmbeddingEngine_RecvTPUEmbeddingDeduplicationDataComputation_Params
        params;

    params.tpu_embedding_config.bytes = config_string_.c_str();
    params.tpu_embedding_config.size = config_string_.size();
    TpuSerializedProto xla_computation_serialized;
    auto proto_cleanup = absl::MakeCleanup([&xla_computation_serialized] {
      StreamExecutor_Tpu_FreeSerializedProto(&xla_computation_serialized);
    });
    params.xla_computation = &xla_computation_serialized;
    StatusHelper status;
    params.status = status.c_status;
    params.tpu_mesh_state = mesh_state->data();

    TpuSerializedProto op_sharding_proto_serialized;
    if (ctx->builder()->sharding().has_value()) {
      stream_executor::tpu::SerializeProto(ctx->builder()->sharding().value(),
                                           &op_sharding_proto_serialized);
      params.op_sharding = &op_sharding_proto_serialized;
    } else {
      params.op_sharding = nullptr;
    }
    auto op_sharding_cleanup = absl::MakeCleanup([&] {
      if (params.op_sharding) {
        StreamExecutor_Tpu_FreeSerializedProto(&op_sharding_proto_serialized);
      }
    });

    stream_executor::tpu::OpsApiFn()
        ->TpuEmbeddingEngine_RecvTPUEmbeddingDeduplicationDataComputationFn(
            &params);
    OP_REQUIRES_OK(ctx, status.status());

    auto xla_computation =
        stream_executor::tpu::DeserializeProto<xla::HloModuleProto>(
            xla_computation_serialized);

    const xla::XlaOp deduplication_data =
        xla::Call(ctx->builder(), xla_computation, {});

    // Ensure that the number of outputs is equal to 1 (for deduplication data).
    OP_REQUIRES(ctx, ctx->num_outputs() == 1,
                errors::InvalidArgument(
                    "Kernel has %d outputs but configuration expects 1 output.",
                    ctx->num_outputs()));

    ctx->SetOutput(0, deduplication_data);
    VLOG(1) << "Compile RecvTPUDeduplicationDataOp done";
  }

 private:
  // TPU Embedding config string.
  std::string config_string_;

  TF_DISALLOW_COPY_AND_ASSIGN(RecvTPUEmbeddingDeduplicationDataOp);
};

REGISTER_XLA_OP(
    Name("XlaRecvTPUEmbeddingDeduplicationData").AllowVariantTypes(),
    RecvTPUEmbeddingDeduplicationDataOp);

// This TensorFlow op sends a batch of gradient and learning rate updates to the
// TpuEmbeddingEngine.
class SendTPUEmbeddingGradientsOp : public XlaOpKernel {
 public:
  explicit SendTPUEmbeddingGradientsOp(OpKernelConstruction* ctx)
      : XlaOpKernel(ctx) {
    OP_REQUIRES_OK(ctx, ctx->GetAttr("config", &config_string_));
    OP_REQUIRES(
        ctx,
        tensorflow::tpu::TPUEmbeddingConfiguration().ParseFromString(
            config_string_),
        errors::InvalidArgument("Failed to parse TPUEmbeddingConfiguration "
                                "proto from config attr"));
  }

  ~SendTPUEmbeddingGradientsOp() override = default;

  void Compile(XlaOpKernelContext* ctx) override {
    VLOG(1) << "Compile SendTPUEmbeddingGradientsOp";

    ResourceMgr* rm = GetTPUConfigResourceMgr();
    OP_REQUIRES(ctx, rm, errors::Internal("No TPUConfigResourceMgr."));

    tensorflow::tpu::TpuMeshStateInterface* mesh_state;
    OP_REQUIRES_OK(
        ctx, rm->Lookup(rm->default_container(),
                        tensorflow::tpu::kTpuMeshStateInterfaceResourceName,
                        &mesh_state));
    core::ScopedUnref mesh_state_unref(mesh_state);

    std::vector<xla::XlaOp> gradients;
    std::vector<TensorShape> tf_gradient_shapes;
    OP_REQUIRES_OK(
        ctx, ctx->InputList("gradients", &gradients, &tf_gradient_shapes));
    std::vector<xla::Shape> gradient_shapes;
    auto builder = ctx->builder();
    gradient_shapes.reserve(gradients.size());
    for (xla::XlaOp op : gradients) {
      gradient_shapes.push_back(builder->GetShape(op).value());
    }

    std::vector<xla::XlaOp> learning_rates;
    std::vector<TensorShape> tf_learning_rate_shapes;
    OP_REQUIRES_OK(ctx, ctx->InputList("learning_rates", &learning_rates,
                                       &tf_learning_rate_shapes));
    std::vector<xla::Shape> learning_rate_shapes;
    learning_rate_shapes.reserve(learning_rates.size());
    for (xla::XlaOp op : learning_rates) {
      learning_rate_shapes.push_back(builder->GetShape(op).value());
    }

    xla::XlaOp deduplication_data = ctx->Input("deduplication_data");

    TpuEmbeddingEngine_SendTPUEmbeddingGradientsComputation_Params params;
    params.tpu_embedding_config.bytes = config_string_.c_str();
    params.tpu_embedding_config.size = config_string_.size();
    TpuSerializedProto xla_computation_serialized;
    auto proto_cleanup = absl::MakeCleanup([&xla_computation_serialized] {
      StreamExecutor_Tpu_FreeSerializedProto(&xla_computation_serialized);
    });
    params.xla_computation = &xla_computation_serialized;
    StatusHelper status;
    params.status = status.c_status;
    params.tpu_mesh_state = mesh_state->data();
    OP_REQUIRES_VALUE(auto deduplication_shape, ctx,
                      builder->GetShape(deduplication_data));
    XLA_Shape gradient_tuple_c_shape;
    params.gradient_tuple_shape = &gradient_tuple_c_shape;
    ApiConverter::ToC(xla::ShapeUtil::MakeTupleShape(gradient_shapes),
                      &gradient_tuple_c_shape);
    XLA_Shape learning_rate_tuple_c_shape;
    params.learning_rate_tuple_shape = &learning_rate_tuple_c_shape;
    ApiConverter::ToC(xla::ShapeUtil::MakeTupleShape(learning_rate_shapes),
                      &learning_rate_tuple_c_shape);
    XLA_Shape deduplication_c_shape;
    params.deduplication_data_shape = &deduplication_c_shape;
    ApiConverter::ToC(deduplication_shape, &deduplication_c_shape);

    auto c_shape_cleanup = absl::MakeCleanup([&gradient_tuple_c_shape,
                                              &learning_rate_tuple_c_shape,
                                              &deduplication_c_shape] {
      ApiConverter::Destroy(&gradient_tuple_c_shape);
      ApiConverter::Destroy(&learning_rate_tuple_c_shape);
      ApiConverter::Destroy(&deduplication_c_shape);
    });
    params.num_inputs = ctx->num_inputs();

    TpuSerializedProto op_sharding_proto_serialized;
    if (ctx->builder()->sharding().has_value()) {
      stream_executor::tpu::SerializeProto(ctx->builder()->sharding().value(),
                                           &op_sharding_proto_serialized);
      params.op_sharding = &op_sharding_proto_serialized;
    } else {
      params.op_sharding = nullptr;
    }
    auto op_sharding_cleanup = absl::MakeCleanup([&] {
      if (params.op_sharding) {
        StreamExecutor_Tpu_FreeSerializedProto(&op_sharding_proto_serialized);
      }
    });

    stream_executor::tpu::OpsApiFn()
        ->TpuEmbeddingEngine_SendTPUEmbeddingGradientsComputationFn(&params);
    OP_REQUIRES_OK(ctx, status.status());

    auto xla_computation =
        stream_executor::tpu::DeserializeProto<xla::HloModuleProto>(
            xla_computation_serialized);

    xla::Call(builder, xla_computation,
              {xla::Tuple(builder, gradients),
               xla::Tuple(builder, learning_rates), deduplication_data});

    VLOG(1) << "Compile SendTPUEmbeddingGradientsOp done";
  }

 private:
  // TPU Embedding config string.
  std::string config_string_;

  TF_DISALLOW_COPY_AND_ASSIGN(SendTPUEmbeddingGradientsOp);
};

REGISTER_XLA_OP(Name("XlaSendTPUEmbeddingGradients").AllowVariantTypes(),
                SendTPUEmbeddingGradientsOp);

// `XLARecvTPUEmbeddingDeduplicationDataOp` gives an XLA Tuple as results, which
// can not be returned as static shape results. `SplitDedupDataOp` is to split
// this XLA tuple into integer and float tensors to return.
class SplitDedupDataOp : public XlaOpKernel {
 public:
  explicit SplitDedupDataOp(OpKernelConstruction* ctx) : XlaOpKernel(ctx) {
    OP_REQUIRES_OK(ctx, ctx->GetAttr("tuple_mask", &tuple_mask_string_));
    OP_REQUIRES(ctx, tuple_mask_tensor_.ParseFromString(tuple_mask_string_),
                errors::InvalidArgument(
                    "Malformed `tuple_mask` attr in SplitDedupData Op."));
  }

  void Compile(XlaOpKernelContext* ctx) override {
    OP_REQUIRES(
        ctx, ctx->num_inputs() == 1,
        errors::InvalidArgument("SplitDedupDataOp must have 1 input but gets ",
                                ctx->num_inputs()));
    const xla::XlaOp& input_tuple = ctx->Input(0);
    xla::XlaBuilder* builder = ctx->builder();

    StatusOr<xla::Shape> tuple_shape = builder->GetShape(input_tuple);
    OP_REQUIRES_OK(ctx, tuple_shape.status());

    const int num_tuple_elements = tuple_shape->tuple_shapes_size();
    OP_REQUIRES(
        ctx,
        tuple_mask_tensor_.tensor_shape().dim(0).size() == num_tuple_elements,
        errors::InvalidArgument(
            "Number of elements in `input` tuple does not match with "
            "`tuple_mask`."));

    if (num_tuple_elements == 0) {
      // Returns empty tensors when tuple is empty.
      ctx->SetOutput(
          0, xla::ConstantLiteral(
                 builder, LiteralUtil::CreateFromDimensions(xla::U32, {0})));
      ctx->SetOutput(
          1, xla::ConstantLiteral(
                 builder, LiteralUtil::CreateFromDimensions(xla::F32, {0})));
      return;
    }

    // Split tuple elements in `input_tuple` into two vectors: integers_vec and
    // floats_vec, corresponding to their mask.
    std::vector<xla::XlaOp> integers_vec, floats_vec;
    for (int i = 0; i < num_tuple_elements; ++i) {
      const xla::XlaOp& element = xla::GetTupleElement(input_tuple, i);
      const int element_type = tuple_mask_tensor_.int_val(2 * i);
      OP_REQUIRES(
          ctx,
          element_type == DedupTupleElementType::kInteger ||
              element_type == DedupTupleElementType::kFloat,
          errors::InvalidArgument(
              "Elements in first column of tuple_mask_tensor are enums of ",
              "DedupTupleElementType, which can only be 0 or 1. Where 0 ",
              "represents integer and 1 represents float. But gets unexpected ",
              "enum = ", element_type));

      if (element_type == DedupTupleElementType::kInteger) {
        integers_vec.push_back(element);
      } else {
        floats_vec.push_back(element);
      }
    }

    // Concatenate elements of integer and floating as return tensors.
    xla::XlaOp integer_tensor = xla::ConcatInDim(builder,
                                                 /*operands=*/integers_vec,
                                                 /*dimension=*/0);
    xla::XlaOp float_tensor = xla::ConcatInDim(builder,
                                               /*operands=*/floats_vec,
                                               /*dimension=*/0);
    ctx->SetOutput(0, integer_tensor);
    ctx->SetOutput(1, float_tensor);
  }

 private:
  std::string tuple_mask_string_;
  tensorflow::TensorProto tuple_mask_tensor_;

  TF_DISALLOW_COPY_AND_ASSIGN(SplitDedupDataOp);
};

REGISTER_XLA_OP(Name("SplitDedupData").AllowVariantTypes(), SplitDedupDataOp);

// MergeDedupDataOp merges integer and floating point tensors back to xla tuple.
class MergeDedupDataOp : public XlaOpKernel {
 public:
  explicit MergeDedupDataOp(OpKernelConstruction* ctx) : XlaOpKernel(ctx) {
    OP_REQUIRES_OK(ctx, ctx->GetAttr("tuple_mask", &tuple_mask_string_));
    OP_REQUIRES(ctx, tuple_mask_tensor_.ParseFromString(tuple_mask_string_),
                errors::InvalidArgument(
                    "Malformed `tuple_mask` attr in MergeDedupData Op"));
  }

  void Compile(XlaOpKernelContext* ctx) override {
    OP_REQUIRES(
        ctx, ctx->num_inputs() == 2,
        errors::InvalidArgument("MergeDedupDataOp expects 2 inputs, but ",
                                "gets ", ctx->num_inputs()));

    // `integer_tensor` should be a 1-D tensor.
    xla::XlaOp integer_tensor = ctx->Input(0);
    StatusOr<xla::Shape> integer_tensor_shape =
        ctx->builder()->GetShape(integer_tensor);
    OP_REQUIRES_OK(ctx, integer_tensor_shape.status());
    OP_REQUIRES(ctx, integer_tensor_shape->rank() == 1,
                errors::InvalidArgument(
                    "Expected rank of integer_vals is 1, but gets, ",
                    integer_tensor_shape->rank()));
    const int64_t num_integers = integer_tensor_shape->dimensions(0);

    // `float_tensor` should be a 1-D tensor.
    xla::XlaOp float_tensor = ctx->Input(1);
    StatusOr<xla::Shape> float_tensor_shape =
        ctx->builder()->GetShape(float_tensor);
    OP_REQUIRES_OK(ctx, float_tensor_shape.status());
    OP_REQUIRES(ctx, float_tensor_shape->rank() == 1,
                errors::InvalidArgument("Expects rank of value is 1, but gets ",
                                        float_tensor_shape->rank()));
    const int64_t num_floats = float_tensor_shape->dimensions(0);

    // Get total number of elements in deduplication data tuple.
    auto builder = ctx->builder();
    const tensorflow::TensorShapeProto& tuple_tensor_shape =
        tuple_mask_tensor_.tensor_shape();
    const int64_t num_tuple_elements = tuple_tensor_shape.dim(0).size();
    if (num_tuple_elements == 0) {
      OP_REQUIRES(
          ctx, num_integers == 0 && num_floats == 0,
          errors::InvalidArgument(
              "Tuple mask indicates empty tuple, but integer_tensor ",
              "shape is ", integer_tensor_shape->DebugString(),
              " float_tensor shape is ", float_tensor_shape->DebugString()));
      ctx->SetOutput(0, xla::Tuple(builder, {}));
      return;
    }
    OP_REQUIRES(
        ctx, tuple_tensor_shape.dim_size() == 2,
        errors::InvalidArgument("Expects rank of tuple mask is 1, but gets ",
                                tuple_tensor_shape.dim_size()));

    std::vector<xla::XlaOp> output_vec;
    output_vec.reserve(num_tuple_elements);

    // Merge elements of integer and float tensor into a tuple.
    int integer_offset = 0;
    int float_offset = 0;
    for (int i = 0; i < num_tuple_elements; ++i) {
      const int element_type = tuple_mask_tensor_.int_val(2 * i);
      const int span_size = tuple_mask_tensor_.int_val(2 * i + 1);
      OP_REQUIRES(
          ctx,
          element_type == DedupTupleElementType::kInteger ||
              element_type == DedupTupleElementType::kFloat,
          errors::InvalidArgument(
              "Elements in first column of tuple_mask_tensor are enums of ",
              "DedupTupleElementType, which can only be 0 or 1. Where 0 ",
              "represents integer and 1 represents float. But gets unexpected ",
              "enum = ", element_type));

      if (element_type == DedupTupleElementType::kInteger) {
        OP_REQUIRES(ctx, integer_offset < num_integers,
                    errors::InvalidArgument(
                        "Offset of integers = ", integer_offset,
                        " exceeds total number of integers = ", num_integers));
        xla::XlaOp integer_slice =
            xla::SliceInDim(integer_tensor,
                            /*start_index=*/integer_offset,
                            /*limit_index*/ integer_offset + span_size,
                            /*stride=*/1, /*dimno=*/0);
        output_vec.push_back(integer_slice);
        integer_offset += span_size;
      } else {
        OP_REQUIRES(ctx, float_offset < num_floats,
                    errors::InvalidArgument(
                        "Offset of integers = ", float_offset,
                        " exceeds total number of floats = ", num_floats));
        xla::XlaOp float_slice =
            xla::SliceInDim(float_tensor,
                            /*start_index=*/float_offset,
                            /*limit_index*/ float_offset + span_size,
                            /*stride=*/1, /*dimno=*/0);
        output_vec.push_back(float_slice);
        float_offset += span_size;
      }
    }
    OP_REQUIRES(ctx, integer_offset == num_integers,
                errors::InvalidArgument(
                    "Number of integers does not match, expect num_integers = ",
                    num_integers, " but actually get = ", integer_offset));
    OP_REQUIRES(ctx, float_offset == num_floats,
                errors::InvalidArgument(
                    "Number of floats does not match, expect num_floats = ",
                    num_floats, " but actually get = ", float_offset));

    xla::XlaOp output_tuple = xla::Tuple(builder, output_vec);
    ctx->SetOutput(0, output_tuple);
  }

 private:
  std::string tuple_mask_string_;
  tensorflow::TensorProto tuple_mask_tensor_;

  TF_DISALLOW_COPY_AND_ASSIGN(MergeDedupDataOp);
};

REGISTER_XLA_OP(Name("MergeDedupData").AllowVariantTypes(), MergeDedupDataOp);

}  // anonymous namespace
}  // namespace tensorflow
