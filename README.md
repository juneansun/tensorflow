# Parvati용 tensorflow lite 빌드 가이드

Parvati는 tflite 벤치마크 도구(benchmark tool)와 Scheduling Server를 기반으로 작동합니다
본 가이드는 Parvati tflite와 scheduling server 빌드에 대한 가이드입니다.
빌드 및 실행은 안드로이드 스마트폰을 타겟으로 합니다

### 빌드환경 구성: Bazel 및 Android 필수 구성 요소 설치하기

Bazel은 TensorFlow의 기본 빌드 시스템입니다. Bazel을 사용하여 빌드하려면 시스템에 Android NDK 및 SDK가 설치되어 있어야 합니다.

1. 최신 버전의 [Bazel 빌드 시스템](https://bazel.build/versions/master/docs/install.html?hl=ko)을 설치합니다.
2. 네이티브(C/C++) TensorFlow Lite 코드를 빌드하려면 Android NDK가 필요합니다. 현재 권장되는 버전은 19c이며 [여기](https://developer.android.com/ndk/downloads/older_releases.html?hl=ko#ndk-19c-downloads)에서 찾을 수 있습니다.
3. Android SDK 및 빌드 도구는 [여기](https://developer.android.com/tools/revisions/build-tools.html?hl=ko)에서 얻거나, [Android Studio](https://developer.android.com/studio/index.html?hl=ko)의 일부로 얻을 수도 있습니다. TensorFlow Lite 빌드에 권장되는 버전은 Build tools API >= 23입니다.

### WORKSPACE 및 .bazelrc 구성하기

루트 TensorFlow 체크아웃 디렉토리에서 `./configure` 스크립트를 실행하고 스크립트가 Android 빌드용 `./WORKSPACE`를 대화식으로 구성할 것인지 물으면 "Yes"를 선택합니다. 스크립트는 다음 환경 변수를 사용하여 설정 구성을 시도합니다.

- `ANDROID_SDK_HOME`
- `ANDROID_SDK_API_LEVEL`
- `ANDROID_NDK_HOME`
- `ANDROID_NDK_API_LEVEL`

Parvati 빌드가 가능한 확인된 설정은 아래와 같습니다

* NDK Version: 19.2.5345600
* NDK API Level: 19
* Tools Version: 33.0.0
* SDK API Level: 33

이들 변수가 설정되지 않은 경우, 스크립트 프롬프트에서 대화식으로 제공해야 합니다. 성공적으로 구성되면 루트 폴더의 `.tf_configure.bazelrc` 파일에 다음과 같은 항목이 생깁니다.

```
build --action_env ANDROID_NDK_HOME="{Android SDK 경로}/ndk/19.2.5345600"
build --action_env ANDROID_NDK_API_LEVEL="19"
build --action_env ANDROID_BUILD_TOOLS_VERSION="33.0.0"
build --action_env ANDROID_SDK_API_LEVEL="33"
build --action_env ANDROID_SDK_HOME="{Android SDK 경로}"
```

### 벤치마크 도구 빌드 및 설치

TensorFlow Lite 벤치마크 도구는 현재 다음과 같은 중요한 성능 지표에 대한 통계를 측정하고 계산합니다.

- 초기화 시간
- 워밍업 상태의 추론 시간
- 정상 상태의 추론 시간
- 초기화 시간 동안의 메모리 사용량
- 전체 메모리 사용량

벤치마크 도구는 Android 및 iOS용 벤치마크 앱과 기본 명령줄 바이너리로 사용할 수 있으며, 모두 동일한 핵심 성능 측정 로직을 공유합니다. 런타임 환경의 차이로 인해 사용 가능한 옵션 및 출력 형식이 약간 다릅니다.

Parvati는 벤치마크 도구와 Scheduling Server가 같이 작동하므로 함께 빌드합니다

```
bazel build -c dbg --config=android_arm64 tensorflow/lite/tools/my_scheduler:sched_server
bazel build -c dbg --config=android_arm64 tensorflow/lite/tools/benchmark:benchmark_model 
```

빌드완료 산출물은 adb 명령어를 이용해 스마트폰에 설치합니다
```
adb push bazel-bin/tensorflow/lite/tools/my_scheduler/sched_server /data/local/tmp 
adb push bazel-bin/tensorflow/lite/tools/benchmark/benchmark_model /data/local/tmp/
```

### 실행

먼저 Scheduling Server를 실행하고 벤치마크 도구를 실행합니다

벤치마크 실행시 옵션은 다음 링크를 참고합니다 [Tflite 벤치마크 도구 안내](https://github.com/tensorflow/tensorflow/tree/master/tensorflow/lite/tools/benchmark#readme)

Parvati는 CPU, GPU, DSP, NPU 를 선택적으로 사용 가능하므로 --use_gpu, --use_hexagon, --use_nnapi, --nnapi_accelerator_name=google-edgetpu 를 모두 넣어줍니다
```
adb shell "/data/local/tmp/sched_server" ;
adb shell "taskset f0 /data/local/tmp/benchmark_model \
  --graph=/data/local/tmp/tflite_models/mobilenet_v2_1.0_224_quant.tflite \
  --use_gpu=true \
  --use_hexagon=true \
  --use_nnapi=true \
  --nnapi_accelerator_name=google-edgetpu \
  --min_secs=10 --num_runs=1"
```

## License

[Apache License 2.0](LICENSE)
