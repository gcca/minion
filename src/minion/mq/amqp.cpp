#include "amqp.hpp"

#include <cerrno>
#include <cstring>
#include <format>

#include <fcntl.h>
#include <rabbitmq-c/tcp_socket.h>
#include <sys/time.h>

namespace minion::mq {

namespace {

constexpr amqp_channel_t Channel = 1;
constexpr int NoHeartbeat = 0;

[[nodiscard]] amqp_bytes_t ToBytes(std::string_view text) {
  return {.len = text.size(), .bytes = const_cast<char *>(text.data())};
}

[[nodiscard]] std::string_view ToView(amqp_bytes_t bytes) {
  return {static_cast<const char *>(bytes.bytes), bytes.len};
}

[[nodiscard]] std::string ReplyError(const amqp_rpc_reply_t &reply) {
  switch (reply.reply_type) {
  case AMQP_RESPONSE_NORMAL:
    return "success";
  case AMQP_RESPONSE_NONE:
    return "missing reply, the connection is gone";
  case AMQP_RESPONSE_LIBRARY_EXCEPTION:
    return amqp_error_string2(reply.library_error);
  case AMQP_RESPONSE_SERVER_EXCEPTION:
    switch (reply.reply.id) {
    case AMQP_CONNECTION_CLOSE_METHOD: {
      const auto *method =
          static_cast<amqp_connection_close_t *>(reply.reply.decoded);
      return std::format("connection closed, {} {}", method->reply_code,
                         ToView(method->reply_text));
    }
    case AMQP_CHANNEL_CLOSE_METHOD: {
      const auto *method =
          static_cast<amqp_channel_close_t *>(reply.reply.decoded);
      return std::format("channel closed, {} {}", method->reply_code,
                         ToView(method->reply_text));
    }
    default:
      return std::format("server exception on method {:#x}", reply.reply.id);
    }
  }
  return "unknown error";
}

[[nodiscard]] bool SetCloseOnExec(amqp_connection_state_t connection,
                                  std::string &error) {
  const int descriptor = amqp_get_sockfd(connection);
  if (descriptor < 0) {
    error = "no socket for the rabbitmq connection";
    return false;
  }

  const int flags = ::fcntl(descriptor, F_GETFD);
  if (flags < 0 || ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) != 0) {
    error = std::format("marking the rabbitmq socket close-on-exec: {}",
                        std::strerror(errno));
    return false;
  }

  return true;
}

[[nodiscard]] amqp_connection_state_t Open(const conf::Node &node,
                                           const Credentials &credentials,
                                           std::string &error) {
  amqp_connection_state_t connection = amqp_new_connection();
  if (!connection) {
    error = "cannot allocate a rabbitmq connection";
    return nullptr;
  }

  const auto fail = [&](std::string message) {
    error = std::format("{}:{}: {}", node.host, node.port, message);
    amqp_destroy_connection(connection);
    return nullptr;
  };

  amqp_socket_t *socket = amqp_tcp_socket_new(connection);
  if (!socket)
    return fail("cannot create a socket");

  if (const int status = amqp_socket_open(socket, node.host.c_str(), node.port);
      status != AMQP_STATUS_OK)
    return fail(amqp_error_string2(status));

  if (std::string reason; !SetCloseOnExec(connection, reason))
    return fail(reason);

  if (const amqp_rpc_reply_t reply = amqp_login(
          connection, credentials.vhost.c_str(), AMQP_DEFAULT_MAX_CHANNELS,
          AMQP_DEFAULT_FRAME_SIZE, NoHeartbeat, AMQP_SASL_METHOD_PLAIN,
          credentials.user.c_str(), credentials.password.c_str());
      reply.reply_type != AMQP_RESPONSE_NORMAL)
    return fail(
        std::format("login as '{}': {}", credentials.user, ReplyError(reply)));

  amqp_channel_open(connection, Channel);
  if (const amqp_rpc_reply_t reply = amqp_get_rpc_reply(connection);
      reply.reply_type != AMQP_RESPONSE_NORMAL)
    return fail(std::format("opening a channel: {}", ReplyError(reply)));

  return connection;
}

}

AmqpBroker::AmqpBroker(amqp_connection_state_t connection, conf::Node node)
    : connection_(connection), node_(std::move(node)) {}

AmqpBroker::~AmqpBroker() {
  if (open_) {
    amqp_channel_close(connection_, Channel, AMQP_REPLY_SUCCESS);
    amqp_connection_close(connection_, AMQP_REPLY_SUCCESS);
  }
  amqp_destroy_connection(connection_);
}

