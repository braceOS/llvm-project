include_directories(
  ${LLVM_SOURCE_DIR}/lib/Target/Brace
  ${LLVM_BINARY_DIR}/lib/Target/Brace
  )

set(LLVM_LINK_COMPONENTS
  BraceDesc
  BraceInfo
  MC
  Support
  )

add_llvm_target_unittest(BraceTests
  BraceS2ObjectWriterTest.cpp
  )
