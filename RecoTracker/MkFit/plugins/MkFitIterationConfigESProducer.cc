#include "FWCore/Framework/interface/ModuleFactory.h"
#include "FWCore/Framework/interface/ESProducer.h"

#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "RecoTracker/MkFit/interface/MkFitGeometry.h"

// mkFit includes
#include "RecoTracker/MkFitCore/interface/IterationConfig.h"

class MkFitIterationConfigESProducer : public edm::ESProducer {
public:
  MkFitIterationConfigESProducer(const edm::ParameterSet &iConfig);

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions);

  std::unique_ptr<mkfit::IterationConfig> produce(const TrackerRecoGeometryRecord &iRecord);

private:
  const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> geomToken_;
  const std::string configFile_;
  const float minPtCut_;
  const unsigned int maxClusterSize_;
  const float backwardFitOutlierChi2_;
  const int backwardFitMaxOutliers_;
  const float backwardFitOutlierMinPt_;
  const float backwardSearchMaxD0_;
};

MkFitIterationConfigESProducer::MkFitIterationConfigESProducer(const edm::ParameterSet &iConfig)
    : geomToken_{setWhatProduced(this, iConfig.getParameter<std::string>("ComponentName")).consumes()},
      configFile_{iConfig.getParameter<edm::FileInPath>("config").fullPath()},
      minPtCut_{(float)iConfig.getParameter<double>("minPt")},
      maxClusterSize_{iConfig.getParameter<unsigned int>("maxClusterSize")},
      backwardFitOutlierChi2_{(float)iConfig.getParameter<double>("backwardFitOutlierChi2")},
      backwardFitMaxOutliers_{iConfig.getParameter<int>("backwardFitMaxOutliers")},
      backwardFitOutlierMinPt_{(float)iConfig.getParameter<double>("backwardFitOutlierMinPt")},
      backwardSearchMaxD0_{(float)iConfig.getParameter<double>("backwardSearchMaxD0")} {}

void MkFitIterationConfigESProducer::fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<std::string>("ComponentName", "")->setComment("Product label");
  desc.add<edm::FileInPath>("config", edm::FileInPath())
      ->setComment("Path to the JSON file for the mkFit configuration parameters");
  desc.add<double>("minPt", 0.0)->setComment("min pT cut applied during track building");
  desc.add<unsigned int>("maxClusterSize", 8)->setComment("Max cluster size of SiStrip hits");
  desc.add<double>("backwardFitOutlierChi2", 0.0)
      ->setComment("If > 0, hits with a larger chi2 increment in the backward fit are dropped as outliers");
  desc.add<int>("backwardFitMaxOutliers", 3)
      ->setComment("Max number of outliers dropped per track in the backward fit");
  desc.add<double>("backwardFitOutlierMinPt", 0.0)
      ->setComment("Outliers are dropped only on tracks with pT above this");
  desc.add<double>("backwardSearchMaxD0", 0.0)
      ->setComment("If > 0, the backward search runs only on candidates with |d0| to the beam spot below this (cm)");
  descriptions.addWithDefaultLabel(desc);
}

std::unique_ptr<mkfit::IterationConfig> MkFitIterationConfigESProducer::produce(
    const TrackerRecoGeometryRecord &iRecord) {
  mkfit::ConfigJson cj;
  auto it_conf = cj.load_File(configFile_);
  it_conf->m_params.minPtCut = minPtCut_;
  it_conf->m_backward_params.minPtCut = minPtCut_;
  it_conf->m_params.maxClusterSize = maxClusterSize_;
  it_conf->m_backward_params.maxClusterSize = maxClusterSize_;
  it_conf->m_backward_fit_outlier_chi2 = backwardFitOutlierChi2_;
  it_conf->m_backward_fit_max_outliers = backwardFitMaxOutliers_;
  it_conf->m_backward_fit_outlier_min_pt = backwardFitOutlierMinPt_;
  it_conf->m_backward_search_max_d0 = backwardSearchMaxD0_;
  it_conf->setupStandardFunctionsFromNames();
  return it_conf;
}

DEFINE_FWK_EVENTSETUP_MODULE(MkFitIterationConfigESProducer);
