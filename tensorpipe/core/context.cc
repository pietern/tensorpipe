/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/core/context.h>

#include <tensorpipe/channel/basic/basic.h>
#include <tensorpipe/common/defs.h>
#include <tensorpipe/transport/connection.h>
#include <tensorpipe/transport/shm/context.h>
#include <tensorpipe/transport/uv/context.h>

namespace tensorpipe {

std::shared_ptr<Context> Context::create(
    const std::vector<std::string>& transports,
    const std::vector<std::string>& channelFactories) {
  auto context = std::make_shared<Context>(
      ConstructorToken(), transports, channelFactories);
  context->start_();
  return context;
}

Context::Context(
    ConstructorToken /* unused */,
    const std::vector<std::string>& transports,
    const std::vector<std::string>& channelFactories)
    : callbackQueue_(1000) {
  for (const auto& transport : transports) {
    if (transport == "shm") {
      contexts_.emplace(transport, std::make_shared<transport::shm::Context>());
    } else if (transport == "uv") {
      contexts_.emplace(transport, std::make_shared<transport::uv::Context>());
    } else {
      TP_THROW_EINVAL() << "unsupported transport " << transport;
    }
  }
  for (const auto& name : channelFactories) {
    if (name == "basic") {
      channelFactories_.emplace(
          name, std::make_shared<channel::basic::BasicChannelFactory>());
    } else {
      TP_THROW_EINVAL() << "unsupported channel " << name;
    }
  }
}

void Context::start_() {
  callbackCaller_ = std::thread([this]() { runCallbackCaller_(); });
}

std::shared_ptr<transport::Context> Context::getContextForTransport_(
    std::string transport) {
  auto iter = contexts_.find(transport);
  if (iter == contexts_.end()) {
    TP_THROW_EINVAL() << "unsupported transport " << transport;
  }
  return iter->second;
}

std::shared_ptr<channel::ChannelFactory> Context::getChannelFactory_(
    std::string name) {
  auto iter = channelFactories_.find(name);
  if (iter == channelFactories_.end()) {
    TP_THROW_EINVAL() << "unsupported channel factory " << name;
  }
  return iter->second;
}

void Context::join() {
  done_ = true;
  for (auto& context : contexts_) {
    context.second->join();
  }
  callbackQueue_.push(nullopt);
  callbackCaller_.join();
}

Context::~Context() {
  if (!done_) {
    TP_LOG_WARNING()
        << "The context is being destroyed but join() wasn't called on it. "
        << "Perhaps a scope exited prematurely, possibly due to an exception?";
    join();
  }
  TP_DCHECK(done_);
  TP_DCHECK(!callbackCaller_.joinable());
}

void Context::runCallbackCaller_() {
  while (true) {
    auto fn = callbackQueue_.pop();
    if (!fn.has_value()) {
      break;
    }
    fn.value()();
  }
}

void Context::callCallback_(std::function<void()> fn) {
  callbackQueue_.push(std::move(fn));
}

} // namespace tensorpipe
