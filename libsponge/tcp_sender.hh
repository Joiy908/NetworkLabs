#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>
#include <deque>

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    // helper class
    class UnAckQueue {
      private:
        // un-acknowledged bytes
        std::deque<char> _unAcked{};
        // un-acknowledged seg size
        // {absSqn, payload.size()
        std::deque<size_t> _unAckedPayloadSize{};

      public:

        // === about flag
        // does SYN have been sent
        bool _synSent{false};
        bool _synAcked{false};
        bool _finSent{false};
        bool _finAcked{false};

        // === about win
        // the absolute sqn for base
        uint64_t _baseSqn{0};
        //! the (absolute) sequence number for the next byte to be sent
        uint64_t _next_seqno{0};

        uint64_t _win_size{1};

        // assume _finSent == True
        uint64_t ackForFinSqn() const { return _next_seqno; }

        //! assume _finSent is True !
        uint64_t ackSqnForFin() const { return _next_seqno; }

        size_t bytesInFlight() const { return _next_seqno - _baseSqn; }

        // if queue is empty, not count SYN and FIN !
        bool empty() const { return _unAcked.empty(); }

        // assume seg is firstPushed, and != SYNSeg and seg is not empty
        void push(TCPSegment seg) {
            // ignore seg.header().fin
            // copy to window and push out
            std::string_view payload = seg.payload().str();
            _unAcked.insert(_unAcked.end(), payload.begin(), payload.end());
            _unAckedPayloadSize.emplace_back(payload.size());
        }

        // called by recv_ack
        // assume:  1 < ack < finSqn, ack > baseSqn
        TCPSegment frontSeg(const WrappingInt32 isn) const {
            TCPSegment seg;
            seg.header().seqno = wrap(_baseSqn, isn);
            const size_t toSend = _unAckedPayloadSize.front();

            std::string payload(_unAcked.begin(), _unAcked.begin()+toSend);
            seg.payload() = std::move(payload);
            if (_finSent && ackSqnForFin()-1 == _baseSqn+toSend) {
                // fin was sent and seg is last seg
                seg.header().fin = true;
            }
            return seg;
        }

        // assume  ack <= _nextSqn
        void updateBase(uint64_t ackno) {
            if (ackno <= _baseSqn)
                return;
            else if (ackno == 1) {
                _synAcked = true;
                ++_baseSqn;
                return;
            } else if (_finSent && ackno == ackSqnForFin()) {
                _finAcked = true;

                if (!empty())
                    erase(ackno);
                else {
                    ++_baseSqn;
                    return;
                }

            } else {
                if (!empty())
                    erase(ackno);
            }
        }

        // assume !empty and 1 < ack < ackForFin
        void erase(uint64_t ackno) {
            // handle ack-for-FIN
            if (_finSent && ackno == ackSqnForFin()){
                _baseSqn = ackno;
                _unAcked.clear();
                _unAckedPayloadSize.clear();
                return ;
            }
            // else
            size_t bytesToErase = 0;
            auto sizeIt = _unAckedPayloadSize.begin();
            while(sizeIt != _unAckedPayloadSize.end()) {
                auto size = *sizeIt;
                size_t nextSqn = _baseSqn + size;
                if (nextSqn == ackno) {
                    bytesToErase += size;
                    _unAckedPayloadSize.erase(_unAckedPayloadSize.begin(), ++sizeIt);
                    _baseSqn = ackno;
                    break ;
                } else if(nextSqn < ackno) {
                    bytesToErase += size;
                    _baseSqn = nextSqn;
                    ++sizeIt;
                    continue ;
                } else { // nextSqn < ack: hard to handler
                    auto bytesAcked = ackno - _baseSqn;
                    bytesToErase += bytesAcked;
                    _baseSqn = ackno;
                    *sizeIt = size - bytesAcked;
                    break ;
                }
            }
            _unAcked.erase(_unAcked.begin(), _unAcked.begin()+bytesToErase);
        }

        // not contain emptyFinSeg, call only by check valid duplicate ack
        size_t size() const { return _unAckedPayloadSize.size(); }
    };

    struct Timer {
        explicit Timer(unsigned int initial_timeout) :
            _initial_retransmission_timeout{initial_timeout}, _timeout{initial_timeout} {}

        //! retransmission timer for the connection
        const unsigned int _initial_retransmission_timeout;
        unsigned int _timeout;

        bool _isTimerRunning{false};
        // time elapses since last basePacket sent
        size_t _msSinceBaseSegSent{0};
        unsigned int _consecutive_retransmissions{0};
        // false if receive win=0
        bool _doubleTimeout{true};

        // call by first push
        void startTimer() {
            _isTimerRunning = true;
            _msSinceBaseSegSent = 0;
            _consecutive_retransmissions = 0;
            _timeout = _initial_retransmission_timeout;
        }
        // call by recv_ack
        void stopTimer() {
            startTimer();
            _isTimerRunning = false;
        }
    };


    Timer timer;

    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //=== pushHelpers : handle timer

    // the seg never been pushed before
    void firstPush(TCPSegment && seg) {
        _segments_out.push(std::move(seg));
        if (!timer._isTimerRunning)
            timer.startTimer();
    }

    void timeoutRetransmit(){
        if (unAckQueue._finAcked) {
            return ;
        }
        // 1. find last send seg and push
        TCPSegment seg;
        if(unAckQueue._finSent && !unAckQueue._finAcked) {
            if (unAckQueue.empty())
                seg = makeNotFirstFinSeg();
            else seg = unAckQueue.frontSeg(_isn);
        } else if (unAckQueue._synSent && !unAckQueue._synAcked) {
            seg = makeSynSeg();
        } else if (!unAckQueue.empty()) {
            seg = unAckQueue.frontSeg(_isn);
        }
        _segments_out.push(seg);
        // 2. handle timer
        if (timer._doubleTimeout) {
            timer._timeout *= 2;
        }
        timer._msSinceBaseSegSent = 0;
        ++timer._consecutive_retransmissions;
    }


    // empty seg maker
    // call by first push, or retransmit.
    TCPSegment makeSynSeg() {
        TCPSegment seg;
        TCPHeader &h = seg.header();
        h.seqno = wrap(0, _isn);

        h.syn = true;

        if (!unAckQueue._synSent) {
            unAckQueue._synSent = true;
            ++unAckQueue._next_seqno;
        }

        if(_stream.eof() && remainSpace() >= 2) {
            h.fin = true;
            unAckQueue._finSent = true;
            ++unAckQueue._next_seqno;
        }
        return seg;
    }

    // called by fill_will
    // assume !unAckQueue._finSent && !_stream.eof() && remainSpace() > 1
    TCPSegment makeFirstFinSeg() {
        TCPSegment seg;

        seg.header().seqno = wrap(unAckQueue._next_seqno, _isn);
        seg.header().fin = true;
        ++unAckQueue._next_seqno;
        unAckQueue._finSent = true;
        return seg;
    }

    // called by recv_ack, to retransmit Seg
    // called by timeoutRetransmit, to ~
    // assume unAckQueue._finSent && absAckno == unAckQueue.ackForFinSqn()-1
    TCPSegment makeNotFirstFinSeg() const {
        TCPSegment seg;
        TCPHeader &h = seg.header();
        h.fin = true;
        h.seqno = wrap(unAckQueue.ackSqnForFin()-1, _isn);
        return seg;
    }


    // not count syn
    bool hasContentToPush() const {
        return _stream.buffer_size() > 0 ||
               (_stream.eof() && !unAckQueue._finSent);
    }

    // assume remainSpace()>0 && hasContentToPush() && !unAckQueue._finSent
    TCPSegment makePayLoadSeg() {
        TCPSegment seg;
        TCPHeader &h = seg.header();
        h.seqno = wrap(unAckQueue._next_seqno, _isn);

        size_t toSend = std::min(remainSpace(), TCPConfig::MAX_PAYLOAD_SIZE);
        std::string payload = _stream.read(toSend);


        // check if need carry FIN
        if(_stream.eof() && remainSpace() - payload.size() > 0) {
            seg.header().fin = true;

            unAckQueue._finSent = true;
            ++unAckQueue._next_seqno;
            // now _next_seqno eq ack-for-fin
        }
        // change after remainSpace() is called
        unAckQueue._next_seqno += payload.size();
        seg.payload() = std::move(payload);
        return seg;
    }

    void setWindowSize(const uint16_t size) {
        timer._doubleTimeout = size != 0;
        unAckQueue._win_size = size > 0 ? size : 1;
    }

  public:
    UnAckQueue unAckQueue{};
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}


    // helper method
    size_t remainSpace() const {
        return unAckQueue._win_size - unAckQueue.bytesInFlight();
    }

    bool finAcked() const {
        return unAckQueue._finAcked;
    }

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return unAckQueue._next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(unAckQueue._next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
