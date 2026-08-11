//===- BraceS2ObjectWriterTest.cpp - Brace S2 writer tests ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>

using namespace llvm;
using namespace llvm::Brace;

namespace {

MCInst inst(unsigned Opcode, std::initializer_list<uint64_t> Operands) {
  MCInst Result;
  Result.setOpcode(Opcode);
  for (uint64_t Operand : Operands)
    Result.addOperand(MCOperand::createImm(static_cast<int64_t>(Operand)));
  return Result;
}

std::array<S2DirectFunction, 2> canonicalH1() {
  std::array<S2DirectFunction, 2> Functions;
  S2DirectFunction &Root = Functions[0];
  Root.Present = true;
  Root.RegisterTypes.assign({PADDR, PADDR, I8, I8, I32, I32, I32});
  Root.BlockStarts.push_back(0);
  Root.Instructions = {
      inst(S2_PHYSICAL_ADDRESS, {0, UINT64_C(0x80000000)}),
      inst(S2_PHYSICAL_LOAD, {4, U32, 0}),
      inst(S2_INTEGER_AND, {6, 4, 4}),
      inst(S2_DIRECT_CALL, {1, 4, 4}),
      inst(S2_INTEGER_AND, {5, 6, 6}),
      inst(S2_INTEGER_AND, {4, 4, 5}),
      inst(S2_PHYSICAL_ADDRESS, {0, UINT64_C(0x80000004)}),
      inst(S2_PHYSICAL_STORE, {U32, 0, 4}),
      inst(S2_RETURN, {}),
  };

  S2DirectFunction &Leaf = Functions[1];
  Leaf.Present = true;
  Leaf.RegisterTypes.assign({PADDR, PADDR, I8, I8, I32, I32});
  Leaf.ParameterSlots.push_back(4);
  Leaf.BlockStarts.push_back(0);
  Leaf.ResultKind = 2;
  Leaf.Instructions = {inst(S2_RETURN_VALUE, {4})};
  return Functions;
}

Error write(std::array<S2DirectFunction, 2> &Functions,
            SmallVectorImpl<char> &Bytes) {
  raw_svector_ostream Out(Bytes);
  S2ObjectWriter Writer(Out, S2ObjectMode::DirectCallHome);
  return Writer.writeDirectCallExact(Functions, /*EntryFunction=*/0,
                                     S2RelocationBase);
}

Error writeMode(std::array<S2DirectFunction, 2> &Functions,
                SmallVectorImpl<char> &Bytes, S2ObjectMode Mode) {
  raw_svector_ostream Out(Bytes);
  S2ObjectWriter Writer(Out, Mode);
  return Writer.writeDirectCallExact(Functions, /*EntryFunction=*/0,
                                     S2RelocationBase);
}

void expectReject(std::array<S2DirectFunction, 2> Functions,
                  StringRef Diagnostic) {
  SmallVector<char, 2048> Bytes;
  Error E = write(Functions, Bytes);
  ASSERT_TRUE(static_cast<bool>(E));
  EXPECT_NE(toString(std::move(E)).find(Diagnostic), std::string::npos);
  EXPECT_TRUE(Bytes.empty());
}

std::array<S2DirectFunction, 2> canonicalByteFrameBF1() {
  std::array<S2DirectFunction, 2> Functions;
  S2DirectFunction &Root = Functions[0];
  Root.Present = true;
  Root.FrameSizeBytes = 16;
  Root.RegisterTypes.assign({PADDR, PADDR, I8, I8, I32, I32});
  Root.BlockStarts.push_back(0);
  Root.Instructions = {
      inst(S2_FRAME_ENTER, {}),
      inst(S2_PHYSICAL_ADDRESS, {0, UINT64_C(0x80000000)}),
      inst(S2_PHYSICAL_LOAD, {4, U32, 0}),
      inst(S2_FRAME_STORE, {4, 4, U32}),
      inst(S2_DIRECT_CALL, {1, 4, 4}),
      inst(S2_FRAME_LOAD, {5, 4, U32}),
      inst(S2_INTEGER_AND, {4, 4, 5}),
      inst(S2_PHYSICAL_ADDRESS, {0, UINT64_C(0x80000004)}),
      inst(S2_PHYSICAL_STORE, {U32, 0, 4}),
      inst(S2_FRAME_LEAVE, {}),
      inst(S2_RETURN, {}),
  };

  S2DirectFunction &Leaf = Functions[1];
  Leaf.Present = true;
  Leaf.RegisterTypes.assign({PADDR, PADDR, I8, I8, I32, I32});
  Leaf.ParameterSlots.push_back(4);
  Leaf.BlockStarts.push_back(0);
  Leaf.ResultKind = 2;
  Leaf.Instructions = {
      inst(S2_PHYSICAL_ADDRESS, {0, UINT64_C(0x80000008)}),
      inst(S2_PHYSICAL_LOAD, {5, U32, 0}),
      inst(S2_INTEGER_AND, {5, 5, 4}),
      inst(S2_INTEGER_AND, {4, 5, 5}),
      inst(S2_RETURN_VALUE, {4}),
  };
  return Functions;
}

std::array<S2DirectFunction, 2> canonicalByteFrameBF0() {
  auto Functions = canonicalByteFrameBF1();
  Functions[0].FrameSizeBytes = 0;
  Functions[0].Instructions = {
      inst(S2_PHYSICAL_ADDRESS, {0, UINT64_C(0x80000000)}),
      inst(S2_PHYSICAL_LOAD, {4, U32, 0}),
      inst(S2_DIRECT_CALL, {1, 4, 4}),
      inst(S2_PHYSICAL_ADDRESS, {0, UINT64_C(0x80000004)}),
      inst(S2_PHYSICAL_STORE, {U32, 0, 4}),
      inst(S2_RETURN, {}),
  };
  return Functions;
}

Error writeByteFrame(std::array<S2DirectFunction, 2> &Functions,
                     SmallVectorImpl<char> &Bytes) {
  raw_svector_ostream Out(Bytes);
  S2ObjectWriter Writer(Out, S2ObjectMode::DirectCallByteFrame);
  return Writer.writeDirectCallExact(Functions, /*EntryFunction=*/0,
                                     S2RelocationBase);
}

void expectByteFrameReject(std::array<S2DirectFunction, 2> Functions,
                           StringRef Diagnostic) {
  SmallVector<char, 2048> Bytes;
  Error E = writeByteFrame(Functions, Bytes);
  ASSERT_TRUE(static_cast<bool>(E));
  EXPECT_NE(toString(std::move(E)).find(Diagnostic), std::string::npos);
  EXPECT_TRUE(Bytes.empty());
}

uint32_t loadU32(ArrayRef<char> Bytes, size_t Offset) {
  EXPECT_LE(Offset + 4, Bytes.size());
  return static_cast<uint32_t>(static_cast<uint8_t>(Bytes[Offset])) |
         static_cast<uint32_t>(static_cast<uint8_t>(Bytes[Offset + 1])) << 8 |
         static_cast<uint32_t>(static_cast<uint8_t>(Bytes[Offset + 2])) << 16 |
         static_cast<uint32_t>(static_cast<uint8_t>(Bytes[Offset + 3])) << 24;
}

TEST(BraceS2ObjectWriterTest, AcceptsCanonicalDirectCallHomeH1) {
  auto Functions = canonicalH1();
  SmallVector<char, 2048> Bytes;
  EXPECT_FALSE(static_cast<bool>(write(Functions, Bytes)));
  EXPECT_FALSE(Bytes.empty());
}

TEST(BraceS2ObjectWriterTest, RejectsNoncanonicalHomeReferences) {
  auto ConstantHome = canonicalH1();
  ConstantHome[0].Instructions[0] = inst(S2_CONSTANT, {6, I32, 1});
  expectReject(std::move(ConstantHome), "home is referenced by Constant");

  auto LoadHome = canonicalH1();
  LoadHome[0].Instructions[1] = inst(S2_PHYSICAL_LOAD, {6, U32, 0});
  expectReject(std::move(LoadHome), "home is referenced by PhysicalLoad");

  auto StoreHome = canonicalH1();
  StoreHome[0].Instructions[7] = inst(S2_PHYSICAL_STORE, {U32, 0, 6});
  expectReject(std::move(StoreHome), "home is referenced by PhysicalStore");

  auto BranchHome = canonicalH1();
  BranchHome[0].Instructions[7] = inst(S2_BRANCH_IF, {6, 8, 0});
  expectReject(std::move(BranchHome), "home is used by BranchIf");

  auto CallHome = canonicalH1();
  CallHome[0].Instructions[3] = inst(S2_DIRECT_CALL, {1, 4, 6});
  expectReject(std::move(CallHome), "home is used by Call");

  auto MixedCopy = canonicalH1();
  MixedCopy[0].Instructions[2] = inst(S2_INTEGER_AND, {6, 4, 5});
  expectReject(std::move(MixedCopy), "home IntegerAnd is not a canonical copy");

  auto HomeToHome = canonicalH1();
  HomeToHome[0].Instructions[2] = inst(S2_INTEGER_AND, {6, 6, 6});
  expectReject(std::move(HomeToHome),
               "home IntegerAnd is not a canonical copy");
}

TEST(BraceS2ObjectWriterTest, RecomputesCallClobberAndHomeCausality) {
  auto DirectResidentRead = canonicalH1();
  DirectResidentRead[0].Instructions[6] = inst(S2_INTEGER_AND, {4, 4, 4});
  expectReject(std::move(DirectResidentRead),
               "register is read before definition");

  auto ForgedSaveOrigin = canonicalH1();
  ForgedSaveOrigin[0].Instructions[1] = inst(S2_CONSTANT, {4, I32, 7});
  expectReject(std::move(ForgedSaveOrigin),
               "saved home does not originate from a physical i32 load");

  auto DeadRestore = canonicalH1();
  DeadRestore[0].Instructions[5] = inst(S2_CONSTANT, {5, I32, 0});
  expectReject(std::move(DeadRestore),
               "restored home does not reach an observable physical store");
}

TEST(BraceS2ObjectWriterTest, RejectsRepeatedCallAndDeadCallResult) {
  auto CallCycle = canonicalH1();
  CallCycle[0].Instructions[8] = inst(S2_BRANCH_IF, {4, 0, 9});
  CallCycle[0].Instructions.push_back(inst(S2_RETURN, {}));
  CallCycle[0].BlockStarts.push_back(9);
  expectReject(std::move(CallCycle),
               "entry direct call can execute more than once");

  auto DeadCallResult = canonicalH1();
  DeadCallResult[0].Instructions[5] = inst(S2_INTEGER_AND, {5, 5, 5});
  DeadCallResult[0].Instructions[7] = inst(S2_PHYSICAL_STORE, {U32, 0, 5});
  expectReject(std::move(DeadCallResult),
               "direct-call result is not consumed on every root exit path");
}

TEST(BraceS2ObjectWriterTest, AcceptsCanonicalDirectCallByteFrameBF1) {
  auto Functions = canonicalByteFrameBF1();
  SmallVector<char, 2048> Bytes;
  EXPECT_FALSE(static_cast<bool>(writeByteFrame(Functions, Bytes)));
  ASSERT_EQ(Bytes.size(), 1320u);
  EXPECT_EQ(loadU32(Bytes, 48), UINT32_C(0x42520400));
  EXPECT_EQ(loadU32(Bytes, 108), 16u);
  EXPECT_EQ(loadU32(Bytes, 172), 0u);
  const std::array<uint32_t, 16> Opcodes = {25, 22, 23, 27, 16, 26, 4, 22,
                                            24, 28, 20, 22, 23, 4,  4, 20};
  for (unsigned Index = 0; Index != Opcodes.size(); ++Index)
    EXPECT_EQ((loadU32(Bytes, 0x120 + 4 * Index) >> 2) & 63, Opcodes[Index]);
}

TEST(BraceS2ObjectWriterTest, AcceptsCanonicalDirectCallByteFrameBF0) {
  auto Functions = canonicalByteFrameBF0();
  SmallVector<char, 2048> Bytes;
  EXPECT_FALSE(static_cast<bool>(writeByteFrame(Functions, Bytes)));
  ASSERT_EQ(Bytes.size(), 1304u);
  EXPECT_EQ(loadU32(Bytes, 48), UINT32_C(0x42520400));
  EXPECT_EQ(loadU32(Bytes, 108), 0u);
  EXPECT_EQ(loadU32(Bytes, 172), 0u);
  const std::array<uint32_t, 11> Opcodes = {22, 23, 16, 22, 24, 20,
                                            22, 23, 4,  4,  20};
  for (unsigned Index = 0; Index != Opcodes.size(); ++Index)
    EXPECT_EQ((loadU32(Bytes, 0x120 + 4 * Index) >> 2) & 63, Opcodes[Index]);
}

TEST(BraceS2ObjectWriterTest,
     RejectsDirectCallByteFrameLifecycleAndJointFlowForgeries) {
  auto WrongOffset = canonicalByteFrameBF1();
  WrongOffset[0].Instructions[3] = inst(S2_FRAME_STORE, {0, 4, U32});
  expectByteFrameReject(std::move(WrongOffset),
                        "FrameStore32 operands are not exact");

  auto WrongWidth = canonicalByteFrameBF1();
  WrongWidth[0].Instructions[5] = inst(S2_FRAME_LOAD, {5, 4, U8});
  expectByteFrameReject(std::move(WrongWidth),
                        "FrameLoad32 operands are not exact");

  auto WrongFrameSize = canonicalByteFrameBF1();
  WrongFrameSize[0].FrameSizeBytes = 32;
  expectByteFrameReject(std::move(WrongFrameSize),
                        "requires BF0 or root frame 16");

  auto SplitCall = canonicalByteFrameBF1();
  SplitCall[0].BlockStarts.push_back(4);
  expectByteFrameReject(std::move(SplitCall), "frame lifecycle is not exact");

  auto SplitReturn = canonicalByteFrameBF1();
  SplitReturn[0].BlockStarts.push_back(10);
  expectByteFrameReject(std::move(SplitReturn), "frame lifecycle is not exact");

  auto EntryBackedge = canonicalByteFrameBF1();
  EntryBackedge[0].Instructions.insert(
      EntryBackedge[0].Instructions.begin() + 8, inst(S2_BRANCH_IF, {4, 0, 9}));
  EntryBackedge[0].BlockStarts.push_back(9);
  expectByteFrameReject(std::move(EntryBackedge),
                        "entry operation has a predecessor or backedge");

  auto OnlyCallResult = canonicalByteFrameBF1();
  OnlyCallResult[0].Instructions[6] = inst(S2_INTEGER_AND, {4, 4, 4});
  expectByteFrameReject(std::move(OnlyCallResult),
                        "Call result and FrameLoad do not jointly reach");

  auto TwoHelperReturns = canonicalByteFrameBF1();
  TwoHelperReturns[1].Instructions.back() = inst(S2_BRANCH_IF, {4, 5, 6});
  TwoHelperReturns[1].Instructions.push_back(inst(S2_RETURN_VALUE, {4}));
  TwoHelperReturns[1].Instructions.push_back(inst(S2_RETURN_VALUE, {4}));
  TwoHelperReturns[1].BlockStarts.append({5, 6});
  expectByteFrameReject(std::move(TwoHelperReturns),
                        "requires exactly one Return");
}

TEST(BraceS2ObjectWriterTest,
     OldDirectCallModesRejectDirectCallByteFrameDeclaration) {
  for (S2ObjectMode Mode :
       {S2ObjectMode::DirectCall, S2ObjectMode::DirectCallHome}) {
    auto Functions = canonicalByteFrameBF0();
    Functions[0].FrameSizeBytes = 16;
    SmallVector<char, 2048> Bytes;
    Error E = writeMode(Functions, Bytes, Mode);
    ASSERT_TRUE(static_cast<bool>(E));
    EXPECT_NE(
        toString(std::move(E))
            .find("older direct-call identity carries a frame declaration"),
        std::string::npos);
    EXPECT_TRUE(Bytes.empty());
  }
}

TEST(BraceS2ObjectWriterTest, DirectCallByteFrameOperationLimitIsExact) {
  auto Exact = canonicalByteFrameBF1();
  Exact[0].Instructions.insert(Exact[0].Instructions.begin() + 1, 117,
                               inst(S2_CONSTANT, {5, I32, 0}));
  SmallVector<char, 4096> Bytes;
  EXPECT_FALSE(static_cast<bool>(writeByteFrame(Exact, Bytes)));
  EXPECT_FALSE(Bytes.empty());

  auto OneMore = canonicalByteFrameBF1();
  OneMore[0].Instructions.insert(OneMore[0].Instructions.begin() + 1, 118,
                                 inst(S2_CONSTANT, {5, I32, 0}));
  expectByteFrameReject(std::move(OneMore),
                        "direct-call function operation range is invalid");
}

} // namespace
