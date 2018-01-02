// framework includes
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "fhiclcpp/ParameterSet.h"

// data product includes
#include "larcore/Geometry/Geometry.h"
#include "lardataobj/RecoBase/Wire.h"

// root includes
#include "TFile.h"
#include "TTree.h"

// c++ includes
#include <vector>
#include <iterator>
#include <typeinfo>
#include <memory>
#include <string>
#include <algorithm>
#include <cmath>
#include <map>

// local includes
#include "DataFormat/EventImage2D.h"
#include "DataFormat/EventROI.h"
#include "DataFormat/IOManager.h"

namespace nnbar {

class LArCVMaker : public art::EDAnalyzer {
  
public:

  explicit LArCVMaker(fhicl::ParameterSet const & pset);
  void analyze(art::Event const & evt);
  void beginJob();
  void endJob();

private:

  void ClearData();
  void ResetROI();
  void SetROISize();
  int FindBestAPA(std::vector<int> apas);
  int FindROI(int apa, int plane);

  larcv::IOManager fMgr;

  std::string fWireModuleLabel;
  int fMaxTick;
  int fADCCut;
  int fContainmentCut;
  int fEventType;

  int fFirstWire;
  int fLastWire;
  int fFirstTick;
  int fLastTick;

  const int fNumberChannels[3] = { 800, 800, 960 };
  const int fFirstChannel[3] = { 0, 800, 1600 };
  const int fLastChannel[3] = { 799, 1599, 2559 };

  int fEvent;
  int fAPA;
  int fNumberWires;
  int fNumberTicks;

