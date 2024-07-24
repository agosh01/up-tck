// SPDX-FileCopyrightText: 2024 Contributors to the Eclipse Foundation
//
// See the NOTICE file(s) distributed with this work for additional
// information regarding copyright ownership.
//
// This program and the accompanying materials are made available under the
// terms of the Apache License Version 2.0 which is available at
// https://www.apache.org/licenses/LICENSE-2.0
//
// SPDX-License-Identifier: Apache-2.0

#include <APIWrapper.h>

using namespace rapidjson;
using namespace uprotocol::v1;
using namespace std;

APIWrapper::APIWrapper(const std::string transportType)
    : transportType_(transportType) {
	// Log the creation of the APIWrapper with the specified transport type
	spdlog::info(
	    "APIWrapper::APIWrapper(), Creating APIWrapper with transport type: {}",
	    transportType);

	// Create default transport with preset uri
	def_src_uuri_.set_authority_name("TestAgentCpp");
	def_src_uuri_.set_ue_id(0x18000);
	def_src_uuri_.set_ue_version_major(1);
	def_src_uuri_.set_resource_id(0);

	transportPtr_ = createTransport(def_src_uuri_);

	// If the transport creation failed, log an error and exit
	if (!transportPtr_) {
		spdlog::error("APIWrapper::APIWrapper(), Failed to create transport");
		exit(1);
	}
}

APIWrapper::~APIWrapper() {}

void APIWrapper::sendToTestManager(const uprotocol::v1::UMessage& proto,
                                   const std::string& action,
                                   const std::string& strTest_id) const {}

void APIWrapper::sendToTestManager(const uprotocol::v1::UStatus& status,
                                   const std::string& action,
                                   const std::string& strTest_id) const {};

void APIWrapper::sendToTestManager(rapidjson::Document& doc,
                                   rapidjson::Value& jsonVal,
                                   const std::string action,
                                   const std::string& strTest_id) const {}

std::shared_ptr<uprotocol::transport::UTransport> APIWrapper::createTransport(
    const UUri& uri) {
	// If the transport type is "socket", create a new SocketUTransport.
	if (transportType_ == "socket") {
		return std::make_shared<SocketUTransport>(uri);
	} else {
		// If the transport type is neither "socket" nor "zenoh", log an error
		// and return null.
		spdlog::error("Invalid transport type: {}", transportType_);
		return nullptr;
	}
}

UStatus APIWrapper::handleCreateTransportCommand(Document& jsonData) {
	UStatus status;

	// Create a v1 UUri object from the provided jsonData.
	def_src_uuri_ = ProtoConverter::distToUri(jsonData[Constants::DATA],
	                                          jsonData.GetAllocator());

	// Create transport with the created URI
	transportPtr_ = createTransport(def_src_uuri_);

	// Check if the transport creation failed
	if (!transportPtr_) {
		spdlog::error(
		    "APIWrapper::handleCreateTransportCommand, Unable to create "
		    "transport");
		status.set_code(UCode::FAILED_PRECONDITION);
		status.set_message("Unable to create transport");
		return status;
	}

	status.set_code(UCode::OK);
	return status;
}

UStatus APIWrapper::addHandleToUriCallbackMap(CommunicationVariantType&& handle,
                                              const UUri& uri) {
	// Insert into the map using emplace to avoid deleted move assignment
	uriCallbackMap_.emplace(uri.SerializeAsString(), std::move(handle));

	UStatus status;
	status.set_code(UCode::OK);
	return status;
}

UStatus APIWrapper::removeHandleOrProvideError(Document& jsonData) {
	auto uri = ProtoConverter::distToUri(jsonData[Constants::DATA],
	                                     jsonData.GetAllocator());

	return removeHandleOrProvideError(uri);
}

UStatus APIWrapper::removeHandleOrProvideError(const UUri& uri) {
	UStatus status;

	// Remove the rpc handles that are not valid
	rpcClientHandles_.erase(
	    std::remove_if(
	        rpcClientHandles_.begin(), rpcClientHandles_.end(),
	        [](const uprotocol::communication::RpcClient::InvokeHandle&
	               handle) { return !handle; }),
	    rpcClientHandles_.end());

	auto count = uriCallbackMap_.erase(uri.SerializeAsString());
	if (count == 0) {
		spdlog::error("APIWrapper::removeCallbackToMap, URI not found.");
		status.set_code(UCode::NOT_FOUND);
		return status;
	}

	status.set_code(UCode::OK);

	return status;
}

