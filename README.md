# Parvati용 tensorflow lite 빌드 가이드

Parvati tflite와 scheduling server를 빌드하는 가이드입니다.

### Bazel 및 Android 필수 구성 요소 설치하기

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
NDK Version: 19.2.5345600
NDK API Level: 19
Tools Version: 33.0.0
SDK API Level: 33

이들 변수가 설정되지 않은 경우, 스크립트 프롬프트에서 대화식으로 제공해야 합니다. 성공적으로 구성되면 루트 폴더의 `.tf_configure.bazelrc` 파일에 다음과 같은 항목이 생깁니다.

```
build --action_env ANDROID_NDK_HOME="{Android SDK 경로}/ndk/19.2.5345600"
build --action_env ANDROID_NDK_API_LEVEL="19"
build --action_env ANDROID_BUILD_TOOLS_VERSION="33.0.0"
build --action_env ANDROID_SDK_API_LEVEL="33"
build --action_env ANDROID_SDK_HOME="{Android SDK 경로}"
```


### 빌드 및 설치하기


<div align="center">
  <img src="https://www.tensorflow.org/images/tf_logo_horizontal.png">
</div>

[![Python](https://img.shields.io/pypi/pyversions/tensorflow.svg?style=plastic)](https://badge.fury.io/py/tensorflow)
[![PyPI](https://badge.fury.io/py/tensorflow.svg)](https://badge.fury.io/py/tensorflow)
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.4724125.svg)](https://doi.org/10.5281/zenodo.4724125)
[![CII Best Practices](https://bestpractices.coreinfrastructure.org/projects/1486/badge)](https://bestpractices.coreinfrastructure.org/projects/1486)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/tensorflow/tensorflow/badge)](https://api.securityscorecards.dev/projects/github.com/tensorflow/tensorflow)
[![Fuzzing Status](https://oss-fuzz-build-logs.storage.googleapis.com/badges/tensorflow.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:tensorflow)
[![Fuzzing Status](https://oss-fuzz-build-logs.storage.googleapis.com/badges/tensorflow-py.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:tensorflow-py)
[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-v1.4%20adopted-ff69b4.svg)](CODE_OF_CONDUCT.md)

**`Documentation`** |
------------------- |
[![Documentation](https://img.shields.io/badge/api-reference-blue.svg)](https://www.tensorflow.org/api_docs/) |

[TensorFlow](https://www.tensorflow.org/) is an end-to-end open source platform
for machine learning. It has a comprehensive, flexible ecosystem of
[tools](https://www.tensorflow.org/resources/tools),
[libraries](https://www.tensorflow.org/resources/libraries-extensions), and
[community](https://www.tensorflow.org/community) resources that lets
researchers push the state-of-the-art in ML and developers easily build and
deploy ML-powered applications.

TensorFlow was originally developed by researchers and engineers working on the
Google Brain team within Google's Machine Intelligence Research organization to
conduct machine learning and deep neural networks research. The system is
general enough to be applicable in a wide variety of other domains, as well.

TensorFlow provides stable [Python](https://www.tensorflow.org/api_docs/python)
and [C++](https://www.tensorflow.org/api_docs/cc) APIs, as well as
non-guaranteed backward compatible API for
[other languages](https://www.tensorflow.org/api_docs).

Keep up-to-date with release announcements and security updates by subscribing
to
[announce@tensorflow.org](https://groups.google.com/a/tensorflow.org/forum/#!forum/announce).
See all the [mailing lists](https://www.tensorflow.org/community/forums).

## Install

See the [TensorFlow install guide](https://www.tensorflow.org/install) for the
[pip package](https://www.tensorflow.org/install/pip), to
[enable GPU support](https://www.tensorflow.org/install/gpu), use a
[Docker container](https://www.tensorflow.org/install/docker), and
[build from source](https://www.tensorflow.org/install/source).

To install the current release, which includes support for
[CUDA-enabled GPU cards](https://www.tensorflow.org/install/gpu) *(Ubuntu and
Windows)*:

```
$ pip install tensorflow
```

Other devices (DirectX and MacOS-metal) are supported using
[Device plugins](https://www.tensorflow.org/install/gpu_plugins#available_devices).

A smaller CPU-only package is also available:

```
$ pip install tensorflow-cpu
```

To update TensorFlow to the latest version, add `--upgrade` flag to the above
commands.

*Nightly binaries are available for testing using the
[tf-nightly](https://pypi.python.org/pypi/tf-nightly) and
[tf-nightly-cpu](https://pypi.python.org/pypi/tf-nightly-cpu) packages on PyPi.*

#### *Try your first TensorFlow program*

```shell
$ python
```

```python
>>> import tensorflow as tf
>>> tf.add(1, 2).numpy()
3
>>> hello = tf.constant('Hello, TensorFlow!')
>>> hello.numpy()
b'Hello, TensorFlow!'
```

For more examples, see the
[TensorFlow tutorials](https://www.tensorflow.org/tutorials/).

## Contribution guidelines

**If you want to contribute to TensorFlow, be sure to review the
[contribution guidelines](CONTRIBUTING.md). This project adheres to TensorFlow's
[code of conduct](CODE_OF_CONDUCT.md). By participating, you are expected to
uphold this code.**

**We use [GitHub issues](https://github.com/tensorflow/tensorflow/issues) for
tracking requests and bugs, please see
[TensorFlow Discuss](https://groups.google.com/a/tensorflow.org/forum/#!forum/discuss)
for general questions and discussion, and please direct specific questions to
[Stack Overflow](https://stackoverflow.com/questions/tagged/tensorflow).**

The TensorFlow project strives to abide by generally accepted best practices in
open-source software development.

## Patching guidelines

Follow these steps to patch a specific version of TensorFlow, for example, to
apply fixes to bugs or security vulnerabilities:

*   Clone the TensorFlow repo and switch to the corresponding branch for your
    desired TensorFlow version, for example, branch `r2.8` for version 2.8.
*   Apply (that is, cherry pick) the desired changes and resolve any code
    conflicts.
*   Run TensorFlow tests and ensure they pass.
*   [Build](https://www.tensorflow.org/install/source) the TensorFlow pip
    package from source.

## Continuous build status

You can find more community-supported platforms and configurations in the
[TensorFlow SIG Build community builds table](https://github.com/tensorflow/build#community-supported-tensorflow-builds).

### Official Builds

Build Type                    | Status                                                                                                                                                                           | Artifacts
----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------
**Linux CPU**                 | [![Status](https://storage.googleapis.com/tensorflow-kokoro-build-badges/ubuntu-cc.svg)](https://storage.googleapis.com/tensorflow-kokoro-build-badges/ubuntu-cc.html)           | [PyPI](https://pypi.org/project/tf-nightly/)
**Linux GPU**                 | [![Status](https://storage.googleapis.com/tensorflow-kokoro-build-badges/ubuntu-gpu-py3.svg)](https://storage.googleapis.com/tensorflow-kokoro-build-badges/ubuntu-gpu-py3.html) | [PyPI](https://pypi.org/project/tf-nightly-gpu/)
**Linux XLA**                 | [![Status](https://storage.googleapis.com/tensorflow-kokoro-build-badges/ubuntu-xla.svg)](https://storage.googleapis.com/tensorflow-kokoro-build-badges/ubuntu-xla.html)         | TBA
**macOS**                     | [![Status](https://storage.googleapis.com/tensorflow-kokoro-build-badges/macos-py2-cc.svg)](https://storage.googleapis.com/tensorflow-kokoro-build-badges/macos-py2-cc.html)     | [PyPI](https://pypi.org/project/tf-nightly/)
**Windows CPU**               | [![Status](https://storage.googleapis.com/tensorflow-kokoro-build-badges/windows-cpu.svg)](https://storage.googleapis.com/tensorflow-kokoro-build-badges/windows-cpu.html)       | [PyPI](https://pypi.org/project/tf-nightly/)
**Windows GPU**               | [![Status](https://storage.googleapis.com/tensorflow-kokoro-build-badges/windows-gpu.svg)](https://storage.googleapis.com/tensorflow-kokoro-build-badges/windows-gpu.html)       | [PyPI](https://pypi.org/project/tf-nightly-gpu/)
**Android**                   | [![Status](https://storage.googleapis.com/tensorflow-kokoro-build-badges/android.svg)](https://storage.googleapis.com/tensorflow-kokoro-build-badges/android.html)               | [Download](https://bintray.com/google/tensorflow/tensorflow/_latestVersion)
**Raspberry Pi 0 and 1**      | [![Status](https://storage.googleapis.com/tensorflow-kokoro-build-badges/rpi01-py3.svg)](https://storage.googleapis.com/tensorflow-kokoro-build-badges/rpi01-py3.html)           | [Py3](https://storage.googleapis.com/tensorflow-nightly/tensorflow-1.10.0-cp34-none-linux_armv6l.whl)
**Raspberry Pi 2 and 3**      | [![Status](https://storage.googleapis.com/tensorflow-kokoro-build-badges/rpi23-py3.svg)](https://storage.googleapis.com/tensorflow-kokoro-build-badges/rpi23-py3.html)           | [Py3](https://storage.googleapis.com/tensorflow-nightly/tensorflow-1.10.0-cp34-none-linux_armv7l.whl)
**Libtensorflow MacOS CPU**   | Status Temporarily Unavailable                                                                                                                                                   | [Nightly Binary](https://storage.googleapis.com/libtensorflow-nightly/prod/tensorflow/release/macos/latest/macos_cpu_libtensorflow_binaries.tar.gz) [Official GCS](https://storage.googleapis.com/tensorflow/)
**Libtensorflow Linux CPU**   | Status Temporarily Unavailable                                                                                                                                                   | [Nightly Binary](https://storage.googleapis.com/libtensorflow-nightly/prod/tensorflow/release/ubuntu_16/latest/cpu/ubuntu_cpu_libtensorflow_binaries.tar.gz) [Official GCS](https://storage.googleapis.com/tensorflow/)
**Libtensorflow Linux GPU**   | Status Temporarily Unavailable                                                                                                                                                   | [Nightly Binary](https://storage.googleapis.com/libtensorflow-nightly/prod/tensorflow/release/ubuntu_16/latest/gpu/ubuntu_gpu_libtensorflow_binaries.tar.gz) [Official GCS](https://storage.googleapis.com/tensorflow/)
**Libtensorflow Windows CPU** | Status Temporarily Unavailable                                                                                                                                                   | [Nightly Binary](https://storage.googleapis.com/libtensorflow-nightly/prod/tensorflow/release/windows/latest/cpu/windows_cpu_libtensorflow_binaries.tar.gz) [Official GCS](https://storage.googleapis.com/tensorflow/)
**Libtensorflow Windows GPU** | Status Temporarily Unavailable                                                                                                                                                   | [Nightly Binary](https://storage.googleapis.com/libtensorflow-nightly/prod/tensorflow/release/windows/latest/gpu/windows_gpu_libtensorflow_binaries.tar.gz) [Official GCS](https://storage.googleapis.com/tensorflow/)

## Resources

*   [TensorFlow.org](https://www.tensorflow.org)
*   [TensorFlow Tutorials](https://www.tensorflow.org/tutorials/)
*   [TensorFlow Official Models](https://github.com/tensorflow/models/tree/master/official)
*   [TensorFlow Examples](https://github.com/tensorflow/examples)
*   [TensorFlow Codelabs](https://codelabs.developers.google.com/?cat=TensorFlow)
*   [TensorFlow Blog](https://blog.tensorflow.org)
*   [Learn ML with TensorFlow](https://www.tensorflow.org/resources/learn-ml)
*   [TensorFlow Twitter](https://twitter.com/tensorflow)
*   [TensorFlow YouTube](https://www.youtube.com/channel/UC0rqucBdTuFTjJiefW5t-IQ)
*   [TensorFlow model optimization roadmap](https://www.tensorflow.org/model_optimization/guide/roadmap)
*   [TensorFlow White Papers](https://www.tensorflow.org/about/bib)
*   [TensorBoard Visualization Toolkit](https://github.com/tensorflow/tensorboard)
*   [TensorFlow Code Search](https://cs.opensource.google/tensorflow/tensorflow)

Learn more about the
[TensorFlow community](https://www.tensorflow.org/community) and how to
[contribute](https://www.tensorflow.org/community/contribute).

## Courses

*   [Deep Learning with Tensorflow from Edx](https://www.edx.org/course/deep-learning-with-tensorflow)
*   [DeepLearning.AI TensorFlow Developer Professional Certificate from Coursera](https://www.coursera.org/specializations/tensorflow-in-practice)
*   [TensorFlow: Data and Deployment from Coursera](https://www.coursera.org/specializations/tensorflow-data-and-deployment)
*   [Getting Started with TensorFlow 2 from Coursera](https://www.coursera.org/learn/getting-started-with-tensor-flow2)
*   [TensorFlow: Advanced Techniques from Coursera](https://www.coursera.org/specializations/tensorflow-advanced-techniques)
*   [TensorFlow 2 for Deep Learning Specialization from Coursera](https://www.coursera.org/specializations/tensorflow2-deeplearning)
*   [Intro to TensorFlow for A.I, M.L, and D.L from Coursera](https://www.coursera.org/learn/introduction-tensorflow)
*   [Machine Learning with TensorFlow on GCP from Coursera](https://www.coursera.org/specializations/machine-learning-tensorflow-gcp)
*   [Intro to TensorFlow for Deep Learning from Udacity](https://www.udacity.com/course/intro-to-tensorflow-for-deep-learning--ud187)
*   [Introduction to TensorFlow Lite from Udacity](https://www.udacity.com/course/intro-to-tensorflow-lite--ud190)

## License

[Apache License 2.0](LICENSE)
