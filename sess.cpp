#include "sess.h"
#include "encrypt.h"

Session::Session(asio::io_service &service, uint32_t convid, OutputHandler o)
    : AsyncInOutputer(o), service_(service), convid_(convid) {
}

Session::~Session() {
    TRACE
    if (kcp_ != nullptr) {
        ikcp_release(kcp_);
    }
}

void Session::run() {
    kcp_ = ikcp_create(convid_, static_cast<void *>(this));
    kcp_->output = Session::output_wrapper;
    kcp_->stream = 1;
    ikcp_nodelay(kcp_, FLAGS_nodelay, FLAGS_interval, FLAGS_resend, FLAGS_nc);
    ikcp_wndsize(kcp_, FLAGS_sndwnd, FLAGS_rcvwnd);
    ikcp_setmtu(kcp_, FLAGS_mtu);
    timer_ = std::make_shared<asio::high_resolution_timer>(service_);
    run_timer(std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(FLAGS_interval));
    // run_peeksize_checker();
}

void Session::run_timer(std::chrono::high_resolution_clock::time_point pt) {
    if(timer_->expires_at() <= pt && timer_->expires_from_now() >= std::chrono::milliseconds(0)) {
        return;
    } else {
        timer_->expires_at(pt);
    }
    std::weak_ptr<Session> ws = shared_from_this();
    timer_->async_wait([this, ws](const std::error_code &) {
        auto s = ws.lock();
        if (!s) {
            return;
        }
        update();
    });
}

void Session::run_peeksize_checker() {
    auto self = shared_from_this();
    auto timer = std::make_shared<asio::high_resolution_timer>(
        service_, std::chrono::seconds(1));
    timer->async_wait([this, self, timer](const std::error_code &) {
        std::cout << ikcp_peeksize(kcp_) << std::endl;
        run_peeksize_checker();
    });
}

void Session::async_input(char *buffer, std::size_t len, Handler handler) {
    input(buffer, len);
    if (handler) {
        handler(errc(0), len);
    }
}

void Session::input(char *buffer, std::size_t len) {
    auto n = ikcp_input(kcp_, buffer, int(len));
    TRACE
    if (rtask_.check()) {
        update();
    } 
    return;
}

void Session::async_read_some(char *buffer, std::size_t len, Handler handler) {
    if (streambufsiz_ > 0) {
        auto n = streambufsiz_;
        if (n > len) {
            n = len;
        }
        memcpy(buffer, stream_buf_, n);
        streambufsiz_ -= n;
        if (streambufsiz_ != 0) {
            memmove(stream_buf_, stream_buf_ + n, streambufsiz_);
        }
        if (handler) {
            handler(std::error_code(0, std::generic_category()),
                    static_cast<std::size_t>(n));
        }
        return;
    }
    auto psz = ikcp_peeksize(kcp_);
    if (psz <= 0) {
        rtask_.buf = buffer;
        rtask_.len = len;
        rtask_.handler = handler;
        return;
    }
    auto n = ikcp_recv(kcp_, buffer, int(len));
    // LOG(INFO) << "ikcp_recv return " << n;
    if (handler) {
        handler(std::error_code(0, std::generic_category()),
                static_cast<std::size_t>(n));
    }
    if (psz > len) {
        n = ikcp_recv(kcp_, stream_buf_, sizeof(stream_buf_));
        // LOG(INFO) << "ikcp_recv2 return " << n;
        streambufsiz_ = n;
    }
    return;
}

void Session::async_write(char *buffer, std::size_t len, Handler handler) {
    auto waitsnd = ikcp_waitsnd(kcp_);
    if (waitsnd <= FLAGS_sndwnd * 2) {
        auto n = ikcp_send(kcp_, buffer, int(len)); 
        if (handler) {
            handler(std::error_code(0, std::generic_category()),
                    static_cast<std::size_t>(n));
        }
        // ikcp_flush(kcp_);
    } else {
        wtasks_.push_back(Task{buffer, len, handler});
    }

    update();
}

int Session::output_wrapper(const char *buffer, int len, struct IKCPCB *kcp,
                            void *user)
{
    assert(user != nullptr);
    Session *sess = static_cast<Session *>(user);
    sess->output((char *)(buffer), static_cast<std::size_t>(len), nullptr);
    sess->updateWrite();
    return 0;
}

void Session::updateRead() {
    ikcp_update(kcp_, iclock());
    if (!rtask_.check()) {
        return;
    }
    auto psz = ikcp_peeksize(kcp_);
    if (psz <= 0) {
        return;
    }
    auto len = rtask_.len;
    auto n = ikcp_recv(kcp_, rtask_.buf, int(len));
    // LOG(INFO) << "ikcp_recv return " << n;
    auto rtask_handler = rtask_.handler;
    rtask_.reset();
    if (rtask_handler) {
        rtask_handler(std::error_code(0, std::generic_category()),
                      static_cast<std::size_t>(n));
    }
    // LOG(INFO) << "psz = " << psz << " pick again is " << ikcp_peeksize(kcp_);
    if (psz > len) {
        n = ikcp_recv(kcp_, stream_buf_, sizeof(stream_buf_));
        // LOG(INFO) << "ikcp_recv2 return " << n;
        streambufsiz_ = n;
    }
}

void Session::updateWrite() {
    while (ikcp_waitsnd(kcp_) < FLAGS_sndwnd * 2 && !wtasks_.empty()) {
        auto task = wtasks_.front();
        wtasks_.pop_front();
        auto n = ikcp_send(kcp_, task.buf, int(task.len));
        auto &handler = task.handler; 
        if (handler) {
            handler(std::error_code(0, std::generic_category()),
                    static_cast<std::size_t>(n));
        }
    }
}

void Session::updateTimer() {
    auto current = iclock();
    auto next = ikcp_check(kcp_, current);
    next -= current;
    // LOG(INFO) << "next = " << next;
    run_timer(std::chrono::high_resolution_clock::now()+std::chrono::milliseconds(next));
}

void Session::update() {
    updateRead();
    updateWrite();
    updateTimer();
}

void Session::call_this_on_destroy() {
    auto self = shared_from_this();

    Destroy::call_this_on_destroy();

    if (timer_) {
        timer_->cancel();
        timer_ = nullptr;
    }

    if (rtask_.check()) {
        auto rtask_handler = rtask_.handler;
        rtask_.reset();
        rtask_handler(std::error_code(1, std::generic_category()), 0);
    }

    if (wtask_.check()) {
        auto wtask_handler = wtask_.handler;
        wtask_.reset();
        wtask_handler(std::error_code(1, std::generic_category()), 0);
    }
}
