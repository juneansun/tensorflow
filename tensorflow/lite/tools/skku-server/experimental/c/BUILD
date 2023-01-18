load("//tensorflow/lite:build_def.bzl", "tflite_copts")

package(
    # copybara:uncomment default_applicable_licenses = ["//tensorflow:license"],
    default_visibility = ["benchmark"],
    licenses = ["notice"],
)

package_group(
    name = "benchmark",
    packages = [
        "//tensorflow/lite/tools/skku-server/...",
    ],
)

exports_files(
    ["benchmark_c_api.h"],
    visibility = ["//tensorflow/lite/tools/skku-server/experimental/c:benchmark"],
)

cc_library(
    name = "benchmark_c_api",
    srcs = ["benchmark_c_api.cc"],
    hdrs = [
        "benchmark_c_api.h",
    ],
    copts = tflite_copts(),
    visibility = [
        "benchmark",
    ],
    deps = [
        "//tensorflow/lite/core/c:c_api_types",
        "//tensorflow/lite/tools/skku-server:benchmark_tflite_model_lib",
        "//tensorflow/tsl/util:stats_calculator_portable",
    ],
)
