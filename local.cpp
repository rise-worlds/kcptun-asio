#include "local.h"
#include "sess.h"
#include "smux.h"

Local::Local(asio::io_service &io_service, asio::ip::udp::endpoint ep)
    : service_(io_service), ep_(ep) {
    auto usocket = asio::ip::udp::socket(io_service);
    usocket.connect(ep_);
    usock_ = std::make_shared<UsocketReadWriter>(std::move(usocket));
}

void Local::run() {
    auto self = shared_from_this();

    in = [this](char *buf, std::size_t len, Handler handler) {
        // auto n = nonce_size + crc_size;
        // buf += n;
        // len -= n;
        sess_->async_input(buf, len, handler);
    };

    out = [this](char *buf, std::size_t len, Handler handler) {
        char *buffer = buffers_.get();
        // auto n = nonce_size + crc_size;
        // memcpy(buffer + n, buf, len);
        // // info("capacity: %lu size: %lu\n", buffers_.capacity(), buffers_.size());
        // auto crc = crc32c_ieee(0, (byte *)buf, len);
        // encode32u((byte *)(buffer + nonce_size), crc);
        // usock_->async_input(buffer, len + n, [handler, buffer, len, this](
        memcpy(buffer, buf, len);
        usock_->async_write(buffer, len, [handler, buffer, len, this](
                                              std::error_code ec, std::size_t) {
            buffers_.push_back(buffer);
            // info("capacity: %lu size: %lu\n", buffers_.capacity(), buffers_.size());
            if (handler) {
                handler(ec, len);
            }
        });
    };
    sess_ = std::make_shared<Session>(service_, uint32_t(rand()), out);
    sess_->run();

    out2 = [this](char *buf, std::size_t len, Handler handler) {
        sess_->async_write(buf, len, handler);
    };
    smux_ = std::make_shared<smux>(service_, out2);
    smux_->call_on_destroy([self, this]{
        destroy();
    });
    smux_->run();

    in2 = [this](char *buf, std::size_t len, Handler handler) {
        smux_->async_input(buf, len, handler);
    };

    do_usocket_receive();
    do_sess_receive();
}

void Local::do_usocket_receive() {
    auto self = shared_from_this();
    usock_->async_read_some(
        buf_, sizeof(buf_), [this, self](std::error_code ec, std::size_t sz) {
            if (ec) {
                return;
            }
            in(buf_, sz, [this, self](std::error_code ec, std::size_t) {
                if (ec) {
                    return;
                }
                do_usocket_receive();
            });
        });
}

void Local::do_sess_receive() {
    auto self = shared_from_this();
    sess_->async_read_some(
        sbuf_, sizeof(sbuf_), [this, self](std::error_code ec, std::size_t sz) {
            if (ec) {
                return;
            }
            in2(sbuf_, sz, [this, self](std::error_code ec, std::size_t) {
                if (ec) {
                    return;
                }
                do_sess_receive();
            });
        });
}

void Local::async_connect(
    std::function<void(std::shared_ptr<smux_sess>)> handler) {
    smux_->async_connect(handler);
}

//bool Local::is_destroyed() const { return smux_->is_destroyed(); }

void Local::run_scavenger() {
    if (FLAGS_scavengettl <= 0) {
        return;
    }
    auto self = shared_from_this();
    auto timer = std::make_shared<asio::high_resolution_timer>(
        service_, std::chrono::seconds(FLAGS_scavengettl));
    timer->async_wait([this, timer, self](const std::error_code &) {
        if (smux_) {
            smux_->destroy();
        }
    });
}

void Local::call_this_on_destroy() {
    auto self = shared_from_this();

    Destroy::call_this_on_destroy();

    if (sess_) {
        auto sess = sess_;
        sess_ = nullptr;
        sess->destroy();
    }

    if (smux_) {
        auto smux = smux_;
        smux_ = nullptr;
        smux->destroy();
    }

    usock_ = nullptr;

    in = nullptr;
    out = nullptr;
    in2 = nullptr;
    out2 = nullptr;
}
