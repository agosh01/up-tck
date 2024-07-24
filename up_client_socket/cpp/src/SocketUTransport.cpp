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

#include "SocketUTransport.h"

#include <arpa/inet.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <up-cpp/datamodel/serializer/UUri.h>

#include <array>
#include <set>
#include <thread>
#include <cctype>
#include <type_traits>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cxxabi.h>
#include "SafeTupleMap.h"
#include "WakeFd.h"

using namespace uprotocol::v1;
using uprotocol::transport::UTransport;
// using uprotocol::datamodel::serializer::uri::AsString;
using namespace std;

// const string dispatcher_ip = "127.0.0.1";
// const int diaptcher_port = 44444;

template <typename T>
std::string demangle(const T& t)
{
	auto info = typeid(t).name();
	int status;
	auto realname = abi::__cxa_demangle(info, NULL, NULL, &status);
	return std::string(realname);
}


string repr(const string& input)
{
	stringstream ss;
	ss << "'" << setfill('0') << hex;
	for (auto c : input) {
		if (isgraph(c)) ss << c;
		else if (c == '\n') {
			ss << "\\n";
		}
		else {
			ss << "\\x" << setw(2) << (int(c) & 0xff);
		}
	}
	ss << "'";
	return ss.str();
}

template <auto Start, auto End, auto Inc, class F>
constexpr void constexpr_for(F&& f)
{
    if constexpr (Start < End)
    {
        f(std::integral_constant<decltype(Start), Start>());
        constexpr_for<Start + Inc, End, Inc>(f);
    }
}

struct SocketUTransport::Impl {
	struct CallbackData {
		mutex mtx;  // this is to protect set insertion and deletion
		set<CallableConn> listeners;
		optional<UUri> source_filter;
	};

	unique_ptr<WakeFd> wake_fd_;
	thread process_thread_;
	string buffer_;

	using UUriKey = tuple<optional<string>, optional<uint32_t>,
	                      optional<uint32_t>, optional<uint32_t> >;
	// using UUriKey = tuple<optional<string>, optional<uint32_t>,
	//                       optional<uint32_t>, uint32_t >;

	// using CallbackKey = tuple<UUriKey, optional<UUriKey> >;

	SafeTupleMap<UUriKey, CallbackData> callback_data_;

	//
	// This function is going to map the protobuf fields for a uuri into a tuple suitable for compile time expansion
	//
	static UUriKey makeUUriKey(const UUri& uuri) {
		UUriKey key;
		if (uuri.authority_name() != "*") {
			cout << "authority_name wildcard" << endl;
			get<0>(key) = uuri.authority_name();
		}
		if (uuri.ue_id() != 0xffff) {
			cout << "ue_id wildcard" << endl;
			get<1>(key) = uuri.ue_id();
		}
		if (uuri.ue_version_major() != 0xffff) {
			cout << "version_major wildcard" << endl;
			get<2>(key) = uuri.ue_version_major();
		}
		if (uuri.resource_id() != 0xffff) {
			cout << "resource_id wildcard" << endl;
			get<3>(key) = uuri.resource_id();
		}
		return key;
	}

	template <typename T>
	static void assign_if(T& field, size_t& cnt, size_t i, size_t bits)
	{
	}

	template <typename T>
	static void assign_if(optional<T>& field, size_t& cnt, size_t i, size_t bits)
	{
		if (((1<<i) & bits) && (field != nullopt)) {
			field = nullopt;
			cnt++;
		}
	}

	//
	// This function is going to take a tuple key, and return a vector of alternatives with wildcard substitutions.
	//
	static vector<UUriKey> makeWildcardKeys(const UUriKey& key)
	{
		const auto len = std::tuple_size_v<UUriKey>;
		vector<UUriKey> ret;
		ret.push_back(key);
		constexpr_for<1, (1<<len), 1>([&](const auto bits) {
			auto out = key;
			size_t cnt = 0;
			constexpr_for<0, len, 1>([&](const auto i) { assign_if(get<i>(out), cnt, i, bits); });
			if (cnt > 0) {
				ret.push_back(out);
			}
		});
		return ret;
	}

	// static CallbackKey makeKey(const UUri& req,
	//                            const optional<UUri>& opt = nullopt) {
	// 	CallbackKey key;
	// 	get<0>(key) = makeUUriKey(req);
	// 	if (opt)
	// 		get<1>(key) = makeUUriKey(*opt);
	// 	return key;
	// }

	Impl(const std::string& dispatcher_ip, int dispatcher_port) {
		struct sockaddr_in serv_addr;

		if (inet_pton(AF_INET, dispatcher_ip.c_str(), &serv_addr.sin_addr) <=
		    0) {
			spdlog::error(
			    "SocketUTransport::SocketUTransport():{}, Invalid address/ "
			    "Address not supported",
			    __LINE__);
			exit(EXIT_FAILURE);
		}

		int fd;
		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			spdlog::error(
			    "SocketUTransport::SocketUTransport():{}, Socket creation "
			    "error",
			    __LINE__);
			exit(EXIT_FAILURE);
		}

