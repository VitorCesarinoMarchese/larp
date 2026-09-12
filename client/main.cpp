#include "common/runtime.hpp"
#include "platform/udp.hpp"
#include "transport/reassembly.hpp"
#include <array>
#include <iomanip>
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc!=4) { std::cerr<<"Usage: larp-client BIND_IPv4 PORT SECONDS\n"; return 1; }
        using namespace larp;
        const auto local=Endpoint::parse(argv[1],static_cast<std::uint16_t>(number(argv[2],0,65535)));
        const auto seconds=number(argv[3],1,86400);
        UdpSocket socket(local);
        Reassembler reassembly;
        std::optional<Endpoint> peer;
        std::array<std::byte,datagram_size> wire{};
        std::uint64_t packets=0, bytes=0, valid=0, corrupt=0, foreign=0;
        std::uint64_t previous_valid=0, previous_bytes=0;
        const auto start=now_us(); auto report_time=start;
        auto report=[&](std::uint64_t now) {
            const double elapsed=static_cast<double>(now-report_time)/1000000.0;
            const auto& s=reassembly.stats;
            std::cout<<std::fixed<<std::setprecision(2)
                <<"FPS: "<<static_cast<double>(valid-previous_valid)/elapsed
                <<" Packets: "<<packets<<" Packet loss: "<<(s.expected?100.0*static_cast<double>(s.missing)/static_cast<double>(s.expected):0.0)
                <<"% Dropped: "<<s.dropped<<" frames Missing: "<<s.missing<<" Skipped: "<<s.skipped
                <<" RTT: N/A Throughput: "<<static_cast<double>(bytes-previous_bytes)*8.0/elapsed/1000000.0
                <<" Mbps Validated: "<<valid<<" Corrupt: "<<corrupt<<" Invalid: "<<s.invalid
                <<" Duplicates: "<<s.duplicates<<" Stale: "<<s.stale<<" Foreign: "<<foreign<<" \n"<<std::flush;
            report_time=now; previous_valid=valid; previous_bytes=bytes;
        };
        std::cout<<"Listening: "<<socket.local_endpoint().port<<std::endl;
        while(now_us()-start<std::uint64_t(seconds)*1000000) {
            const auto received=socket.receive(wire,10);
            const auto now=now_us(); reassembly.expire(now);
            if(received.status!=ReceiveStatus::timeout) {
                if(peer && received.peer!=*peer) ++foreign;
                else {
                    ++packets; bytes+=received.size;
                    if(received.status==ReceiveStatus::truncated) ++reassembly.stats.invalid;
                    else {
                        const auto packet=std::span(wire).first(received.size);
                        const auto header=decode(packet);
                        if(header && !peer) peer=received.peer;
                        const auto frame=reassembly.accept(packet,now);
                        if(frame) {
                            bool correct=true;
                            for(std::size_t i=0;i<frame->size();++i)
                                if((*frame)[i]!=synthetic_byte(header->frame_id,i)) { correct=false; break; }
                            if(correct) ++valid; else ++corrupt;
                        }
                    }
                }
            }
            if(now-report_time>=1000000) report(now);
        }
        reassembly.finish(); report(now_us());
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
