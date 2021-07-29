#include "kcptun_client.h"

kcptun_client::kcptun_client(asio::io_service &io_service,
                             asio::ip::tcp::endpoint local_endpoint,
                             asio::ip::udp::endpoint target_endpoint)
    : service_(io_service), socket_(io_service),
      target_endpoint_(target_endpoint), acceptor_(io_service, local_endpoint){}

void kcptun_client::run() {
    locals_.reserve(FLAGS_conn);
    for (int i = 0; i < FLAGS_conn; i++) {
        auto l = std::make_shared<Local>(service_, target_endpoint_);
        l->run();
        locals_.emplace_back(l);
    }
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    do_accept();
}

void kcptun_client::async_choose_local(
    std::function<void(std::shared_ptr<Local>)> f) {
    auto i = rand() % FLAGS_conn;
    auto local = locals_[i].lock();
    if ((!local) || local->is_destroyed()) {
        local = std::make_shared<Local>(service_, target_endpoint_);
        local->run();
        locals_[i] = local;
        f(local);
        return;
    }
    f(local);
    return;
}

void kcptun_client::do_accept() {
    auto self = shared_from_this();
    acceptor_.async_accept(socket_, [this, self](std::error_code ec) {
        TRACE
        if (ec) {
            TRACE
            return;
        }
        auto sock = std::make_shared<asio::ip::tcp::socket>(std::move(socket_));
        async_choose_local([this, self, sock](std::shared_ptr<Local> local) {
            if (!local) {
                return;
            }
            local->async_connect([this, self,
                                  sock](std::shared_ptr<smux_sess> sess) {
                if (!sess) {
                    return;
                }
                std::make_shared<kcptun_client_session>(service_, sock, sess)
                    ->run();
            });
        });
        do_accept();
    });
}

kcptun_client_session::kcptun_client_session(
    asio::io_service &io_service, std::shared_ptr<asio::ip::tcp::socket> sock,
    std::shared_ptr<smux_sess> sess)
    : service_(io_service), sock_(sock), sess_(sess) {
}

kcptun_client_session::~kcptun_client_session() {
    // LOG(INFO) << "stream closed!";
}

void kcptun_client_session::run() {
    // LOG(INFO) << "stream opened!";
    do_pipe1();
    do_pipe2();
}

void kcptun_client_session::do_pipe1() {
    auto self = shared_from_this();
    sock_->async_read_some(
        asio::buffer(buf1_, sizeof(buf1_)),
        [this, self](std::error_code ec, std::size_t len) {
            if (ec) {
                destroy();
                return;
            }
            sess_->async_write(buf1_, len,
                               [this, self](std::error_code ec, std::size_t) {
                                   if (ec) {
                                       destroy();
                                       return;
                                   }
                                   do_pipe1();
                               });
        });
}

void kcptun_client_session::do_pipe2() {
    auto self = shared_from_this();
    sess_->async_read_some(buf2_, sizeof(buf2_), [this,
                                                  self](std::error_code ec,
                                                        std::size_t len) {
        if (ec) {
            destroy();
            return;
        }
        asio::async_write(*sock_, asio::buffer(buf2_, len),
                          [this, self](std::error_code ec, std::size_t len) {
                              if (ec) {
                                  destroy();
                                  return;
                              }
                              do_pipe2();
                          });
    });
}

void kcptun_client_session::call_this_on_destroy() {
    auto self = shared_from_this();

    Destroy::call_this_on_destroy();

    if(sock_) {
        sock_->close();
    }
    if(sess_) {
        sess_->destroy();
    }
}
