void LSTEvent::probeQuintuplets() {
  alpaka::wait(queue_);
  constexpr unsigned int cap = 1u << 19;
  const unsigned int maskSize = probeMask_->size();
  auto mask_h = cms::alpakatools::make_host_buffer<uint8_t[]>(queue_, maskSize + 1u);
  std::copy(probeMask_->begin(), probeMask_->end(), mask_h.data());
  auto mask_d = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, maskSize + 1u);
  alpaka::memcpy(queue_, mask_d, mask_h);
  auto rec_d = cms::alpakatools::make_device_buffer<T5ProbeRecord[]>(queue_, cap);
  auto n_d = cms::alpakatools::make_device_buffer<unsigned int>(queue_);
  alpaka::memset(queue_, n_d, 0u);

  alpaka::exec<Acc1D>(queue_,
                      cms::alpakatools::make_workdiv<Acc1D>(1, 1024),
                      ProbeQuintuplets{},
                      modules_.const_view().modules(),
                      lstInputDC_->const_view().hits(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      tripletsDC_->const_view().tripletsOccupancy(),
                      quintupletsDC_->const_view().quintuplets(),
                      quintupletsDC_->const_view().quintupletsOccupancy(),
                      rangesDC_->const_view(),
                      ptCut_,
                      mask_d.data(),
                      maskSize,
                      rec_d.data(),
                      cap,
                      n_d.data());

  auto n_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);
  alpaka::memcpy(queue_, n_h, n_d);
  auto rec_h = cms::alpakatools::make_host_buffer<T5ProbeRecord[]>(queue_, cap);
  alpaka::memcpy(queue_, rec_h, rec_d);
  alpaka::wait(queue_);
  rungProbeWrite("t5_probe.bin", probeEntry_, rec_h.data(), *n_h.data(), cap);
}

void LSTEvent::probeQuadruplets() {
  alpaka::wait(queue_);
  constexpr unsigned int cap = 1u << 19;
  const unsigned int maskSize = probeMask_->size();
  auto mask_h = cms::alpakatools::make_host_buffer<uint8_t[]>(queue_, maskSize + 1u);
  std::copy(probeMask_->begin(), probeMask_->end(), mask_h.data());
  auto mask_d = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, maskSize + 1u);
  alpaka::memcpy(queue_, mask_d, mask_h);
  auto rec_d = cms::alpakatools::make_device_buffer<T4ProbeRecord[]>(queue_, cap);
  auto n_d = cms::alpakatools::make_device_buffer<unsigned int>(queue_);
  alpaka::memset(queue_, n_d, 0u);

  alpaka::exec<Acc1D>(queue_,
                      cms::alpakatools::make_workdiv<Acc1D>(1, 1024),
                      ProbeQuadruplets{},
                      modules_.const_view().modules(),
                      lstInputDC_->const_view().hits(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      tripletsDC_->const_view().tripletsOccupancy(),
                      quadrupletsDC_->const_view().quadruplets(),
                      quadrupletsDC_->const_view().quadrupletsOccupancy(),
                      rangesDC_->const_view(),
                      ptCut_,
                      mask_d.data(),
                      maskSize,
                      rec_d.data(),
                      cap,
                      n_d.data());

  auto n_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);
  alpaka::memcpy(queue_, n_h, n_d);
  auto rec_h = cms::alpakatools::make_host_buffer<T4ProbeRecord[]>(queue_, cap);
  alpaka::memcpy(queue_, rec_h, rec_d);
  alpaka::wait(queue_);
  rungProbeWrite("t4_probe.bin", probeEntry_, rec_h.data(), *n_h.data(), cap);
}
