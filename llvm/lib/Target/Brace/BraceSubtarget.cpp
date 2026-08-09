//===-- BraceSubtarget.cpp - Brace subtarget information -----------------===//

#include "BraceSubtarget.h"
#include "BraceTargetMachine.h"

#define DEBUG_TYPE "brace-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "BraceGenSubtargetInfo.inc"

using namespace llvm;

BraceSubtarget &BraceSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                                StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  ParseSubtargetFeatures(CPU, CPU, FS);
  return *this;
}

BraceSubtarget::BraceSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                               const BraceTargetMachine &TM)
    : BraceGenSubtargetInfo(TT, CPU, CPU, FS), InstrInfo(*this),
      FrameLowering(), TLInfo(TM, *this) {
  initializeSubtargetDependencies(CPU, FS);
}
