add_llvm_component_library(LLVMBraceInfo
  BraceTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support
  TargetParser

  ADD_TO_COMPONENT
  Brace
  )
