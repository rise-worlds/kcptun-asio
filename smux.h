#ifndef KCPTUN_SMUX_H
#define KCPTUN_SMUX_H

#include <utility>

#include "frame.h"
#include "utils.h"

class smux;

class smux_sess final : public std::enable_shared_from_this<smux_sess>,
                        public AsyncReadWriter,
                        public Destroy {
public:
    smux_sess(asio::io_service &io_service, uint32_t id, uint8_t version,
              std::weak_ptr<smux> sm);
    ~smux_sess();
    void input(char *buf, std::size_t len, const Handler& handler);
    void async_read_some(char *buf, std::size_t len, Handler handler) override;
    void async_write(char *buf, std::size_t len, Handler handler) override;

private:
    void call_this_on_destroy() override;

private:
    uint8_t version_;
    uint32_t id_;
    bool destroy_ = false;
    asio::io_service &service_;
    Task read_task_;
    Handler input_handler_;
    LinearBuffer input_buffer_;
    std::weak_ptr<smux> sm_;
};

class smux final : public std::enable_shared_from_this<smux>,
                   public AsyncReadWriter,
                   public AsyncInOutputer,
                   public Destroy {
public:
    smux(asio::io_service &io_service, OutputHandler handler = nullptr)
        : AsyncInOutputer(std::move(handler)), service_(io_service) {}

    void run();
    void async_input(char *buf, std::size_t len, Handler handler) override;
    void set_accept_handler(
        std::function<void(std::shared_ptr<smux_sess>)> handler) {
        acceptHandler_ = std::move(handler);
    }
    void async_write(char *buf, std::size_t len, Handler handler) override;
    void async_connect(
        const std::function<void(std::shared_ptr<smux_sess>)>& connectHandler);
    void async_write_frame(frame f, const Handler& handler);
    void async_read_some(char *buf, std::size_t len, Handler handler) override {
    }
    void remove_sess_by_id(uint32_t id) { sessions_.erase(id); }

private:
    void do_keepalive_checker();
    void do_keepalive_sender();
    void do_receive_frame();
    void do_stat_checker();
    void handle_frame(frame f);
    void async_read_full(char *buf, std::size_t len, const Handler& handler);
    void try_output(char *buf, std::size_t len, Handler handler);
    void try_write_task();
    void call_this_on_destroy() override;

private:
    bool writing_ = false;
    bool data_ready_ = true;
    char frame_header_[headerSize];
    char frame_data_[65536];
    uint16_t nextStreamID_ = 1;
    asio::io_service &service_;
    Task read_task_;
    Task input_task_;
    std::deque<Task> tasks_;
    std::function<void(std::shared_ptr<smux_sess>)> acceptHandler_;
    std::unordered_map<uint32_t, std::weak_ptr<smux_sess>> sessions_;
    std::shared_ptr<asio::high_resolution_timer> keepalive_check_timer_;
    std::shared_ptr<asio::high_resolution_timer> keepalive_sender_timer_;
    bool frame_flag = false;
};

#endif
