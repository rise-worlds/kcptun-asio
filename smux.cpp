#include "smux.h"
#include "config.h"

void smux::run() {
    do_receive_frame();
    do_keepalive_checker();
    do_keepalive_sender();
}

void smux::do_keepalive_checker() {
    std::weak_ptr<smux> weaksmux = shared_from_this();
    if (!keepalive_check_timer_) {
        keepalive_check_timer_ = std::make_shared<asio::high_resolution_timer>(
            service_, std::chrono::seconds(FLAGS_keepalive * 3));
    } else {
        keepalive_check_timer_->expires_at(
            keepalive_check_timer_->expires_at() +
            std::chrono::seconds(FLAGS_keepalive * 3));
    }
    keepalive_check_timer_->async_wait(
        [this, weaksmux](const std::error_code &) {
            auto s = weaksmux.lock();
            if (!s || is_destroyed()) {
                return;
            }
            if (data_ready_) {
                data_ready_ = false;
                do_keepalive_checker();
            } else {
                s->destroy();
            }
        });
}

void smux::do_keepalive_sender() {
    std::weak_ptr<smux> weaksmux = shared_from_this();
    if (!keepalive_sender_timer_) {
        keepalive_sender_timer_ = std::make_shared<asio::high_resolution_timer>(
            service_, std::chrono::seconds(FLAGS_keepalive));
    } else {
        keepalive_sender_timer_->expires_at(
            keepalive_sender_timer_->expires_at() +
            std::chrono::seconds(FLAGS_keepalive));
    }
    keepalive_sender_timer_->async_wait(
        [this, weaksmux](const std::error_code &) {
            auto s = weaksmux.lock();
            if (!s || is_destroyed()) {
                return;
            }
            async_write_frame(frame{VERSION, cmdNop, 0, 0}, nullptr);
            do_keepalive_sender();
        });
}

void smux::do_stat_checker() {
    auto self = shared_from_this();
    auto stat_timer = std::make_shared<asio::high_resolution_timer>(
        service_, std::chrono::seconds(1));
    stat_timer->async_wait([this, self, stat_timer](const std::error_code &) {
        if (is_destroyed()) {
            return;
        }
        do_stat_checker();
    });
}

void smux::async_input(char *buf, std::size_t len, Handler handler) {
    data_ready_ = true;
    if (is_destroyed()) {
        if (handler) {
            handler(std::error_code(1, std::generic_category()), 0);
        }
        return;
    }
    input_task_.reset();
    if (len < read_task_.len) {
        memcpy(read_task_.buf, buf, len);
        read_task_.buf += len;
        read_task_.len -= len;
        if (handler) {
            handler(std::error_code(0, std::generic_category()), 0);
        }
    } else {
        TRACE
        memcpy(read_task_.buf, buf, read_task_.len);
        len -= read_task_.len;
        buf += read_task_.len;
        auto read_handler = read_task_.handler;
        read_task_.reset();
        if (len == 0) {
            TRACE
            if (handler) {
                handler(std::error_code(0, std::generic_category()), 0);
            }
        } else {
            TRACE
            input_task_.buf = buf;
            input_task_.len = len;
            input_task_.handler = handler;
        }
        if (read_handler) {
            read_handler(std::error_code(0, std::generic_category()), 0);
        }
    }
    return;
}

void smux::async_read_full(char *buf, std::size_t len, Handler handler) {
    read_task_.reset();
    if (is_destroyed()) {
        if (handler) {
            handler(std::error_code(1, std::generic_category()), 0);
        }
        return;
    }
    TRACE
    if (len < input_task_.len) {
        memcpy(buf, input_task_.buf, len);
        input_task_.len -= len;
        input_task_.buf += len;
        TRACE
        if (handler) {
            TRACE
            handler(std::error_code(0, std::generic_category()), 0);
        }
    } else if (len == input_task_.len) {
        memcpy(buf, input_task_.buf, len);
        auto input_handler = input_task_.handler;
        input_task_.reset();
        TRACE
        if (input_handler) {
            TRACE
            input_handler(std::error_code(0, std::generic_category()), 0);
        }
        if (handler) {
            handler(std::error_code(0, std::generic_category()), 0);
        }
    } else {
        TRACE
        memcpy(buf, input_task_.buf, input_task_.len);
        read_task_.len = len - input_task_.len;
        read_task_.buf = buf + input_task_.len;
        read_task_.handler = handler;
        auto input_handler = input_task_.handler;
        input_task_.reset();
        if (input_handler) {
            TRACE
            input_handler(std::error_code(0, std::generic_category()), 0);
        }
    }
    return;
}

