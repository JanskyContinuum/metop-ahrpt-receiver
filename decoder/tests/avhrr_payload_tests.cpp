#include "avhrr_payload.h"
#include "sample_unpack.h"
#include "ccsds_randomizer.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool threw = false;
    try { action(); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "Expected bounds rejection");
}
// Test-only packer: assign each one-bit by its absolute position, independently
// of the decoder's shift accumulator. Fill out-of-range bits with ones.
Bytes pack(std::span<const std::uint16_t> samples, std::size_t offset = 0) {
    Bytes bytes((offset + samples.size() * 10 + 7) / 8, 0xff);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        require(samples[i] <= 1023, "Invalid test sample");
        for (unsigned b = 0; b < 10; ++b) {
            const auto position = offset + i * 10 + b;
            const auto mask = static_cast<std::uint8_t>(1U << (7 - position % 8));
            bytes[position / 8] &= static_cast<std::uint8_t>(~mask);
            if (samples[i] & (1U << (9 - b))) bytes[position / 8] |= mask;
        }
    }
    return bytes;
}
std::uint16_t value(std::size_t i) { return static_cast<std::uint16_t>((i * 37 + i / 7) % 1024); }
void checksum(Bytes& packet) {
    packet[packet.size()-2] = packet.back() = 0;
    for (std::size_t i = 0; i < packet.size()-2; ++i)
        packet[packet.size()-2 + i%2] ^= packet[i];
}
Bytes make_packet(std::uint16_t apid = 103) {
    Bytes p(12966);
    // Header, CDS timestamp and SBT deliberately have nonzero distinct octets.
    const Bytes prefix{8, static_cast<std::uint8_t>(apid), 0xd2, 0x34, 0x32, 0x9f,
        0x25, 0xeb, 0x01, 0xe1, 0x83, 0xd7, 0x02, 0xcc, 0, 0x14, 0x13, 0x0d, 0xe0, 0x1a};
    std::copy(prefix.begin(), prefix.end(), p.begin());
    std::vector<std::uint16_t> words(10355);
    for (std::size_t i = 0; i < words.size(); ++i) words[i] = value(i);
    const auto packed = pack(words);
    std::copy(packed.begin(), packed.end(), p.begin()+20);
    p[p.size()-3] &= 0xfc;
    checksum(p);
    return p;
}
void geometry(const metop::AvhrrScan& s) {
    for (std::size_t c = 0; c < 5; ++c) {
        require(s.earth[c].size() == 2048, "Wrong Earth width");
        for (std::size_t x = 0; x < 2048; ++x)
            require(s.earth[c][x] == value(55 + 5*x + c), "Earth pixel/order/boundary mismatch");
        for (std::size_t x = 0; x < 10; ++x) {
            require(s.space[c][x] == value(5*x+c), "Space view mismatch");
            require(s.back_scan[c][x] == value(10305+5*x+c), "Back scan mismatch");
        }
        require(s.ramp[c] == value(50+c), "Ramp leaked into Earth data");
        require(s.ir_target_temperature[c] == value(10295+c), "IR telemetry mismatch");
        require(s.patch_temperature[c] == value(10300+c), "Patch telemetry mismatch");
    }
}
metop::ReassembledPacket reassembled(Bytes b) {
    metop::ReassembledPacket p;
    p.source.spacecraft_id=11; p.source.vcid=9; p.source.counter=100; p.end_counter=114;
    p.bytes=std::move(b); p.header=metop::validate_space_packet(p.bytes);
    return p;
}
std::vector<std::array<std::uint8_t, 892>> frames() {
    auto stream=make_packet();
    const auto second=make_packet(104);
    stream.insert(stream.end(),second.begin(),second.end());
    const auto idle_start=stream.size();
    const auto idle_size=882-stream.size()%882;
    require(idle_size >= 7, "Idle test packet too short");
    stream.resize(stream.size()+idle_size);
    stream[idle_start]=7; stream[idle_start+1]=255; stream[idle_start+2]=0xc0;
    stream[idle_start+4]=static_cast<std::uint8_t>((idle_size-7)>>8);
    stream[idle_start+5]=static_cast<std::uint8_t>(idle_size-7);
    std::vector<std::array<std::uint8_t, 892>> result;
    for (std::size_t offset=0; offset<stream.size(); offset+=882) {
        std::array<std::uint8_t,892> f{};
        f[0]=0x42; f[1]=0xc9; f[4]=static_cast<std::uint8_t>(100+offset/882);
        std::uint16_t fhp=0x7ff;
        for (const auto start : {std::size_t{0}, second.size(), idle_start})
            if(start>=offset && start<offset+882) { fhp=static_cast<std::uint16_t>(start-offset); break; }
        f[8]=static_cast<std::uint8_t>(fhp>>8); f[9]=static_cast<std::uint8_t>(fhp);
        std::copy_n(stream.begin()+static_cast<std::ptrdiff_t>(offset),882,f.begin()+10);
        result.push_back(f);
    }
    return result;
}
void expect_error(Bytes p, metop::AvhrrPayloadError error, bool fix_vpc=true) {
    if(fix_vpc) checksum(p);
    const auto result=metop::decode_avhrr_packet(p);
    require(!result.scan && result.error==error,"Malformed packet accepted or misclassified");
}
}

