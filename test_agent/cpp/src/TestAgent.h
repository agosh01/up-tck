
/*
 * Copyright (c) 2024 General Motors GTO LLC
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 * SPDX-FileType: SOURCE
 * SPDX-FileCopyrightText: 2024 General Motors GTO LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TEST_AGENT_H_
#define _TEST_AGENT_H_

#include <iostream>
#include <unistd.h>
#include <string>
#include <thread>
#include <optional>
#include <map>
#include <variant>

#include <Constants.h>
#include <spdlog/spdlog.h>
#include <up-client-zenoh-cpp/client/upZenohClient.h>
#include <up-cpp/uri/serializer/LongUriSerializer.h>
#include <up-cpp/uuid/factory/Uuidv8Factory.h>
#include <up-cpp/transport/UTransport.h>
#include <up-core-api/uri.pb.h>
#include <up-core-api/umessage.pb.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <SocketUTransport.h>
#include "ProtoConverter.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <google/protobuf/any.pb.h>
#include <google/protobuf/util/message_differencer.h>

using namespace google::protobuf;
using namespace std;
using namespace rapidjson;

using FunctionType = std::variant<std::function<UStatus(Document&)>, std::function<void(Document&)>>;

/**
 * @class TestAgent
 * @brief Represents a test agent that communicates with a test manager.
 *
 * The TestAgent class is responsible for connecting to a test manager, sending and receiving messages,
 * and handling various commands. It inherits from the UListener class to handle incoming messages.
 */
class TestAgent : public uprotocol::utransport::UListener {
public:
	/**
	 * @brief Constructs a TestAgent object with the specified transport type.
	 * @param transportType The type of transport to be used by the agent.
	 */
	TestAgent(std::string transportType);

	/**
	 * @brief Destroys the TestAgent object.
	 */
	~TestAgent();

	/**
	 * @brief Callback function called when a message is received from the transport layer.
	 * @param transportUMessage The received message.
	 * @return The status of the message processing.
	 */
	UStatus onReceive(uprotocol::utransport::UMessage &transportUMessage) const;

	/**
	 * @brief Connects the agent to the test manager.
	 * @return True if the connection is successful, false otherwise.
	 */
	bool Connect();

	/**
	 * @brief Disconnects the agent from the test manager.
	 * @return The status of the disconnection.
	 */
	int DisConnect();

	/**
	 * @brief Receives data from the test manager.
	 */
	void receiveFromTM();

	/**
	 * @brief Processes the received message.
	 * @param jsonData The JSON data of the received message.
	 */
	void processMessage(Document &jsonData);

	/**
	 * @brief Sends a message to the test manager.
	 * @param proto The message to be sent.
	 * @param action The action associated with the message.
	 * @param strTest_id The ID of the test (optional).
	 */
	void sendToTestManager(const Message &proto, const string &action, const string& strTest_id="") const;

	/**
	 * @brief Sends a message to the test manager.
	 * @param doc The JSON document to be sent.
	 * @param jsonVal The JSON value to be sent.
	 * @param action The action associated with the message.
	 * @param strTest_id The ID of the test (optional).
	 */
	void sendToTestManager(Document &doc, Value &jsonVal, string action, const string& strTest_id="") const;

	/**
	 * @brief Handles the "sendCommand" command received from the test manager.
	 * @param jsonData The JSON data of the command.
	 * @return The status of the command handling.
	 */
	UStatus handleSendCommand(Document &jsonData);

	/**
	 * @brief Handles the "registerListener" command received from the test manager.
	 * @param jsonData The JSON data of the command.
	 * @return The status of the command handling.
	 */
	UStatus handleRegisterListenerCommand(Document &jsonData);

	/**
	 * @brief Handles the "unregisterListener" command received from the test manager.
	 * @param jsonData The JSON data of the command.
	 * @return The status of the command handling.
	 */
	UStatus handleUnregisterListenerCommand(Document &jsonData);

	/**
	 * @brief Handles the "invokeMethod" command received from the test manager.
	 * @param jsonData The JSON data of the command.
	 */
	void handleInvokeMethodCommand(Document &jsonData);

	/**
	 * @brief Handles the "serializeUri" command received from the test manager.
	 * @param jsonData The JSON data of the command.
	 */
	void handleSerializeUriCommand(Document &jsonData);

	/**
	 * @brief Handles the "deserializeUri" command received from the test manager.
	 * @param jsonData The JSON data of the command.
	 */
	void handleDeserializeUriCommand(Document &jsonData);

private:
	int clientSocket_; // The socket used for communication with the test manager.
	struct sockaddr_in mServerAddress_; // The address of the test manager.
	std::shared_ptr<uprotocol::utransport::UTransport> transportPtr_; // The transport layer used for communication.
	std::unordered_map<std::string, FunctionType> actionHandlers_; // The map of action handlers.

	/**
	 * @brief Creates a transport layer object based on the specified transport type.
	 * @param transportType The type of transport to be created.
	 * @return A shared pointer to the created transport layer object.
	 */
	std::shared_ptr<uprotocol::utransport::UTransport> createTransport(const std::string& transportType);

	/**
	 * @brief Writes data to the test manager socket.
	 * @param responseDoc The JSON document containing the response data.
	 * @param action The action associated with the response.
	 */
	void writeDataToTMSocket(Document &responseDoc, string action) const;
};

#endif /*_TEST_AGENT_H_*/