void smux::do_receive_frame() {
    auto self = shared_from_this();
    frame_flag = true;
    async_read_full(frame_header_, headerSize, [this, self](std::error_code ec,
                                                            std::size_t) {
        if (ec) {
            TRACE
            return;
        }
        frame f = frame::unmarshal(frame_header_);
        async_read_full(
            frame_data_, static_cast<std::size_t>(f.length),
            [f, this, self](std::error_code ec, std::size_t) mutable {
                if (ec) {
                    return;
                }
                f.data = frame_data_;
                frame_flag = false;
                handle_frame(f);
            });
    });
}

void smux::handle_frame(frame f) {
    auto it = sessions_.find(f.id);
    auto id = f.id;
    switch (f.cmd) {
    case cmdSyn:
        if (it == sessions_.end() && acceptHandler_) {
            auto s = std::make_shared<smux_sess>(
                service_, id, VERSION, std::weak_ptr<smux>(shared_from_this()));
            acceptHandler_(s);
            sessions_.emplace(std::make_pair(id, std::weak_ptr<smux_sess>(s)));
        }
        do_receive_frame();
        break;
    case cmdFin:
        if (it != sessions_.end()) {
            auto s = it->second.lock();
            if (s) {
                sessions_.erase(f.id);
                s->destroy();
            }
        }
        do_receive_frame();
        break;
    case cmdPsh:
        if (it != sessions_.end()) {
            auto s = it->second.lock();
            if (s) {
                auto self = shared_from_this();
                s->input(f.data, static_cast<std::size_t>(f.length),
                         [this, self](std::error_code ec, std::size_t) {
                             TRACE
                             do_receive_frame();
                         });
            } else {
                do_receive_frame();
            }
        }
        break;
    case cmdNop:
        do_receive_frame();
        break;
    default:
        TRACE
        destroy();
        break;
    }
    return;
}

void smux::async_write(char *buf, std::size_t len, Handler handler) {
    if (is_destroyed()) {
        if (handler) {
            handler(std::error_code(1, std::generic_category()), 0);
        }
        return;
    }
    TRACE
    try_output(buf, len, handler);
}

smux_sess::smux_sess(asio::io_service &io_service, uint32_t id, uint8_t version,
                     std::weak_ptr<smux> sm)
    : service_(io_service), id_(id), version_(version), sm_(sm){
    }

void smux_sess::input(char *buf, std::size_t len, Handler handler) {
    if (destroy_) {
        if (handler) {
            handler(std::error_code(1, std::generic_category()), 0);
        }
        return;
    }
    if (read_task_.check()) {
        if (len <= read_task_.len) {
            memcpy(read_task_.buf, buf, len);
            auto read_handler = read_task_.handler;
            read_task_.reset();
            if (read_handler) {
                read_handler(std::error_code(0, std::generic_category()), len);
            }
            if (handler) {
                handler(std::error_code(0, std::generic_category()), len);
            }
        } else {
            memcpy(read_task_.buf, buf, read_task_.len);
            auto read_handler = read_task_.handler;
            auto read_len = read_task_.len;
            read_task_.reset();
            input_buffer_.append(buf+read_len, len-read_len);
            if (read_handler) {
                read_handler(std::error_code(0, std::generic_category()),
                             read_len);
            }
            if (handler) {
                handler(std::error_code(0, std::generic_category()), len);
            }
        }
    } else {
        input_buffer_.append(buf, len);
        if (handler) {
            if (input_buffer_.size() > 4096 * 32) {
                input_handler_ = [len, handler](std::error_code ec, std::size_t) {
                    if (ec) {
                        handler(ec, 0);
                    } else {
                        handler(ec, len);
                    }
                };
            } else {
                handler(std::error_code(0, std::generic_category()), len);
            }
        }
    }
    return;
}

void smux_sess::async_read_some(char *buf, std::size_t len, Handler handler) {
    if (destroy_) {
        if (handler) {
            handler(std::error_code(1, std::generic_category()), 0);
        }
        return;
    }
    auto input_buffer_size = input_buffer_.size();
    if (input_buffer_size == 0) {
        read_task_.buf = buf;
        read_task_.len = len;
        read_task_.handler = handler;
    } else {
        auto retrieve_size = input_buffer_size;
        if (len < retrieve_size) {
            retrieve_size = len;
        }
        input_buffer_.retrieve(buf, retrieve_size);
        assert(input_buffer_size-retrieve_size == input_buffer_.size());
        if (handler) {
            handler(std::error_code(0, std::generic_category()), retrieve_size);
        }
    }
    if (input_handler_ && input_buffer_.size() < 4096 * 4) {
        auto input_handler = input_handler_;
        input_handler_ = nullptr;
        input_handler(std::error_code(0, std::generic_category()), 0);
    }
    return;
}

