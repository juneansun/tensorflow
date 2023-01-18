/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

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

#include <iostream>


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "sched_logger.h"

#define  LOG_TAG    "tflite_sched________client"

// Can be anything if using abstract namespace
#define SOCKET_NAME "serverSocket"
#define BUFFER_SIZE 16

static int data_socket;
static struct sockaddr_un server_addr;

namespace tflite {
namespace benchmark {


void setupClient() {
	char socket_name[108]; // 108 sun_path length max

	data_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (data_socket < 0) {
		LOGE("socket: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// NDK needs abstract namespace by leading with '\0'
	// Ya I was like WTF! too... http://www.toptip.ca/2013/01/unix-domain-socket-with-abstract-socket.html?m=1
	// Note you don't need to unlink() the socket then
	memcpy(&socket_name[0], "\0", 1);
	strcpy(&socket_name[1], SOCKET_NAME);

	// clear for safty
	memset(&server_addr, 0, sizeof(struct sockaddr_un));
	server_addr.sun_family = AF_UNIX; // Unix Domain instead of AF_INET IP domain
	strncpy(server_addr.sun_path, socket_name, sizeof(server_addr.sun_path) - 1); // 108 char max

	// Assuming only one init connection for demo
	int ret = connect(data_socket, (const struct sockaddr *) &server_addr, sizeof(struct sockaddr_un));
	if (ret < 0) {
		LOGE("connect: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	LOGI("Client Setup Complete");
}

int Main(int argc, char** argv) {
  LOGW("client main");

  fprintf(stderr, "client main\n");

  setupClient();

  return EXIT_SUCCESS;
}
}  // namespace benchmark
}  // namespace tflite

int main(int argc, char** argv) { return tflite::benchmark::Main(argc, argv); }