UStatus APIWrapper::handleSendCommand(Document& jsonData) {
	// Create a v1 UMessage object.
	UMessage umsg;
	// Convert the jsonData to a proto message.
	ProtoConverter::dictToProto(jsonData[Constants::DATA], umsg,
	                            jsonData.GetAllocator());
	// Log the UMessage string.
	spdlog::info("APIWrapper::handleSendCommand(), umsg string is: {}",
	             umsg.DebugString());

	// Send the UMessage and return the status.
	return transportPtr_->send(umsg);
}

UStatus APIWrapper::handleRegisterListenerCommand(Document& jsonData) {
	// Create a v1 UUri object.
	auto uri = ProtoConverter::distToUri(jsonData[Constants::DATA],
	                                     jsonData.GetAllocator());

	// Register the lambda function as a listener for messages on the specified
	// URI.
	auto result = transportPtr_->registerListener(
	    // sink_uri =
	    uri,
	    // callback =
	    [this](const UMessage& transportUMessage) {
		    spdlog::info("APIWrapper::onReceive(), received.");
		    spdlog::info("APIWrapper::onReceive(), umsg string is: {}",
		                 transportUMessage.DebugString());

		    // Send the message to the Test Manager with a predefined response.
		    sendToTestManager(transportUMessage,
		                      Constants::RESPONSE_ON_RECEIVE);
	    });

	return result.has_value()
	           ? addHandleToUriCallbackMap(std::move(result).value(), uri)
	           : result.error();
}

UStatus APIWrapper::handleInvokeMethodCommand(Document& jsonData) {
	// Get the data and test ID from the JSON document.
	Value& data = jsonData[Constants::DATA];
	std::string strTest_id = jsonData[Constants::TEST_ID].GetString();
	UStatus status;

	auto format = ProtoConverter::distToUPayFormat(
	    data[Constants::ATTRIBUTES][Constants::FORMAT]);

	if (!format.has_value()) {
		spdlog::error(
		    "APIWrapper::handleInvokeMethodCommand(), Invalid format "
		    "received.");
		status.set_code(UCode::NOT_FOUND);
		status.set_message("Invalid payload format received in the request.");
		return status;
	}

	// Build payload
	std::string valueStr = std::string(data[Constants::PAYLOAD].GetString());
	uprotocol::datamodel::builder::Payload payload(valueStr, format.value());

	// Create a UUri object.
	auto uri = ProtoConverter::distToUri(
	    data[Constants::ATTRIBUTES][Constants::SINK], jsonData.GetAllocator());

	// Log the UUri string.
	spdlog::debug(
	    "APIWrapper::handleInvokeMethodCommand(), UUri in string format is :  "
	    "{}",
	    uri.DebugString());

	std::chrono::milliseconds ttl = std::chrono::milliseconds(10000);

	// Serialize the URI
	std::string serializedUri = uri.SerializeAsString();

	// Check and add RpcClient if not exists
	MultiMapUtils::checkOrAdd(
	    uriCallbackMap_, serializedUri,
	    uprotocol::communication::RpcClient(transportPtr_, std::move(uri),
	                                        UPriority::UPRIORITY_CS4, ttl,
	                                        format.value()));

	// Define a lambda function for handling received messages.
	// TODO: Temprary using Constants::RESPONSE_ON_RECEIVE to integrate with test manager
	auto callBack = [this, strTest_id](auto responseOrError) {
		spdlog::info("APIWrapper::handleInvokeMethodCommand(), response received.");

		if (!responseOrError.has_value()) {
			auto& status = responseOrError.error();
			spdlog::error("APIWrapper rpc callback, Error received: {}",
			              status.message());
			sendToTestManager(std::move(responseOrError).error(),
			                  Constants::RESPONSE_ON_RECEIVE);
		}
		sendToTestManager(std::move(responseOrError).value(),
		                  Constants::RESPONSE_ON_RECEIVE);
	};

	// Retrieve the rpc client as it is already added or existing
	auto& rpcClient = std::get<uprotocol::communication::RpcClient>(
	    uriCallbackMap_.find(serializedUri)->second);

	// Invoke the method
	auto handle =
	    rpcClient.invokeMethod(std::move(payload), std::move(callBack));
	rpcClientHandles_.push_back(std::move(handle));

	status.set_code(UCode::OK);
	return status;
}

