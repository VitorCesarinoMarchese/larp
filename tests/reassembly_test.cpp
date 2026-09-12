#include "transport/reassembly.hpp"
#include <array>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
int main() {
    using namespace larp;
    Reassembler r;
    std::array<std::byte, datagram_size> wire{};
    auto packet = [&](std::uint64_t id, std::uint32_t index, std::uint64_t now) {
        Header h{id,index,2,77,index == 0 ? payload_capacity : 3};
        encode(h, wire);
        for (std::size_t i=0;i<h.payload_size;++i) wire[header_size+i]=synthetic_byte(id,index*payload_capacity+i);
        return r.accept(std::span(wire).first(header_size+h.payload_size),now);
    };
    CHECK(!packet(1,1,0)); CHECK(!packet(1,1,1)); CHECK(r.stats.duplicates == 1);
    auto frame=packet(1,0,2); CHECK(frame && frame->size()==payload_capacity+3);
    for(std::size_t i=0;i<frame->size();++i) CHECK((*frame)[i]==synthetic_byte(1,i));
    CHECK(!packet(1,0,3)); CHECK(!packet(2,0,10));
    CHECK(!packet(3,1,11)); CHECK(r.stats.dropped==1 && r.stats.missing==1);
    r.expire(100011); CHECK(r.stats.dropped==2 && r.stats.missing==2);
    CHECK(!packet(3,0,100012)); CHECK(!packet(5,0,100013)); CHECK(r.stats.skipped==1);
    r.expire(200013); CHECK(r.stats.dropped==3);
    CHECK(!r.accept(std::span(wire).first(2),200014)); CHECK(r.stats.invalid==1);
}
