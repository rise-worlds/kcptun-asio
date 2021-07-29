#include "server.h"
#include "sess.h"
#include "smux.h"

static kvar server_kvar("Server");

Server::Server(asio::io_service &io_service, OutputHandler handler)
    : AsyncInOutputer(handler), service_(io_service), kvar_(server_kvar) {
}

void Server::run(AcceptHandler accept_handler, uint32_t convid) {
    auto self = shared_from_this();

    in = [this](char *buf, std::size_t len, Handler handler) {
        sess_->async_input(buf, len, handler);
    };

    out = [this](char *buf, std::size_t len, Handler handler) {
        output(buf, len, handler);
    };
    sess_ = std::make_shared<Session>(service_, convid, out);
    sess_->run();

    out2 = [this](char *buf, std::size_t len, Handler handler) {
        sess_->async_write(buf, len, handler);
    };
    smux_ = std::make_shared<smux>(service_, out2);
    smux_->set_accept_handler(accept_handler);
    smux_->call_on_destroy([this, self]{
        destroy();
    });
    smux_->run();

    in2 = [this](char *buf, std::size_t len, Handler handler) {
        smux_->async_input(buf, len, handler);
    };

    do_sess_receive();
}

void Server::async_input(char *buf, std::size_t len, Handler handler) {
    in(buf, len, handler);
}

void Server::do_sess_receive() {
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

Server::~Server() {
}

void Server::call_this_on_destroy() {
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

    in = nullptr;
    out = nullptr;
    in2 = nullptr;
    out2 = nullptr;
}