[[nodiscard]] std::unique_ptr<AmqpBroker>
AmqpBroker::Connect(const std::vector<conf::Node> &nodes,
                    const Credentials &credentials, std::string &error) {
  std::string failures;

  for (const auto &node : nodes) {
    std::string reason;
    if (auto *connection = Open(node, credentials, reason))
      return std::unique_ptr<AmqpBroker>(new AmqpBroker(connection, node));

    if (!failures.empty())
      failures += "; ";
    failures += reason;
  }

  error = failures.empty() ? "no MQ nodes configured" : failures;
  return nullptr;
}

[[nodiscard]] bool AmqpBroker::Expect(std::string_view action,
                                      std::string &error) {
  const amqp_rpc_reply_t reply = amqp_get_rpc_reply(connection_);
  if (reply.reply_type == AMQP_RESPONSE_NORMAL)
    return true;

  error = std::format("{}: {}", action, ReplyError(reply));
  open_ = false;
  return false;
}

[[nodiscard]] bool AmqpBroker::DeclareQueue(const std::string &queue,
                                            std::string &error) {
  amqp_queue_declare(connection_, Channel, ToBytes(queue), 0, 1, 0, 0,
                     amqp_empty_table);
  return Expect(std::format("declaring queue '{}'", queue), error);
}

[[nodiscard]] bool AmqpBroker::Consume(const std::string &queue,
                                       std::string &error) {
  amqp_basic_qos(connection_, Channel, 0, 1, 0);
  if (!Expect("setting prefetch", error))
    return false;

  amqp_basic_consume(connection_, Channel, ToBytes(queue), amqp_empty_bytes, 0,
                     0, 0, amqp_empty_table);
  return Expect(std::format("consuming queue '{}'", queue), error);
}

[[nodiscard]] bool AmqpBroker::Publish(const std::string &queue,
                                       std::string_view body,
                                       std::string &error) {
  amqp_basic_properties_t properties{};
  properties._flags =
      AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
  properties.content_type = amqp_cstring_bytes("application/octet-stream");
  properties.delivery_mode = AMQP_DELIVERY_PERSISTENT;

  if (const int status =
          amqp_basic_publish(connection_, Channel, amqp_empty_bytes,
                             ToBytes(queue), 0, 0, &properties, ToBytes(body));
      status != AMQP_STATUS_OK) {
    error = std::format("publishing to '{}': {}", queue,
                        amqp_error_string2(status));
    open_ = false;
    return false;
  }

  return true;
}

[[nodiscard]] Receive AmqpBroker::Next(Delivery &delivery,
                                       std::chrono::milliseconds wait,
                                       std::string &error) {
  amqp_maybe_release_buffers(connection_);

  const auto micros =
      std::chrono::duration_cast<std::chrono::microseconds>(wait).count();
  timeval timeout{.tv_sec = static_cast<time_t>(micros / 1000000),
                  .tv_usec = static_cast<suseconds_t>(micros % 1000000)};

  amqp_envelope_t envelope;
  const amqp_rpc_reply_t reply =
      amqp_consume_message(connection_, &envelope, &timeout, 0);

  if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
      reply.library_error == AMQP_STATUS_TIMEOUT)
    return Receive::Timeout;

  if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
      reply.library_error == AMQP_STATUS_UNEXPECTED_STATE) {
    amqp_frame_t frame;
    if (const int status = amqp_simple_wait_frame(connection_, &frame);
        status != AMQP_STATUS_OK) {
      error = std::format("reading a frame: {}", amqp_error_string2(status));
      open_ = false;
      return Receive::Lost;
    }

    if (frame.frame_type == AMQP_FRAME_METHOD &&
        (frame.payload.method.id == AMQP_CHANNEL_CLOSE_METHOD ||
         frame.payload.method.id == AMQP_CONNECTION_CLOSE_METHOD)) {
      error = "closed by the server";
      open_ = false;
      return Receive::Lost;
    }

    return Receive::Timeout;
  }

  if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
    error = std::format("consuming: {}", ReplyError(reply));
    open_ = false;
    return Receive::Lost;
  }

  delivery.tag = envelope.delivery_tag;
  delivery.body.assign(ToView(envelope.message.body));
  delivery.redelivered = envelope.redelivered != 0;
  amqp_destroy_envelope(&envelope);

  return Receive::Message;
}

[[nodiscard]] bool AmqpBroker::Ack(std::uint64_t tag, std::string &error) {
  if (const int status = amqp_basic_ack(connection_, Channel, tag, 0);
      status != AMQP_STATUS_OK) {
    error =
        std::format("acking delivery {}: {}", tag, amqp_error_string2(status));
    open_ = false;
    return false;
  }

  return true;
}

}
