#include "pch.h"

#include <cstdio>

#include "core/stages.h"
#include "core/wrapper.h"

namespace tube {

Wrapper::Wrapper(Connection* conn)
    : conn_(conn), pipeline_(Pipeline::instance())
{}

Request::Request(Connection* conn)
  : Wrapper(conn)
{
}

Request::~Request()
{
}

ssize_t
Request::read_data(byte* ptr, size_t sz)
{
    Buffer& buf = conn_->in_stream().buffer();
    size_t nbuffer_read = buf.size();
    ssize_t nread = 0;
    if (nbuffer_read > 0) {
        if (sz < buf.size()) {
            nbuffer_read = sz;
            sz = 0;
        } else {
            sz -= nbuffer_read;
        }
        buf.copy_front(ptr, nbuffer_read);
        ptr += nbuffer_read;
    }
    if (sz > 0) {
        utils::set_socket_blocking(conn_->fd(), true);
        nread = ::read(conn_->fd(), (void*) ptr, sz);
        utils::set_socket_blocking(conn_->fd(), false);
    }
    return nbuffer_read + nread;
}

size_t Response::kMaxMemorySizes = (4 << 20);

Response::Response(Connection* conn)
    : Wrapper(conn), max_mem_(kMaxMemorySizes), inactive_(false)
{
    out_stage_ = Pipeline::instance().write_back_stage();
}

Response::~Response()
{
    if (response_code() < 0) {
        conn_->set_cork();
        out_stage_->sched_add(conn_); // silently flush
    }
}

int
Response::response_code() const
{
    if (active() && !conn_->out_stream().is_done())
        return -1;
    return 0;
}

ssize_t
Response::write_data(const byte* ptr, size_t sz)
{
    OutputStream& out = conn_->out_stream();
    ssize_t ret;
    out.append_data(ptr, sz);

    if (out.memory_usage() > max_mem_) {
        ret = flush_data();
        if (ret <= 0) return ret;
    }
    return sz;
}

ssize_t
Response::write_string(const std::string& str)
{
    return write_data((const byte*) str.c_str(), str.length());
}

ssize_t
Response::write_string(const char* str)
{
    return write_data((const byte*) str, strlen(str));
}

void
Response::write_file(int file_desc, off64_t offset, off64_t length)
{
    conn_->out_stream().append_file(file_desc, offset, length);
}

ssize_t
Response::flush_data()
{
    OutputStream& out = conn_->out_stream();
    ssize_t nwrite = 0;
    utils::set_socket_blocking(conn_->fd(), true);
    while (true) {
        ssize_t rs = out.write_into_output();
        if (rs < 0) {
            nwrite = rs;
            break;
        } else if (rs == 0) {
            break;
        }
        nwrite += rs;
    }
    utils::set_socket_blocking(conn_->fd(), false);
    return nwrite;
}

void
Response::close()
{
    inactive_ = true;
    conn_->active_close();
}

}
