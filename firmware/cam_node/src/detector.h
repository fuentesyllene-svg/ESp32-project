#pragma once
#include <Arduino.h>
#include "config.h"

enum BlobClass : uint8_t {
  CLASS_UNKNOWN = 0,
  CLASS_CATERPILLAR,
  CLASS_APHID,
  CLASS_NONTARGET
};

// One connected foreground region, with the features the classifier uses.
struct Blob {
  uint16_t area;
  float    cx, cy;
  uint8_t  x0, y0, x1, y1;
  float    elongation;      // sqrt(major/minor) from second-order moments
  float    fill;            // area / bbox area: solidity
  int16_t  contrast;        // mean(foreground) - mean(background)
  float    speed;           // grid px per frame, from frame-to-frame tracking
  uint8_t  age;             // consecutive frames this blob has been tracked
  uint8_t  cls;
  uint8_t  confidence;      // 0-100
  uint8_t  id;
};

struct DetectionResult {
  uint16_t caterpillars;
  uint16_t aphids;
  uint16_t nontarget;
  uint8_t  best_confidence;
  uint32_t motion_px;
  uint8_t  blob_count;
  bool     learning;        // background model not yet stable
  bool     scene_reset;     // a global lighting change forced a relearn
};

namespace detector {
// Optional plug-in point for a trained classifier (for example an Edge Impulse
// export). When set, it is consulted for every blob that passes the size gate,
// and its verdict overrides the geometric heuristic. Return CLASS_UNKNOWN to
// fall through to the heuristic.
using ExternalClassifier = uint8_t (*)(const Blob& b, uint8_t* confidence_out);

bool begin();
void setExternalClassifier(ExternalClassifier fn);

// Sensitivity 0-100 from the controller; 50 is nominal.
void setSensitivity(uint8_t pct);

// Discards the background model. Called when the UV array switches, because
// the illumination change rewrites every pixel in the scene.
void resetBackground();

// Runs one frame: grabs, downsamples, segments, tracks, classifies.
DetectionResult process();

const Blob* blobs(uint8_t* count);
uint32_t framesProcessed();
uint32_t frameErrors();
}  // namespace detector
