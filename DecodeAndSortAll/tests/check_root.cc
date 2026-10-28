// Independent checker; --stream uses constant memory and aggregate checksums.
// Does not link R5560Decode.cc or Sort.hh. Usage: check_root FILE.root RUN_DIRECTORY [--stream] [--par FILE]
#include <TFile.h>
#include <TTree.h>
#include <TLeaf.h>
#include <TObjString.h>
#include <TH1.h>
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>
namespace fs=std::filesystem;
using json=nlohmann::json;
struct Expected {
    uint32_t mod,ch,daq,run,a,b,s1,g,s2;
    uint64_t t,previous,record;
    int64_t ns;
    double energy,baseline;
};
static uint32_t read32(std::istream &s) {
    unsigned char b[4];
    if(!s.read(reinterpret_cast<char*>(b),4)) throw std::runtime_error("short reference input");
    return uint32_t(b[0])+(uint32_t(b[1])<<8)+(uint32_t(b[2])<<16)+(uint32_t(b[3])<<24);
}
static void require(bool ok,const std::string &what) {if(!ok) throw std::runtime_error(what);}
static bool near(double a,double b) {return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=1e-7*std::max(1.0,std::max(std::abs(a),std::abs(b)));}
static uint64_t hash(const Expected &e) {
    uint64_t h=1469598103934665603ULL;
    for(uint64_t v:{uint64_t(e.mod),uint64_t(e.ch),uint64_t(e.daq),uint64_t(e.run),uint64_t(e.a),uint64_t(e.b),uint64_t(e.s1),uint64_t(e.g),uint64_t(e.s2),e.t,e.previous,e.record,uint64_t(e.ns)}) {
        for(unsigned b=0;b<8;++b){h^=(v>>(8*b))&255;h*=1099511628211ULL;}
    }
    return h;
}
int main(int argc,char **argv) {
    try {
        require(argc>=3,"Usage: check_root FILE.root RUN_DIRECTORY [--stream] [--par FILE]");
        bool streaming=false;
        struct Rule {int64_t offset; double low,high;};
        std::map<std::pair<uint32_t,uint32_t>,Rule> rules;
        for(int arg=3;arg<argc;++arg){
            const std::string flag=argv[arg];
            if(flag=="--stream"){streaming=true;continue;}
            require(flag=="--par"&&arg+1<argc,"unknown checker option");
            std::ifstream par(argv[++arg]);require(bool(par),"reference parameter table");
            std::string line;
            while(std::getline(par,line)){
                line=line.substr(0,line.find('#')); std::istringstream row(line);
                std::string first; if(!(row>>first)||first=="mod")continue;
                uint32_t m=static_cast<uint32_t>(std::stoul(first)),c;Rule r{};
                require(bool(row>>c>>r.offset>>r.low>>r.high),"reference parameter line");
                rules[{m,c}]=r;
            }
        }
        uint64_t expectedCount=0,expectedSum=0,expectedXor=0,actualSum=0,actualXor=0;
        uint64_t decodedCount=0,filteredCount=0;
        long double expectedEnergy=0,actualEnergy=0,expectedBaseline=0,actualBaseline=0;
        std::vector<fs::path> paths;
        for(const auto &p:fs::directory_iterator(argv[2])) if(p.is_regular_file()&&p.path().extension()==".bin") paths.push_back(p.path());
        std::sort(paths.begin(),paths.end()); require(!paths.empty(),"no reference binary files");
        std::vector<Expected> expected;
        std::map<std::pair<uint32_t,uint32_t>,uint64_t> counts;
        std::set<uint32_t> modules;
        for(const auto &path:paths) {
            std::ifstream s(path,std::ios::binary); char magic[8];
            s.read(magic,8); require(s&&std::memcmp(magic,"R5560D1\n",8)==0,"reference magic");
            const auto length=read32(s); require(length<16*1024*1024,"reference header length");
            std::string text(length,'\0'); s.read(&text[0],length); require(bool(s),"reference JSON read");
            auto h=json::parse(text); modules.insert(h.at("module_index").get<uint32_t>());
            std::array<double,32> vdc{};
            uint64_t record=0;
            while(s.peek()!=std::char_traits<char>::eof()) {
                uint32_t w[8]; for(auto &x:w)x=read32(s);
                Expected e{};
                e.mod=h.at("module_index");e.daq=h.at("daq_id");e.run=h.at("run_id");
                e.ch=(w[2]>>27)&31; e.a=w[2]&0x00ffffff; e.b=w[3];e.s1=w[4];e.g=w[5];e.s2=w[6];
                e.t=w[0]+(uint64_t(w[1]&65535)<<32);e.previous=w[7]+(uint64_t(w[1]>>16)<<32);e.record=record++;
                const auto &p=h.at("settings").at("channels").at(e.ch);
                const double bl=p.at("BLLEN"),sl=p.at("XIASL"),gap=p.at("XIASG"),tau=p.at("TAU");
                // Independent algebraic form of the UserDPP exponential coefficients.
                const double decay=std::exp(-8.0/tau),decaySl=std::pow(decay,sl),decayBl=std::pow(decay,bl);
                const double a1=(1-decay)/(1-decaySl),a0=-a1*decaySl,ag=1-decay;
                const double abl=(1-decayBl)/(decayBl*(1-decaySl)),abl2=1/decayBl;
                const uint64_t dt=(e.t-e.previous)&0xffffffffffffULL;
                double estimate=vdc[e.ch];
                if(dt>2*bl+sl+gap) estimate=(e.a-abl2*e.b)/(bl-abl2*bl);
                else if(dt>bl+sl+gap) estimate=(e.b-abl*e.s1)/(bl-abl*sl);
                if(std::isfinite(estimate))vdc[e.ch]=estimate;
                e.baseline=vdc[e.ch];
                e.energy=a0*e.s1+ag*e.g+a1*e.s2-(a0*e.baseline*sl+ag*e.baseline*gap+a1*e.baseline*sl);
                ++decodedCount;e.ns=static_cast<int64_t>(e.t*8);
                if(rules.count({e.mod,e.ch})){
                    const auto &r=rules.at({e.mod,e.ch});
                    if(e.energy<r.low||e.energy>r.high){++filteredCount;continue;}
                    e.ns+=r.offset;
                }
                if(!streaming)expected.push_back(e);
                ++expectedCount;const auto hv=hash(e);expectedSum+=hv;expectedXor^=hv;
                expectedEnergy+=e.energy;expectedBaseline+=e.baseline;
                ++counts[{e.mod,e.ch}];
            }
        }
        TFile f(argv[1],"READ");require(!f.IsZombie(),"ROOT open");
        auto *tree=dynamic_cast<TTree*>(f.Get("tree"));require(tree!=nullptr,"tree exists");
        require(tree->GetEntries()==static_cast<Long64_t>(expectedCount),"tree entries");
        auto *meta=dynamic_cast<TObjString*>(f.Get("r5560_metadata"));require(meta!=nullptr,"metadata exists");
        auto j=json::parse(meta->GetString().Data());
        require(j.at("timestamp_unit")=="8 ns ticks"&&j.at("ts_unit")=="ns","timestamp units metadata");
        require(j.at("sources").size()==paths.size(),"source count metadata");
        require(std::stoull(j.at("events").get<std::string>())==expectedCount,"event count metadata");
        if(j.contains("decoded_events"))require(std::stoull(j.at("decoded_events").get<std::string>())==decodedCount,"decoded count metadata");
        if(j.contains("filtered_events"))require(std::stoull(j.at("filtered_events").get<std::string>())==filteredCount,"filtered count metadata");
        if(j.at("sorted").get<bool>())
            std::sort(expected.begin(),expected.end(),[](const Expected&a,const Expected&b){return std::tie(a.ns,a.mod,a.record)<std::tie(b.ns,b.mod,b.record);});
        UShort_t mod,ch,sr,fw,energyxia;
        UInt_t daq,run,a,b,s1,g,s2;
        ULong64_t t,previous,record;
        Long64_t ns;
        Double_t energy,baseline;
        auto bind=[&](const char *name,void *address,const char *type){
            require(tree->GetLeaf(name)!=nullptr&&std::string(tree->GetLeaf(name)->GetTypeName())==type,std::string("branch type: ")+name);
            require(tree->SetBranchAddress(name,address)>=0,std::string("bind: ")+name);
        };
        bind("mod",&mod,"UShort_t");bind("ch",&ch,"UShort_t");bind("daq_id",&daq,"UInt_t");bind("run",&run,"UInt_t");
        bind("sr",&sr,"UShort_t");bind("fw",&fw,"UShort_t");bind("energyxia",&energyxia,"UShort_t");
        bind("timestamp",&t,"ULong64_t");bind("previous",&previous,"ULong64_t");bind("record",&record,"ULong64_t");bind("ts",&ns,"Long64_t");
        bind("baselineA",&a,"UInt_t");bind("baselineB",&b,"UInt_t");bind("sl1",&s1,"UInt_t");bind("sg",&g,"UInt_t");bind("sl2",&s2,"UInt_t");
        bind("energy",&energy,"Double_t");bind("baseline",&baseline,"Double_t");
        std::tuple<Long64_t,UShort_t,ULong64_t> last{};
        for(uint64_t i=0;i<expectedCount;++i){
            require(tree->GetEntry(i)>0,"read ROOT entry");
            const auto key=std::make_tuple(ns,mod,record);
            if(j.at("sorted").get<bool>()&&i)require(key>=last,"ROOT global sort order");last=key;
            const auto rule=rules.find({mod,ch});
            const auto offset=rule==rules.end()?0:rule->second.offset;
            require(ns==static_cast<Long64_t>(8*t)+offset&&sr==125&&fw==4,"ROOT timestamp units/offset/sr/fw");
            require(energyxia==(energy<0?0:energy>=65536?65535:static_cast<UShort_t>(energy)),"energyxia clipping/truncation");
            if(!streaming){const auto &e=expected[i];
                require(mod==e.mod&&ch==e.ch&&daq==e.daq&&run==e.run&&t==e.t&&previous==e.previous&&record==e.record&&ns==e.ns&&a==e.a&&b==e.b&&s1==e.s1&&g==e.g&&s2==e.s2,"raw field/sort mismatch at ROOT entry "+std::to_string(i));
                require(near(energy,e.energy)&&near(baseline,e.baseline),"energy/baseline mismatch at ROOT entry "+std::to_string(i));
            }
            Expected e{};e.mod=mod;e.ch=ch;e.daq=daq;e.run=run;e.t=t;e.previous=previous;e.record=record;e.a=a;e.b=b;e.s1=s1;e.g=g;e.s2=s2;e.ns=ns;
            const auto hv=hash(e);actualSum+=hv;actualXor^=hv;actualEnergy+=energy;actualBaseline+=baseline;
        }
        require(expectedSum==actualSum&&expectedXor==actualXor,"raw-record checksum sum/xor");
        require(near(static_cast<double>(expectedEnergy),static_cast<double>(actualEnergy))&&near(static_cast<double>(expectedBaseline),static_cast<double>(actualBaseline)),"independent aggregate energy/baseline");
        for(auto m:modules) for(uint32_t c=0;c<32;++c){
            std::string name="energy_M"+std::to_string(m)+"_C"+std::to_string(c);
            auto *hist=dynamic_cast<TH1*>(f.Get(name.c_str()));require(hist!=nullptr,"histogram missing "+name);
            require(hist->GetEntries()==static_cast<double>(counts[{m,c}]),"histogram entries "+name);
            require(hist->Integral(0,hist->GetNbinsX()+1)==hist->GetEntries(),"histogram bins "+name);
        }
        std::cout<<"PASS: "<<expectedCount<<" entries (decoded="<<decodedCount<<", filtered="<<filteredCount<<"); "<<(streaming?"streaming checksums":"all raw fields")<<", "<<(j.at("sorted").get<bool>()?"global sorted order":"file input order")<<", timestamp units/offsets, independent energy/baseline, metadata and 32 histograms per module\n";
        return 0;
    } catch(const std::exception&e){std::cerr<<"check_root: "<<e.what()<<'\n';return 1;}
}