int main(int argc, char** argv) {
    try {
        require(argc>=2,"Missing case");
        const std::string test=argv[1];
        using Error=metop::AvhrrPayloadError;
        if(test=="packing") {
            std::vector<std::uint16_t> samples{0,1023,1,1022};
            require(metop::unpack_10bit_msb(Bytes{0x00,0x3f,0xf0,0x07,0xfe},0,4)==samples,"Known bit vector");
            for(std::size_t i=0;i<1024;++i) samples.push_back(static_cast<std::uint16_t>(i));
            for(std::size_t offset=0;offset<16;++offset)
                require(metop::unpack_10bit_msb(pack(samples,offset),offset,samples.size())==samples,"Pack/unpack round trip");
        } else if(test=="bit_bounds") {
            require(metop::unpack_10bit_msb({},0,0).empty(),"Empty bit range");
            require(metop::unpack_10bit_msb(Bytes{0xff},8,0).empty(),"Empty at end");
            for(const auto offset : {std::size_t{9},std::numeric_limits<std::size_t>::max()})
                rejects([&]{(void)metop::unpack_10bit_msb(Bytes{0xff},offset,0);});
            rejects([]{(void)metop::unpack_10bit_msb(Bytes{0xff},0,1);});
            rejects([]{(void)metop::unpack_10bit_msb(Bytes{0xff,0xff},7,1);});
            rejects([]{(void)metop::unpack_10bit_msb(Bytes{0xff},0,std::numeric_limits<std::size_t>::max());});
        } else if(test=="geometry") {
            for(const auto apid : {103U,104U}) {
                const auto result=metop::decode_avhrr_packet(make_packet(static_cast<std::uint16_t>(apid)));
                require(result.scan.has_value() && result.error==Error::none,"Valid scan rejected");
                const auto& s=*result.scan; geometry(s);
                require(s.header.sequence_count==0x1234 && s.header.data_length_field==12959,"Packet fields");
                require(s.utc_days==9707 && s.utc_milliseconds==31556567 && s.utc_microseconds==716,"CDS field offsets");
                require(s.sbt_seconds==0x14130d && s.sbt_fraction==0xe01a,"SBT field offsets");
                const std::array expected{metop::AvhrrChannel::ch1,metop::AvhrrChannel::ch2,
                    apid==103 ? metop::AvhrrChannel::ch3a : metop::AvhrrChannel::ch3b,
                    metop::AvhrrChannel::ch4,metop::AvhrrChannel::ch5};
                require(s.channels==expected,"Channel-3 mode/order");
            }
        } else if(test=="truncated") {
            auto p=make_packet();
            for(std::size_t n=0;n<p.size();++n) {
                const auto result=metop::decode_avhrr_packet(std::span(p).first(n));
                require(!result.scan && result.error==Error::packet_length,"Truncated payload accepted");
            }
            p.push_back(0); expect_error(p,Error::packet_length,false);
        } else if(test=="headers") {
            for(const auto index : {0U,1U,2U,4U,5U}) {
                auto p=make_packet(); p[index]^=index==0 ? 0x20 : index==2 ? 0x40 : 1;
                expect_error(p,Error::packet_header);
            }
            auto p=make_packet();p[0]|=0x10;expect_error(p,Error::packet_header);
            p=make_packet();p[0]&=0xf7;expect_error(p,Error::packet_header);
        } else if(test=="vpc") {
            for(const auto index : {6U,13U,20U,1000U,12963U,12964U,12965U}) {
                auto p=make_packet();p[index]^=1;expect_error(p,Error::checksum,false);
            }
        } else if(test=="metadata") {
            auto p=make_packet();p[12]=3;p[13]=0xe8;expect_error(p,Error::time);
            p=make_packet();p[8]=0xff;expect_error(p,Error::time);
            p=make_packet();p[14]=1;expect_error(p,Error::sbt_reserved);
            p=make_packet();p[p.size()-3]|=1;expect_error(p,Error::filler);
            p=make_packet();
            const std::uint32_t leap=86400999;
            for(unsigned i=0;i<4;++i)p[8+i]=static_cast<std::uint8_t>(leap>>(24-8*i));
            checksum(p);require(metop::decode_avhrr_packet(p).scan.has_value(),"Leap second rejected");
        } else if(test=="processor") {
            std::ostringstream log;std::uint64_t delivered=0;
            metop::AvhrrScanProcessor processor(log,false,[&](const auto& p,const auto& s){
                require(p.source.spacecraft_id==11,"Lost provenance");geometry(s);++delivered;
            });
            auto p=reassembled(make_packet());p.source.vcid=10;processor.consume(p,0);
            p=reassembled(make_packet(105));processor.consume(p,1);
            p=reassembled(make_packet());p.header.apid=105;processor.consume(p,2);
            processor.consume(reassembled(make_packet(104)),3);
            p=reassembled(make_packet());p.bytes[21]^=1;processor.consume(p,4);
            const auto& s=processor.statistics();
            require(delivered==2 && s.candidates==3 && s.accepted==2 && s.rejected==1
                && s.channel_3a==1 && s.channel_3b==1 && s.errors[3]==1,"Scan accounting/filtering");
            require(log.str().find("not_applied,vpc")!=std::string::npos,"No-RS provenance/rejection missing");
        } else if(test=="reassembly") {
            std::ostringstream log;std::uint64_t delivered=0;metop::PacketReassembler r;
            metop::AvhrrScanProcessor processor(log,true,[&](const auto& p,const auto& s){
                geometry(s); require(p.end_counter-p.source.counter>=14,"Scan did not span VCDUs");++delivered;
            });
            for(const auto& f:frames())for(const auto& p:r.consume(f))processor.consume(p,delivered);
            r.finish();require(delivered==2 && r.statistics().truncated_packets==0,"Cross-VCDU scan reconstruction");
        } else if(test=="output_failure") {
            std::ostringstream log;metop::AvhrrScanProcessor processor(log,true);
            log.setstate(std::ios::badbit);bool threw=false;
            try{processor.consume(reassembled(make_packet()),0);}catch(const std::runtime_error&){threw=true;}
            require(threw,"Scan log failure ignored");
        } else if(test=="fixture") {
            require(argc==3 || (argc==4 && std::string_view(argv[3])=="bad_vpc"),"Fixture path/mode missing");
            std::ofstream out(std::filesystem::path(std::u8string(std::string_view(argv[2]).begin(), std::string_view(argv[2]).end())),std::ios::binary);
            auto fixture_frames=frames();
            if(argc==4) fixture_frames.front()[30]^=1; // Break only the first packet VPC.
            for(const auto& f:fixture_frames) {
                std::array<std::uint8_t,1024> cadu{};cadu[0]=0x1a;cadu[1]=0xcf;cadu[2]=0xfc;cadu[3]=0x1d;
                std::copy(f.begin(),f.end(),cadu.begin()+4);
                metop::derandomize(std::span(cadu).subspan<4, 1020>());
                out.write(reinterpret_cast<const char*>(cadu.data()),static_cast<std::streamsize>(cadu.size()));
            }
            out.close();require(bool(out),"Fixture write failed");
        } else if(test=="capture") {
            require(argc==3,"Capture path missing");
            std::ifstream in(std::filesystem::path(std::u8string(std::string_view(argv[2]).begin(), std::string_view(argv[2]).end())),std::ios::binary);
            require(bool(in),"Cannot open capture dump");
            const Bytes bytes{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
            std::size_t offset=0,count=0;
            while(offset<bytes.size()) {
                const auto rest=std::span(bytes).subspan(offset);
                const auto header=metop::parse_space_packet_header(rest);
                require(header.total_packet_bytes<=rest.size(),"Truncated capture");
                const auto decoded=metop::decode_avhrr_packet(rest.first(header.total_packet_bytes));
                require(decoded.scan.has_value(),"Capture packet rejected");
                const auto& s=*decoded.scan;
                std::cout<<s.header.sequence_count;
                for(std::size_t c=0;c<5;++c) {
                    std::uint64_t hash=14695981039346656037ULL;
                    for(const auto v:s.earth[c]) { hash^=v>>8;hash*=1099511628211ULL;hash^=v&255U;hash*=1099511628211ULL; }
                    const auto [lo,hi]=std::minmax_element(s.earth[c].begin(),s.earth[c].end());
                    std::cout<<','<<metop::avhrr_channel_name(s.channels[c])<<':'<<*lo<<':'<<*hi<<':'<<std::hex<<hash<<std::dec;
                }
                std::cout<<'\n';offset+=header.total_packet_bytes;++count;
            }
            require(count>0,"Empty capture");
            std::cout<<"scans="<<count<<'\n';
        } else throw std::runtime_error("Unknown test");
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
