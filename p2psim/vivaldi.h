#ifndef __VIVALDI_H
#define __VIVALDI_H

#include "p2psim.h"
#include <pair.h>

// Compute Vivaldi synthetic coordinates.
// Protocol-independent: doesn't care where the measurements
// come from.
// Indexed by IPAddress.
// Anyone can create a Vivaldi, call got_sample() after each
// RPC, and then call my_location().

class Vivaldi {
 public:
  Vivaldi();
  virtual ~Vivaldi();

  // Same as Euclidean::Coord
  // But unsigned may be bad for us...
  typedef pair<unsigned,unsigned> Coord;

  void got_sample(IPAddress who, Coord c, unsigned latency);
  Coord my_location();

 private:
  double _x;
  double _y;
};

#endif