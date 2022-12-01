#include "stream_reassembler.hh"
#include <algorithm>
#include <iostream>
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

//template <typename... Targs>
//void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity) {}

std::map<size_t, std::string>::iterator StreamReassembler::getBackItr(size_t idx) {
    map<size_t, string>::iterator bIt;
    if (idx == 0) // can not -1, which will cause overflow
        bIt = _unassembledStrs.upper_bound(idx);
    else
        bIt = _unassembledStrs.upper_bound(idx-1);
    return bIt;
}

std::map<size_t, std::string>::iterator StreamReassembler::getFrontItr(size_t idx) {
    // +1 to get fIt <= idx
    auto fIt = _unassembledStrs.lower_bound(idx+1);
    if(fIt != _unassembledStrs.begin()) {
        --fIt;
    }
    if (fIt->first > idx) fIt = _unassembledStrs.end();
    return fIt;
}

std::pair<bool,size_t>  StreamReassembler::cutData(size_t idx, std::string& data) {
    // 1 handle front: find idx
    if (idx < _exIdx) {
        if (idx + data.size() <= _exIdx) { // totally out of date data
            return {true, 0};
        }
        if (idx + data.size() > _exIdx) { // process part of new data
            data.erase(data.begin(), std::next(data.begin(),_exIdx - idx));
            idx = _exIdx;
        }
    } else { // idx >= _exIdx
        // get fIdx(frontIdx) nearest to index
        auto fIt = getFrontItr(idx);

        // if (fIt == _unassembledStrs.end())
        // do nothing, waiting for check of back
        if(fIt != _unassembledStrs.end()) {
            auto& [fIdx, fData] = *fIt;
            size_t fTailIdxNext = fIdx + fData.size();
            // if(fTailIdxNext <= idx) :do nothing
            if (fTailIdxNext > idx) {
                if(idx+data.size() <= fTailIdxNext) {
                    // fData contain data
                    // update old data with new one
                    fData.replace(idx-fIdx, data.size(), data);
                    return {true, 0};
                } else {
                    // cut data, update idx
                    // update overlapped part
                    fData.replace(idx-fIdx, fTailIdxNext-idx, data,0, fTailIdxNext-idx);
                    data.erase(data.begin(), next(data.begin(),fTailIdxNext-idx));
                    idx = fTailIdxNext;
                }
            }
        }
    }
    // 2 handle back: cut data tail
    // bIt >= idx
    auto bIt = getBackItr(idx);

    while (bIt != _unassembledStrs.end()) {
        auto& [bIdx, bData] = *bIt;
        if (idx + data.size() <= bIdx) // no overlap
            break ;
        else {
            // case1: part overlap => cut data
            if (idx + data.size() <= bIdx + bData.size()){
                if (idx == bIdx) { // totally overlapped
                    bData.replace(0, data.size(), data);
                    return {true, 0};
                }
                // else
                bData.replace(0, idx+data.size()-bIdx, data, bIdx-idx, idx+data.size()-bIdx);
                data.erase(data.begin()+(bIdx - idx), data.end());
                break ;
            } else { // case2: total overlap => cut bIt, and check nextBack
                _unassembledSize -= bData.size();
                bIt = _unassembledStrs.erase(bIt);
            }
        }
    }
    return {false, idx};
}


//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // 0 check eof
    if (_output.input_ended()) return;
    if (eof) {
        _eofSignRecv = true;
        _eofIdx = index + data.size(); //this idx after last byte!
        if (_eofIdx == _exIdx) _output.end_input();
    }
    if (data.size() == 0) return;

    // 1 _cutString
    std::string dataCopy = data;
    auto [drop, newIdx] = cutData(index, dataCopy);

    if (drop) return ;
    //else
    // 2 try insert to _unassembledStrs
    size_t firstUnacceptableIdx = _exIdx + _capacity - _output.buffer_size();

    if(newIdx < firstUnacceptableIdx) {
        _unassembledStrs[newIdx] = dataCopy;
        _unassembledSize += dataCopy.size();
    }
    if (_exIdx != _unassembledStrs.begin()->first)
        return ;
    // else

    // 3 try push
    auto it = _unassembledStrs.begin();
    while (it != _unassembledStrs.end()) {
        auto& [currIdx, currData] = *it;
        if(it->first > _exIdx) break ;
        // else // _unassembledStrs.begin()->first == _exIdx
        size_t pushed = _output.write(currData);
        _exIdx += pushed;
        if(_eofSignRecv && _eofIdx <= _exIdx) {
            _output.end_input();
        }
        // else not reach eof
        _unassembledSize -= pushed;
        if (pushed < currData.size()) { // _output is busy
            // update
            _unassembledStrs.erase(it);
            currData.erase(0, pushed);
            _unassembledStrs.insert({currIdx + pushed, currData});
            break ;
        }else { // _output eat all currData
            it = _unassembledStrs.erase(it);
        }
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembledSize; }

bool StreamReassembler::empty() const { return _unassembledSize == 0; }