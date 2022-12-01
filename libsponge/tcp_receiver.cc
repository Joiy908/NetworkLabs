#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

//template <typename... Targs>
//void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader h = seg.header();
    uint64_t absIndex;

    // check sign
    if (h.syn) {
        if (_synRecv) return ;
        _synRecv = true;
        _isn = h.seqno;
        absIndex = 0;
    }else {
        if (!_synRecv) return; // dummy package
        // 1. get absIndex
        // 1.2 get checkpoint: the absIndex of
        // the last reassembled byte as the checkpoint.
        uint64_t checkpoint = _reassembler.stream_out().bytes_written();
        absIndex = unwrap(h.seqno,_isn, checkpoint) - 1;
    }
    _reassembler.push_substring(seg.payload().copy(), absIndex, h.fin);

    // update _ack
    auto& output = _reassembler.stream_out();
    uint64_t ex_sqn = output.bytes_written() + 1;
    _ackno = wrap(ex_sqn, _isn);

    if (output.input_ended()){ // means receive FIN
        _ackno  = _ackno + 1;
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (_synRecv) return {_ackno};
    else return {nullopt};
}

size_t TCPReceiver::window_size() const {
    // dis btw the
    // ``first unassembled'' and the ``first unacceptable'' index.
    // firstUnacceptableIdx = _exIdx + _capacity - _output.buffer_size();
    // firstUnassembled = _exIdx
    return _capacity - _reassembler.stream_out().buffer_size();
}
