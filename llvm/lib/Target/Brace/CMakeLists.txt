add_llvm_component_group(Brace)

set(LLVM_TARGET_DEFINITIONS Brace.td)
tablegen(LLVM BraceGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM BraceGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM BraceGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM BraceGenDAGISel.inc -gen-dag-isel)
add_public_tablegen_target(BraceCommonTableGen)

add_llvm_target(BraceCodeGen
  BraceAsmPrinter.cpp
  BraceFinalizeBranches.cpp
  BraceFinalizeSpillHomes.cpp
  BraceFrameLowering.cpp
  BraceInstrInfo.cpp
  BraceISelDAGToDAG.cpp
  BraceISelLowering.cpp
  BraceRegisterInfo.cpp
  BraceS2Lowering.cpp
  BraceS3IRVerifier.cpp
  BraceSubtarget.cpp
  BraceTargetMachine.cpp

  LINK_COMPONENTS
  AsmPrinter
  BraceDesc
  BraceInfo
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  Brace
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