static Buffers smux_sess_buffers(4120);

void smux_sess::async_write(char *buf, std::size_t len, Handler handler) {
    if (destroy_) {
        if (handler) {
            handler(std::error_code(1, std::generic_category()), 0);
        }
        return;
    }
    auto s = sm_.lock();
    if (!s) {
        if (handler) {
            handler(std::error_code(1, std::generic_category()), 0);
        }
        return;
    }
    auto f = frame{version_, cmdPsh, static_cast<uint16_t>(len), id_};
    auto data = smux_sess_buffers.get();
    f.marshal(data);
    memcpy(data + headerSize, buf, len);
    s->async_write(data, len + headerSize, [handler, data](std::error_code ec, std::size_t sz){
        smux_sess_buffers.push_back(data);
        if (handler) {
            handler(ec, sz);
        }
    });
}

smux_sess::~smux_sess() {
    auto s = sm_.lock();
    if (s) {
        s->async_write_frame(frame{version_, cmdFin, 0, id_}, nullptr);
    }
    // LOG(INFO) << "smux session destroyed!";
}

void smux_sess::call_this_on_destroy() {
    auto self = shared_from_this();

    Destroy::call_this_on_destroy();

    destroy_ = true;
    auto read_handler = read_task_.handler;
    auto input_handler = input_handler_;
    read_task_.reset();
    input_handler_ = nullptr;
    if (read_handler) {
        read_handler(std::error_code(1, std::generic_category()), 0);
    }
    if (input_handler) {
        input_handler(std::error_code(1, std::generic_category()), 0);
    }
}

void smux::async_write_frame(frame f, Handler handler) {
    TRACE
    if (is_destroyed()) {
        TRACE
        if (handler) {
            handler(std::error_code(1, std::generic_category()), 0);
        }
        return;
    }
    char *header = new char[headerSize];
    f.marshal(header);
    async_write(header, headerSize,
                [handler, header](std::error_code ec, std::size_t) {
                    if (ec) {
                        TRACE
                    }
                    delete[] header;
                    if (handler) {
                        handler(ec, 0);
                    }
                });
}

void smux::async_connect(
    std::function<void(std::shared_ptr<smux_sess>)> connectHandler) {
    TRACE
    auto self = shared_from_this();
    if (is_destroyed()) {
        if (connectHandler) {
            connectHandler(nullptr);
        }
        return;
    }
    nextStreamID_ += 2;
    auto sid = nextStreamID_;
    auto ss = std::make_shared<smux_sess>(service_, sid, VERSION,
                                          std::weak_ptr<smux>(self));
    async_write_frame(
        frame{VERSION, cmdSyn, 0, sid},
        [this, self, ss, connectHandler, sid](std::error_code ec, std::size_t) {
            if (ec) {
                if (connectHandler) {
                    connectHandler(nullptr);
                }
                return;
            }
            if (connectHandler) {
                sessions_.emplace(sid, std::weak_ptr<smux_sess>(ss));
                connectHandler(ss);
            }
        });
}

void smux::try_output(char *buf, std::size_t len, Handler handler) {
    tasks_.push_back(Task{buf, len, handler});
    if (!writing_) {
        writing_ = true;
        try_write_task();
    }
}

void smux::try_write_task() {
    auto self = shared_from_this();
    if (tasks_.empty()) {
        writing_ = false;
        return;
    }
    auto task = tasks_.front();
    tasks_.pop_front();
    output(task.buf, task.len,
           [this, self, task](std::error_code ec, std::size_t) {
               if (ec) {
                   return;
               }
               if (task.handler) {
                   task.handler(ec, task.len);
               }
               try_write_task();
           });
}

void smux::call_this_on_destroy() {
    auto self = shared_from_this();

    Destroy::call_this_on_destroy();

    auto read_handler = read_task_.handler;
    auto input_handler = input_task_.handler;
    read_task_.reset();
    input_task_.reset();
    if (read_handler) {
        read_handler(std::error_code(1, std::generic_category()), 0);
    }
    if (input_handler) {
        input_handler(std::error_code(1, std::generic_category()), 0);
    }
}
