#include "image_writer.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace metop;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool threw=false; try { action(); } catch(const std::exception&) { threw=true; }
    require(threw,"Invalid input/output was accepted");
}
std::string read(const std::filesystem::path& p) {
    std::ifstream in(p,std::ios::binary); require(bool(in),"Missing output");
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
std::vector<std::uint16_t> pixels(const std::filesystem::path& p,std::size_t height) {
    const auto data=read(p);
    const auto header="P5\n2048 "+std::to_string(height)+"\n1023\n";
    require(data.starts_with(header),"PGM header/dimensions/Maxval");
    require(data.size()==header.size()+height*4096,"PGM output size");
    std::vector<std::uint16_t> result(height*2048);
    for(std::size_t i=0;i<result.size();++i)
        result[i]=static_cast<std::uint16_t>((static_cast<unsigned char>(data[header.size()+2*i])<<8)
            | static_cast<unsigned char>(data[header.size()+2*i+1]));
    return result;
}
AvhrrScan scan(std::uint16_t apid,std::uint16_t base) {
    AvhrrScan s; s.header.apid=apid;
    s.channels={AvhrrChannel::ch1,AvhrrChannel::ch2,apid==103 ? AvhrrChannel::ch3a : AvhrrChannel::ch3b,
        AvhrrChannel::ch4,AvhrrChannel::ch5};
    for(std::size_t c=0;c<5;++c)s.earth[c].fill(static_cast<std::uint16_t>(base+c));
    return s;
}
ReassembledPacket source(std::uint8_t scid=11,bool replay=false) {
    ReassembledPacket p;p.source.spacecraft_id=scid;p.source.vcid=9;p.source.replay=replay;
    p.source.counter=100;p.end_counter=114;return p;
}
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"Expected case and test root");
        const std::string test=argv[1];
        const std::string_view root_text=argv[2];
        auto root=std::filesystem::path(std::u8string(root_text.begin(),root_text.end()));
        if(test=="cli_check") {
            const auto dir=root/"avhrr"/"scid_11_vcid_9_replay_0";
            const std::array names{"ch1_raw.pgm","ch2_raw.pgm","ch3a_raw.pgm","ch3b_raw.pgm","ch4_raw.pgm","ch5_raw.pgm"};
            for(std::size_t c=0;c<6;++c) {
                const auto data=pixels(dir/names[c],c==2 || c==3 ? 1 : 2);
                const auto slot=c>=3 ? c-1 : c;
                for(std::size_t i=0;i<data.size();++i) {
                    const auto word=55+(i%2048)*5+slot;
                    require(data[i]==(word*37+word/7)%1024,"CLI changed raw decoded counts");
                }
            }
            return 0;
        }
        root/=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        if(test=="header") {
            std::array<std::uint16_t,2048> first{},second{};
            for(std::size_t i=0;i<2048;++i) { first[i]=static_cast<std::uint16_t>(i%1024);second[i]=static_cast<std::uint16_t>(1023-i%1024); }
            RawPgmWriter writer(root/"raw.pgm");writer.append(first);writer.append(second);writer.finish();writer.finish();
            const auto data=pixels(root/"raw.pgm",2);
            require(std::equal(first.begin(),first.end(),data.begin()) &&
                std::equal(second.begin(),second.end(),data.begin()+2048),"Raw values/endian/row order changed");
            require(!std::filesystem::exists(root/"raw.pgm.raster.tmp"),"Raster spool retained after success");
            rejects([&]{writer.append(first);});
        } else if(test=="invalid") {
            RawPgmWriter writer(root/"raw.pgm");
            for(const auto size:{0U,2047U,2049U})rejects([&]{writer.append(std::vector<std::uint16_t>(size));});
            std::array<std::uint16_t,2048> good{};good.fill(512);writer.append(good);
            for(const auto value:{1024U,65535U}) {
                auto bad=good;bad.back()=static_cast<std::uint16_t>(value);
                rejects([&]{writer.append(bad);});
                require(writer.height()==1,"Rejected line changed height");
            }
            writer.finish();const auto data=pixels(root/"raw.pgm",1);
            require(std::all_of(data.begin(),data.end(),[](auto v){return v==512;}),"Rejected line changed raster");
        } else if(test=="empty") {
            RawPgmWriter writer(root/"empty.pgm");writer.finish();
            require(!std::filesystem::exists(root/"empty.pgm"),"Invalid zero-height PGM created");
            AvhrrImageWriter collection(root/"avhrr",true);collection.finish();
            require(std::filesystem::is_empty(root/"avhrr") && collection.statistics().files==0,"Empty channel files created");
        } else if(test=="modes") {
            AvhrrImageWriter writer(root/"avhrr",true);
            auto p=source();writer.add(p,scan(103,10));p.source.counter=200;writer.add(p,scan(104,100));
            p.source.counter=300;writer.add(p,scan(103,200));writer.finish();writer.finish();
            const auto dir=root/"avhrr"/"scid_11_vcid_9_replay_0";
            const auto a=pixels(dir/"ch3a_raw.pgm",2),b=pixels(dir/"ch3b_raw.pgm",1),c=pixels(dir/"ch1_raw.pgm",3);
            require(a.front()==12 && a.back()==202 && b.front()==102 && c[2048]==100,"Channel modes mixed/padded");
            const auto rows=read(dir/"scan_rows.csv");
            require(rows.find(",0,0,0,,0,0\n")!=std::string::npos &&
                rows.find(",1,1,,0,1,1\n")!=std::string::npos &&
                rows.find(",2,2,1,,2,2\n")!=std::string::npos,"Channel row mapping incorrect");
            require(writer.statistics().files==6 && writer.statistics().scans==3 &&
                writer.statistics().channel_rows==std::array<std::uint64_t,6>{3,3,2,1,3,3},"Image statistics");
            const auto meta=read(dir/"metadata.json");
            require(meta.find("\"scan_count\": 3")!=std::string::npos &&
                meta.find("\"gap_padding\": false")!=std::string::npos,"Missing geometry/provenance");
        } else if(test=="streams") {
            AvhrrImageWriter writer(root/"avhrr",false);
            writer.add(source(),scan(103,10));writer.add(source(12),scan(103,100));
            writer.add(source(11,true),scan(104,200));writer.finish();
            const auto dir=root/"avhrr";
            require(pixels(dir/"scid_11_vcid_9_replay_0"/"ch1_raw.pgm",1).front()==10 &&
                pixels(dir/"scid_12_vcid_9_replay_0"/"ch1_raw.pgm",1).front()==100 &&
                pixels(dir/"scid_11_vcid_9_replay_1"/"ch1_raw.pgm",1).front()==200,"Stream identities mixed");
            require(!std::filesystem::exists(dir/"scid_11_vcid_9_replay_0"/"ch3b_raw.pgm"),"Absent channel synthesized");
            require(writer.statistics().streams==3 && writer.statistics().files==15,"Stream/file counters");
            require(read(dir/"scid_11_vcid_9_replay_0"/"metadata.json").find("\"rs_applied\": false")!=std::string::npos,"Bypass provenance lost");
        } else if(test=="invalid_scan") {
            AvhrrImageWriter writer(root/"avhrr",true);
            auto s=scan(103,10);s.earth.back().back()=1024;rejects([&]{writer.add(source(),s);});
            s=scan(103,10);s.channels.back()=AvhrrChannel::ch1;rejects([&]{writer.add(source(),s);});
            s=scan(104,10);s.channels[2]=AvhrrChannel::ch3a;rejects([&]{writer.add(source(),s);});
            require(std::filesystem::is_empty(root/"avhrr"),"Invalid scan partially written");
            writer.add(source(),scan(103,10));writer.finish();
            require(writer.statistics().scans==1,"Invalid scan counted");
        } else if(test=="failures") {
            std::ofstream(root/"existing.pgm")<<"sentinel";
            rejects([&]{RawPgmWriter writer(root/"existing.pgm");});
            require(read(root/"existing.pgm")=="sentinel","Existing file overwritten");
            rejects([&]{AvhrrImageWriter writer(root,true);});
            std::array<std::uint16_t,2048> line{};
            {
                RawPgmWriter writer(root/"blocked.pgm");writer.append(line);
                std::filesystem::create_directory(root/"blocked.pgm");
                rejects([&]{writer.finish();});rejects([&]{writer.append(line);});
            }
            require(std::filesystem::is_directory(root/"blocked.pgm"),"Foreign output removed");
            require(!std::filesystem::exists(root/"blocked.pgm.raster.tmp"),"Failed writer leaked spool");
            RawPgmWriter missing(root/"missing"/"raw.pgm");rejects([&]{missing.append(line);});
        } else throw std::runtime_error("Unknown case");
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
