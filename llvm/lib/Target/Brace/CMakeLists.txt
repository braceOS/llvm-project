add_llvm_component_group(Brace)

set(LLVM_TARGET_DEFINITIONS Brace.td)
tablegen(LLVM BraceGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM BraceGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM BraceGenRegisterInfo.inc -gen-register-info)
add_public_tablegen_target(BraceCommonTableGen)

add_llvm_target(BraceCodeGen
  BraceS2Lowering.cpp
  BraceTargetMachine.cpp

  LINK_COMPONENTS
  BraceDesc
  BraceInfo
  CodeGen
  Core
  MC
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  Brace
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
