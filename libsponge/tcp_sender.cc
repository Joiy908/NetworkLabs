#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

//template <typename... Targs>
//void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : timer{retx_timeout}
    , _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return unAckQueue.bytesInFlight(); }

void TCPSender::fill_window() {
    if (unAckQueue._finAcked || (unAckQueue._finSent && !unAckQueue._finAcked)) {
        return ;
    }
    if (!unAckQueue._synSent) {
        firstPush(makeSynSeg());
        return ;
    }
    if (!unAckQueue._finSent && _stream.eof() && remainSpace() > 1) {
        firstPush(makeFirstFinSeg());
    }
    if (unAckQueue._synSent && !unAckQueue._synAcked) {
        return ;
    }

    while(remainSpace()>0 && hasContentToPush()) {
        TCPSegment seg = makePayLoadSeg();
        unAckQueue.push(seg);
        firstPush(std::move(seg));
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    setWindowSize(window_size);

    // the index of the last
    // reassembled byte as the checkpoint.
    uint64_t absAckno = unwrap(ackno, _isn, unAckQueue._baseSqn);
    if ((unAckQueue._finSent && absAckno > unAckQueue.ackForFinSqn()) ||
        absAckno <= 0 || absAckno < unAckQueue._baseSqn) {
        return ;
    }
    if(absAckno > unAckQueue._baseSqn) { // expected ack received
        unAckQueue.updateBase(absAckno);
        if (!unAckQueue.empty() || (unAckQueue._finSent && !unAckQueue._finAcked))
            timer.startTimer();
        else timer.stopTimer();
    }
    // ignore other ack
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (timer._isTimerRunning) {
        timer._msSinceBaseSegSent += ms_since_last_tick;
        if (timer._msSinceBaseSegSent >= timer._timeout) {
            // handler timer
            timeoutRetransmit();
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return timer._consecutive_retransmissions; }

// only called when generate empty RST seg
void TCPSender::send_empty_segment() {
    TCPSegment seg;

    seg.header().seqno = wrap(unAckQueue._next_seqno, _isn);
    _segments_out.push(std::move(seg));
}
