#ifndef PROTOTYPE_NTUPLEREADER_H
#define PROTOTYPE_NTUPLEREADER_H

// Reads the LST --allobj ntuple and the original tracking ntuple, aligned per event.
//
// Alignment: LST-ntuple entries are written in stream-completion order, NOT input order.
// The (run, lumi, evt) branches carry the tracking ntuple's own event identity; on
// construction this reader scans the tracking sample once, building
// (run, lumi, event) -> (file index, entry) so loadEntry() can fetch the matching truth.
//
// Tracking sample path: either a single .root file or a directory (all *.root globbed,
// same rule as the standalone looper). The tree is at "trackingNtuple/tree".

#include <memory>
#include <string>

#include "EventData.h"

class NtupleReaderImpl;

class NtupleReader {
public:
  // lstNtuplePath: the LST ntuple (TTree "tree").
  // trkSamplePath: tracking ntuple file or directory.
  NtupleReader(const std::string& lstNtuplePath, const std::string& trkSamplePath);
  ~NtupleReader();

  long long nEntries() const;

  // Loads LST entry i into ev and the aligned tracking event into trk.
  // Returns false if the tracking event cannot be found (should not happen).
  bool loadEntry(long long i, LSTEventData& ev, TrkEventData& trk);

  // From the module_* branches (identical across entries; cached from entry 0).
  const ModuleTable& moduleTable() const;

private:
  std::unique_ptr<NtupleReaderImpl> impl_;
};

#endif