UStatus APIWrapper::handleRpcServerCommand(Document& jsonData) {
	// Get the data and test ID from the JSON document.
	Value& data = jsonData[Constants::DATA];
	std::string strTest_id = jsonData[Constants::TEST_ID].GetString();
	UStatus status;

	auto format = ProtoConverter::distToUPayFormat(
	    data[Constants::ATTRIBUTES][Constants::FORMAT]);

	if (!format.has_value()) {
		spdlog::error(
		    "APIWrapper::handleRpcServerCommand(), Invalid format received.");
		status.set_code(UCode::NOT_FOUND);
		status.set_message("Invalid payload format received in the request.");
		return status;
	}

	// Build payload
	std::string valueStr = std::string(data[Constants::PAYLOAD].GetString());

	uprotocol::datamodel::builder::Payload payload(valueStr, format.value());

	// Create a UUri object.
	auto uri = ProtoConverter::distToUri(
	    data[Constants::ATTRIBUTES][Constants::SINK], jsonData.GetAllocator());

	// Log the UUri string.
	spdlog::debug(
	    "APIWrapper::handleRpcServerCommand(), UUri in string format is :  "
	    "{}",
	    uri.DebugString());

	auto rpcServerCallback = [payload](const uprotocol::v1::UMessage& message)
	    -> std::optional<uprotocol::datamodel::builder::Payload> {
				spdlog::info(
	    "APIWrapper::handleRpcServerCommand(), Sending response to client");
		return payload;
	};

	// Serialize the URI
	std::string serializedUri = uri.SerializeAsString();

	// Attempt to find the range of elements with the matching key
	if (MultiMapUtils::checkKeyValueType<
	        std::unique_ptr<uprotocol::communication::RpcServer>>(
	        uriCallbackMap_, serializedUri)) {
		status.set_code(UCode::ALREADY_EXISTS);
		status.set_message("RPC Server already exists for the given URI.");
		return status;
	}

	// Create RPC Client object
	auto rpcServer = uprotocol::communication::RpcServer::create(
	    transportPtr_, uri, rpcServerCallback, format);

	return rpcServer.has_value()
	           ? addHandleToUriCallbackMap(std::move(rpcServer).value(), uri)
	           : rpcServer.error();
}

UStatus APIWrapper::handlePublisherCommand(Document& jsonData) {
	// Get the data and test ID from the JSON document.
	Value& data = jsonData[Constants::DATA];
	std::string strTest_id = jsonData[Constants::TEST_ID].GetString();

	auto format = ProtoConverter::distToUPayFormat(
	    data[Constants::ATTRIBUTES][Constants::FORMAT]);

	if (!format.has_value()) {
		spdlog::error(
		    "APIWrapper::handlePublisherCommand(), Invalid format received.");
		UStatus status;
		status.set_code(UCode::NOT_FOUND);
		status.set_message("Invalid payload format received in the request.");
		return status;
	}

	// Build payload
	auto valueStr = std::string(data[Constants::PAYLOAD].GetString());
	uprotocol::datamodel::builder::Payload payload(valueStr, format.value());

	// Create a UUri object.
	auto uri = ProtoConverter::distToUri(
	    data[Constants::ATTRIBUTES][Constants::SOURCE],
	    jsonData.GetAllocator());

	// Log the UUri string.
	spdlog::debug(
	    "APIWrapper::handlePublisherCommand(), UUri in string format is :  "
	    "{}",
	    uri.DebugString());

	std::string serializedUri = uri.SerializeAsString();

	// Check and add NotificationSource if not exists
	MultiMapUtils::checkOrAdd(
	    uriCallbackMap_, serializedUri,
	    uprotocol::communication::Publisher(transportPtr_, std::move(uri),
	                                        format.value()));

	auto& publisherHandle = std::get<uprotocol::communication::Publisher>(
	    uriCallbackMap_.find(serializedUri)->second);

	return publisherHandle.publish(std::move(payload));
}

