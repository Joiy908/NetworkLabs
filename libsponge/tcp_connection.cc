#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

//template <typename... Targs>
//void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active) return ;
    _time_since_last_segment_received = 0;

    const TCPHeader& h = seg.header();
    /**
     * special case: out-of-order sqn
     * case 1: reply keep-alive seg: sqn < _receiver.ackno()
     * case 2: future seg arrive: sqn > _receiver.ackno()
     */
    if (_receiver.ackno().has_value() && h.seqno != _receiver.ackno().value()
        && seg.length_in_sequence_space() <= 1 && !h.syn) {
        // if seg is future seg
        _receiver.segment_received(seg);
        if (h.ack) {
            _sender.ack_received(h.ackno, h.win);
            _sender.fill_window();
        }
        if (_segQueueFromSender.empty()) {
            _sender.send_empty_segment();
        }
        updateOutput();
        return ;
    }

    // special case: reset
    if (h.rst) {
        uncleanShutdown();
        return ;
    }

    // receiver status3:
    if (receiver_FIN_recv()) {
        if (h.syn) { // out-of-order seg
            return ;
        }
        if (!_linger_after_streams_finish) {  // CloseWait or LastAck
            if (h.fin) {                      // recv fin again
                _sender.send_empty_segment();
                updateOutput();
            }
            // receiver is closed, so just use sender to handle
            if (h.ack) {
                _sender.ack_received(h.ackno, h.win);
                if (_sender.finAcked()) {  // LastAck
                    cleanShutdown();
                } else {  // still in CloseWait
                    _sender.fill_window();
                    updateOutput();
                }
            }  // else: ignore
            return;
        }
        // else: in lingering
        if (h.ack) {
            _sender.ack_received(h.ackno, h.win);
        }
        if (h.fin) {  // re-recv FIN
            _sender.send_empty_segment();
            updateOutput();
        }
        return;
    }

    // receiver status1:
    if(receiver_in_listen()) {
        if (h.syn){
            _receiver.segment_received(seg);
            // recv: listen -> SYN_recv
            if (h.ack) {
                _sender.ack_received(h.ackno, h.win);
            }
            _sender.fill_window();
            updateOutput();
            // ack-for-syn
            if(_segments_out.empty()) {
                _sender.send_empty_segment();
                updateOutput();
            }
            // sender: close -> SYN_sent or SYN_sent -> SYN_recv
            return ;
        }
        // else: useless seg, ignore
    } else { //receiver status2: SYN_recv
        if (h.syn) { // out-of-order seg
            return ;
        }
        // === sender update status
        if (seg.length_in_sequence_space() != 0) {
            _receiver.segment_received(seg);
        }
        // may: sender:Fin_sent -> sender:Fin-acked aka(FIN_wait_1 -> FIN_wait_2)
        // may: recv: SYN_recv -> FIN_recv
        /**
         * two case here:
         *  1 sender has something to send: ack-for-fin is append to last seg by updateOutput();
         *  2 sender has nothing to send:
         *     - let us send a ack-for-fin
         */
        if (h.ack) {
            _sender.ack_received(h.ackno, h.win);
            _sender.fill_window();
            updateOutput();
        }
        // if ack for segs which is not empty ack-seg
        if(seg.length_in_sequence_space() > 0 && _segments_out.empty()) {
            _sender.send_empty_segment();
            updateOutput();
        }
        // if recv:SYN_recv -> recv:FIN_recv happens
        if (receiver_FIN_recv()) {

            // check if remote host first sends FIN
            if (!_sender.unAckQueue._finSent) { // if remote peer first send FIN
                // Established -> CloseWait(still allow to send data)
                _linger_after_streams_finish = false;
            } else {
                _time_wait = true;
            }
            // ack-for-fin
            if(_segments_out.empty()) {
                _sender.send_empty_segment();
                updateOutput();
            }
            // else: case1, ack-for-fin is appended
        }
    }
}

bool TCPConnection::active() const { return _active; }

// write and send
size_t TCPConnection::write(const string &data) {
    if (data.empty()) return 0;
    size_t writen = _sender.stream_in().write(data);
    _sender.fill_window();
    updateOutput();
    return writen;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!_active)
        return;
    _time_since_last_segment_received += ms_since_last_tick;

    _sender.tick(ms_since_last_tick);
    updateOutput();
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        sendRSTSeg();
        uncleanShutdown();
        return ;
    }

    if (_time_wait && _time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
        // in TIME_wait && check timeout
        cleanShutdown();
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    updateOutput();
}

void TCPConnection::connect() {
    _sender.fill_window();
    updateOutput();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            sendRSTSeg();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
