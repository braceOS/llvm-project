add_llvm_component_library(LLVMBraceDesc
  BraceMCTargetDesc.cpp
  BraceS2ObjectWriter.cpp
  BraceS2Streamer.cpp

  LINK_COMPONENTS
  BraceInfo
  MC
  Support
  TargetParser

  ADD_TO_COMPONENT
  Brace
  )