UStatus APIWrapper::handleSubscriberCommand(Document& jsonData) {
	// Create a UUri object.
	auto uri = ProtoConverter::distToUri(jsonData[Constants::DATA],
	                                     jsonData.GetAllocator());

	// Get the test ID from the JSON data.
	std::string strTest_id = jsonData[Constants::TEST_ID].GetString();

	// Define a lambda function for handling received messages.
	// This function logs the reception of a message and forwards it to the TM.
	// TODO: Temprary using Constants::RESPONSE_ON_RECEIVE to integrate with test manager
	auto callBack = [this, strTest_id](const UMessage response) {
		spdlog::info("Subscribe response received.");

		// Send the message to the Test Manager.
		sendToTestManager(response, Constants::RESPONSE_ON_RECEIVE);
	};

	// Create a subscriber object.
	auto subscriber = uprotocol::communication::Subscriber::subscribe(
	    transportPtr_, uri, std::move(callBack));

	// Add the subscriber object to the URI callback map.
	return subscriber.has_value()
	           ? addHandleToUriCallbackMap(std::move(subscriber).value(), uri)
	           : subscriber.error();
}

UStatus APIWrapper::handleNotificationSourceCommand(Document& jsonData) {
	Value& data = jsonData[Constants::DATA];

	auto uriSource = def_src_uuri_;
	auto uriSink = ProtoConverter::distToUri(
	    data[Constants::ATTRIBUTES][Constants::SINK], jsonData.GetAllocator());

	auto format = ProtoConverter::distToUPayFormat(
	    data[Constants::ATTRIBUTES][Constants::FORMAT]);

	// Build payload
	auto payloadValueStr = std::string(data[Constants::PAYLOAD].GetString());
	uprotocol::datamodel::builder::Payload payload(payloadValueStr,
	                                               format.value());

	std::string serializedUri = uriSink.SerializeAsString();

	// Check and add NotificationSource if not exists
	MultiMapUtils::checkOrAdd(uriCallbackMap_, serializedUri,
	                          uprotocol::communication::NotificationSource(
	                              transportPtr_, std::move(uriSource),
	                              std::move(uriSink), format.value()));

	// Retrieve the handle as it is already added or existing
	auto& notificationSourceHandle =
	    std::get<uprotocol::communication::NotificationSource>(
	        uriCallbackMap_.find(serializedUri)->second);

	return notificationSourceHandle.notify(std::move(payload));
}

UStatus APIWrapper::handleNotificationSinkCommand(Document& jsonData) {
	// Get the data and test ID from the JSON document.
	Value& data = jsonData[Constants::DATA];
	std::string strTest_id = jsonData[Constants::TEST_ID].GetString();
	UStatus status;

	// Create a UUri object.
	auto uri = ProtoConverter::distToUri(data, jsonData.GetAllocator());

	// Log the UUri string.
	spdlog::debug(
	    "APIWrapper::handleRpcServerCommand(), UUri in string format is :  "
	    "{}",
	    uri.DebugString());

	// TODO: Temprary using Constants::RESPONSE_ON_RECEIVE to integrate with test manager
	auto callback = [this, strTest_id](const UMessage& transportUMessage) {
		spdlog::info("APIWrapper::handleNotificationSinkCommand(), received.");

		// Send the message to the Test Manager with a response.
		sendToTestManager(transportUMessage,
		                  Constants::RESPONSE_ON_RECEIVE);
	};
	// Serialize the URI
	std::string serializedUri = uri.SerializeAsString();

	// Attempt to find the range of elements with the matching key
	if (MultiMapUtils::checkKeyValueType<
	        std::unique_ptr<uprotocol::communication::NotificationSink>>(
	        uriCallbackMap_, serializedUri)) {
		status.set_code(UCode::ALREADY_EXISTS);
		status.set_message("RPC Server already exists for the given URI.");
		return status;
	}

	// Create NotificationSink object
	auto NotificationSinkHandle =
	    uprotocol::communication::NotificationSink::create(
	        transportPtr_, std::move(uri), std::move(callback), std::nullopt);

	return NotificationSinkHandle.has_value()
	           ? addHandleToUriCallbackMap(
	                 std::move(NotificationSinkHandle).value(), uri)
	           : NotificationSinkHandle.error();
}