		wake_fd_ = make_unique<WakeFd>(fd);

		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(dispatcher_port);

		if (wake_fd_->connect((struct sockaddr*)&serv_addr, sizeof(serv_addr)) <
		    0) {
			spdlog::error(
			    "SocketUTransport::SocketUTransport():{}, Socket connection "
			    "Failed",
			    __LINE__);
			exit(EXIT_FAILURE);
		}

		process_thread_ = thread([&]() { dispatcher(); });
	}

	~Impl() {
		wake_fd_->wake();
		process_thread_.join();
	}

	UStatus sendImpl(const UMessage& umsg) {
		cout << "inside sendImpl " << umsg.ShortDebugString() << endl;
		// spdlog::debug(
		//     "SocketUTransport::send():{}, UMessage in string format is : {}",
		//     __LINE__, umsg.DebugString());

		// size_t serializedSize = umsg.ByteSizeLong();
		// string umsgSerialized(serializedSize, '\0');
		// bool ret = umsg.SerializeToArray(umsgSerialized.data(), serializedSize);
		string buf;
		bool ret = umsg.SerializeToString(&buf);
		// spdlog::debug("SocketUTransport::send():{}, Serialized UMessage is {}",
		//               __LINE__, buf);

		UStatus status;
		status.set_code(UCode::OK);
		status.set_message("OK");

		cout << "sending " << repr(buf) << endl;
		if (wake_fd_->send(buf.c_str(), buf.size(), 0) < 0) {
			spdlog::error("SocketUTransport::send():{}, Error sending UMessage",
			              __LINE__);
			status.set_code(UCode::INTERNAL);
			status.set_message("Sending data in socket failed.");
			return status;
		}

		return status;
	}

	void dispatcher() {
		while (true) {
			try {
				if (wake_fd_->read(buffer_) == false)
					break;

				cout << "dispatcher received " << repr(buffer_) << endl;
				UMessage umsg;
				try {
					if (!umsg.ParseFromString(buffer_)) {
						spdlog::error(
						    "SocketUTransport::listen():{}, Error parsing "
						    "UMessage",
						    __LINE__);
						continue;
					}
				} catch (const google::protobuf::FatalException& e) {
					spdlog::error(
					    "SocketUTransport::listen():{}, Protobuf exception: {}",
					    __LINE__, e.what());
					continue;
				}

				spdlog::debug(
				    "SocketUTransport::listen():{}, Received uMessage:{}",
				    __LINE__, umsg.DebugString());

				auto& attributes = umsg.attributes();
				auto key = makeUUriKey(attributes.source());
				auto patterns = makeWildcardKeys(key);
				for (const auto& pattern : patterns) {
					cout << "pattern = " << pattern << endl;
					auto ptr = callback_data_.find(pattern);
					if (ptr != nullptr) {
						cout << "match" << endl;
						unique_lock<mutex> lock(ptr->mtx);
						for (auto callback : ptr->listeners) {
							callback(umsg);
						}
					}
				}

			} catch (const system_error& e) {
				if (e.code() == errc::io_error) {
					spdlog::error(
					    "SocketUTransport::listen():{}, I/O error: {}",
					    __LINE__, e.what());
				} else {
					throw;  // rethrow the exception if it's not an I/O error
				}
			}
		}
	}

	UStatus registerListenerImpl(const UUri& sink_filter,
	                             CallableConn& listener,
	                             optional<UUri>& source_filter) {
		UStatus retval;
		retval.set_code(UCode::OK);
		// auto key = makeKey(sink_filter, source_filter);
		auto key = makeUUriKey(sink_filter);
		cout << "inserting " << key << endl;
		auto ptr = callback_data_.find(key, true);
		unique_lock<mutex> lock(ptr->mtx);
		ptr->listeners.insert(listener);
		return retval;
	}

	void cleanupListener(CallableConn listener) {
		callback_data_.erase([&](shared_ptr<CallbackData> ptr) {
			unique_lock<mutex> lock(ptr->mtx);
			ptr->listeners.erase(listener);
		});
	}
};

SocketUTransport::SocketUTransport(const UUri& uuri,
                                   const std::string& dispatcher_ip,
                                   int dispatcher_port)
    : UTransport(uuri), pImpl(new Impl(dispatcher_ip, dispatcher_port)) {}

UStatus SocketUTransport::sendImpl(const UMessage& umsg) {
	return pImpl->sendImpl(umsg);
}

UStatus SocketUTransport::registerListenerImpl(const UUri& sink_filter,
                                               CallableConn&& listener,
                                               optional<UUri>&& source_filter) {
	return pImpl->registerListenerImpl(sink_filter, listener, source_filter);
}

void SocketUTransport::cleanupListener(CallableConn listener) {
	pImpl->cleanupListener(listener);
}
