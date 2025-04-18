//===-- GOFFDump.cpp - GOFF-specific dumper ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the GOFF-specific dumper for llvm-objdump.
///
//===----------------------------------------------------------------------===//

#include "GOFFDump.h"

#include "llvm-objdump.h"
#include "llvm/Object/GOFFObjectFile.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::objdump;

namespace {
class GOFFDumper : public Dumper {
public:
  GOFFDumper(const GOFFObjectFile &O) : Dumper(O) {}
};
} // namespace

std::unique_ptr<Dumper> llvm::objdump::createGOFFDumper(const GOFFObjectFile &Obj) {
  return std::make_unique<GOFFDumper>(Obj);
}