  std::map<int,std::vector<float>> fWireMap;

}; // class LArCVMaker

LArCVMaker::LArCVMaker(fhicl::ParameterSet const & pset) :
    EDAnalyzer(pset),
    fMgr(larcv::IOManager::kWRITE),
    fWireModuleLabel(pset.get<std::string>("WireModuleLabel")),
    fMaxTick(pset.get<int>("MaxTick")),
    fADCCut(pset.get<int>("ADCCut")),
    fEventType(pset.get<int>("EventType"))
{} // function LArCVMaker::LArCVMaker

void LArCVMaker::beginJob() {

  std::string filename;
  if (std::getenv("PROCESS") != nullptr) filename = "larcv_" + std::string(std::getenv("PROCESS")) + ".root";
  else filename = "larcv.root";
  fMgr.set_out_file(filename);
  fMgr.initialize();
} // function LArCVMaker::beginJob

void LArCVMaker::endJob() {

  fMgr.finalize();
} // function LArCVMaker::endJob

void LArCVMaker::ClearData() {

  ResetROI();
  fAPA = -1;
  fWireMap.clear();
} // function LArCVMaker::ClearData

void LArCVMaker::ResetROI() {

  fFirstWire = -1;
  fLastWire = -1;
  fFirstTick = -1;
  fLastTick = -1;
  fNumberWires = -1;
  fNumberTicks = -1;
} // function LArCVMaker::ResetROI

void LArCVMaker::SetROISize() {

  fNumberWires = fLastWire - fFirstWire + 1;
  fNumberTicks = fLastTick - fFirstTick + 1;
}

int LArCVMaker::FindBestAPA(std::vector<int> apas) {

  int best_apa = -1;
  float best_adc = -1;

  for (int apa : apas) {

    float max_adc = 0;

    for (int it_plane = 0; it_plane < 3; ++it_plane) {
      for (int it_x = 0; it_x < fNumberChannels[it_plane]; ++it_x) {
        for (int it_y = 0; it_y < fMaxTick; ++it_y) {
          int wire_address = (apa * 2560) + fFirstChannel[it_plane] + it_x;
          int tick_address = it_y;
          if (fWireMap.find(wire_address) != fWireMap.end() && tick_address < (int)fWireMap[wire_address].size())
            max_adc += fWireMap[wire_address][tick_address];
        }
      }
    }

    if (max_adc > best_adc || best_apa == -1) {
      best_apa = apa;
      best_adc = max_adc;
    }
  }

  return best_apa;
} // function LArCVMaker::FindBestAPA

int LArCVMaker::FindROI(int apa, int plane) {

  ResetROI();

  // find first & last channels in APA
  int first_channel = (2560*apa) + fFirstChannel[plane];
  int last_channel = (2560*apa) + fLastChannel[plane];

  // find first & last channel & tick with ADC above threshold
  for (int channel = first_channel; channel < last_channel; ++channel) {
    if (fWireMap.find(channel) != fWireMap.end()) {
      for (int tick = 0; tick < (int)fWireMap[channel].size(); ++tick) {
        if (fWireMap[channel][tick] > fADCCut) {
          if (fFirstWire == -1 || fFirstWire > channel) fFirstWire = channel;
          if (fLastWire == -1 || fLastWire < channel) fLastWire = channel;
          if (fFirstTick == -1 || fFirstTick > tick) fFirstTick = tick;
          if (fLastTick == -1 || fLastTick < tick) fLastTick = tick;
        }
      }
    }
  }

  if (fFirstWire == -1 || fLastWire == -1 || fFirstTick == -1 || fLastTick == -1) return -1;
  SetROISize();

  // figure out whether we need to downsample
  int downsample = 1;
  if (fNumberWires > 600 || fNumberTicks/4 > 600) downsample = 2;

  // calculate exact required image size

  // add margin in wire dimension
  int margin = 10 * downsample;
  if (fFirstWire-margin < first_channel) fFirstWire = first_channel;
  else fFirstWire -= margin;
  if (fLastWire+margin > last_channel) fLastWire = last_channel;
  else fLastWire += margin;
  SetROISize();

  // make sure number of wires is even
  if (fNumberWires%downsample == 1 && fLastWire < last_channel) {
    ++fLastWire;
    ++fNumberWires;
  }
  else if (fNumberWires%downsample == 1 && fFirstWire > first_channel) {
    --fFirstWire;
    ++fNumberWires;
  }
  else if (fNumberWires%downsample == 1) {
    std::cout << "VERY WEIRD. Odd number of wires but somehow out of bounds???" << std::endl;
    std::cout << "There are " << fNumberWires << " wires. Exiting." << std::endl;
    exit(1);
  }
  SetROISize();

  // make sure number of ticks is good
  int first_tick = 2;
  int last_tick = 4489;
  if (downsample == 1) {
    first_tick = 0;
    last_tick = 4491;
  }
  int num_ticks = last_tick - first_tick;
  int ticks_to_add = 0;
  int order = 4 * downsample;
  margin = 40 * downsample;
  if (fNumberTicks%order != 0) ticks_to_add = order-(fNumberTicks%order);
  if (fNumberTicks+(2*margin)+ticks_to_add > num_ticks) {
    fFirstTick = first_tick;
    fLastTick = last_tick;
  }
  else {
    // deal with start of ROI
    if (fFirstTick-(margin+ticks_to_add) < first_tick) fFirstTick = first_tick;
    else fFirstTick -= margin + ticks_to_add;
    SetROISize();

    // now deal with end of ROI
    if (fNumberTicks%order != 0) ticks_to_add = order - (fNumberTicks%order);
    else ticks_to_add = 0;
    if (fLastTick+margin+ticks_to_add > last_tick) fLastTick = last_tick;
    else fLastTick += margin + ticks_to_add;
    SetROISize();

    // now mop up any residual
    if (fNumberTicks%order != 0) {
      ticks_to_add = order-(fNumberTicks%order);
      if (fFirstTick - ticks_to_add < first_tick) fFirstTick = first_tick;
      else fFirstTick -= ticks_to_add;
      SetROISize();
    }

    if (fNumberTicks%order != 0) {
      std::cout << "Number of ticks " << fNumberTicks << " is still not divisible by order " << order << ". I have no idea what's happening." << std::endl;
      exit(1);
    }
  }

  SetROISize();
  return downsample;
} // function LArCVMaker::FindROI

void LArCVMaker::analyze(art::Event const & evt) {

  ClearData();

  // get event number
  fEvent = evt.event();

  fMgr.set_id(evt.id().run(),evt.id().subRun(),evt.id().event());

  // get wire objects
  art::Handle<std::vector<recob::Wire>> wireh;
  evt.getByLabel(fWireModuleLabel,wireh);

  // initialize ROI finding variables
  std::vector<int> apas;

  // fill wire map
  for (std::vector<recob::Wire>::const_iterator it = wireh->begin();
      it != wireh->end(); ++it) {
    const recob::Wire & wire = *it;
    fWireMap.insert(std::pair<int,std::vector<float>>(wire.Channel(),std::vector<float>(wire.Signal())));
    int apa = std::floor(wire.Channel()/2560);
    if (std::find(apas.begin(),apas.end(),apa) == apas.end())
      apas.push_back(apa);
  }

  // find best APA
  if (apas.size() == 0) {
    std::cout << "Skipping event. No activity inside the TPC!" << std::endl;
    return;
  }
  int best_apa = FindBestAPA(apas);
  if (best_apa != -1) fAPA = best_apa;
  else {
    std::cout << "Skipping event. Could not find good APA!" << std::endl;
    return;
  }

  // check for problems
  for (int it_plane = 0; it_plane < 3; ++it_plane) {
    if (FindROI(best_apa,it_plane) == -1) {
      std::cout << "Skipping event. Could not find good ROI in APA!" << std::endl;
      return;
    }
  }

  // produce image
  auto images = (larcv::EventImage2D*)(fMgr.get_data(larcv::kProductImage2D, "tpc"));
  std::cout << std::endl;
  for (int it_plane = 0; it_plane < 3; ++it_plane) {
    int downsample = FindROI(best_apa,it_plane);
    std::cout << "PLANE " << it_plane << " IMAGE" << std::endl;
    std::cout << "Original image resolution " << fNumberWires << "x" << fNumberTicks;
    larcv::Image2D image_temp(fNumberWires,fNumberTicks);
    for (int it_channel = 0; it_channel < fNumberWires; ++it_channel) {
      int channel = it_channel + fFirstWire;
      for (int it_tick = 0; it_tick < fNumberTicks; ++it_tick) {
        int tick = it_tick + fFirstTick;
        if (fWireMap.find(channel) != fWireMap.end()) image_temp.set_pixel(it_channel,it_tick,fWireMap[channel][tick]);
      }
    }
    image_temp.compress(fNumberWires/downsample,fNumberTicks/(4*downsample));
    std::cout << " => downsampling to " << fNumberWires/downsample << "x" << fNumberTicks/(4*downsample) << "." << std::endl << std::endl;
    larcv::Image2D image(600,600);
    int image_width = fNumberWires / downsample;
    int image_height = fNumberTicks / (4 * downsample);
    for (int it_x = 0; it_x < 600; ++it_x) {
      for (int it_y = 0; it_y < 600; ++it_y) {
        if (it_x < image_width && it_y < image_height) image.set_pixel(it_x,it_y,image_temp.pixel(it_x,it_y));
        else image.set_pixel(it_x,it_y,0);
      }
    }
    images->Emplace(std::move(image));
  }

  auto roi = (larcv::EventROI*)(fMgr.get_data(larcv::kProductROI, "tpc"));
  roi->Emplace(larcv::ROI((larcv::ROIType_t)fEventType));

  fMgr.save_entry();
} // function LArCVMaker::analyze

DEFINE_ART_MODULE(LArCVMaker)

} // namespace nnbar

