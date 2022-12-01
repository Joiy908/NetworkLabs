#include "byte_stream.hh"

#include <algorithm>
#include <string>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _capacity(capacity) {}

size_t ByteStream::write(const string &data) {
    if (_isInputEnd || _capacity == _size)
        return 0;
    size_t toWrite = min(_capacity - _size, data.size());
    _buf.insert(_buf.begin() + _size, data.begin(), data.begin() + toWrite);
    _size += toWrite;
    _num_bytes_written += toWrite;
    return toWrite;
}
size_t ByteStream::remaining_capacity() const { return _capacity - _size; }

void ByteStream::end_input() { _isInputEnd = true; }

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t toPeek = min(_size, len);
    return string(_buf.begin(), _buf.begin() + toPeek);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t toPop = min(_size, len);
    _buf.erase(_buf.begin(), _buf.begin() + toPop);
    _size -= toPop;
    _num_bytes_read += toPop;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    std::string rst = peek_output(len);
    pop_output(len);
    return rst;
}

bool ByteStream::input_ended() const { return _isInputEnd; }

size_t ByteStream::buffer_size() const { return _size; }

bool ByteStream::buffer_empty() const { return _size == 0; }

bool ByteStream::eof() const { return _size == 0 && _isInputEnd; }

size_t ByteStream::bytes_written() const { return _num_bytes_written; }

size_t ByteStream::bytes_read() const { return _num_bytes_read; }
