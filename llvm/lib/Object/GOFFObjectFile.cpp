//===- GOFFObjectFile.cpp - GOFF object file implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the GOFFObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/GOFFObjectFile.h"
#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/Object/GOFF.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>

#ifndef DEBUG_TYPE
#define DEBUG_TYPE "goff"
#endif

using namespace llvm::object;
using namespace llvm;

Expected<std::unique_ptr<ObjectFile>>
ObjectFile::createGOFFObjectFile(MemoryBufferRef Object) {
  Error Err = Error::success();
  std::unique_ptr<GOFFObjectFile> Ret(new GOFFObjectFile(Object, Err));
  if (Err)
    return std::move(Err);
  return std::move(Ret);
}

GOFFObjectFile::GOFFObjectFile(MemoryBufferRef Object, Error &Err)
    : ObjectFile(Binary::ID_GOFF, Object) {
  ErrorAsOutParameter ErrAsOutParam(Err);
  // Object file isn't the right size, bail out early.
  if ((Object.getBufferSize() % GOFF::RecordLength) != 0) {
    Err = createStringError(
        object_error::unexpected_eof,
        "object file is not the right size. Must be a multiple "
        "of 80 bytes, but is " +
            std::to_string(Object.getBufferSize()) + " bytes");
    return;
  }
  // Object file doesn't start/end with HDR/END records.
  // Bail out early.
  if (Object.getBufferSize() != 0) {
    if ((base()[1] & 0xF0) >> 4 != GOFF::RT_HDR) {
      Err = createStringError(object_error::parse_failed,
                              "object file must start with HDR record");
      return;
    }
    if ((base()[Object.getBufferSize() - GOFF::RecordLength + 1] & 0xF0) >> 4 !=
        GOFF::RT_END) {
      Err = createStringError(object_error::parse_failed,
                              "object file must end with END record");
      return;
    }
  }

  SectionEntryImpl DummySection;
  SectionList.emplace_back(DummySection); // Dummy entry at index 0.

  GOFF::RecordType PrevRecordType = GOFF::RecordType::RT_HDR;
  uint8_t PrevWasContinued = false;
  const uint8_t *End = reinterpret_cast<const uint8_t *>(Data.getBufferEnd());
  for (GOFF::Record I(base()); I.getBuffer() != End; ++I) {
    GOFF::RecordType RecordType = I.getType();
    bool IsContinuation = I.isContinuation();
    size_t RecordNum = (I.getBuffer() - base()) / GOFF::RecordLength;

    // If the previous record was continued, the current record should be a
    // continuation.
    if (PrevWasContinued && !IsContinuation) {
      if (PrevRecordType == RecordType) {
        Err = createStringError(object_error::parse_failed,
                                "record " + std::to_string(RecordNum) +
                                    " is not a continuation record but the "
                                    "preceding record is continued");
        return;
      }
    }
    // Don't parse continuations records, only parse initial record.
    if (IsContinuation) {
      if (RecordType != PrevRecordType) {
        Err = createStringError(object_error::parse_failed,
                                "record " + std::to_string(RecordNum) +
                                    " is a continuation record that does not "
                                    "match the type of the previous record");
        return;
      }
      if (!PrevWasContinued) {
        Err = createStringError(object_error::parse_failed,
                                "record " + std::to_string(RecordNum) +
                                    " is a continuation record that is not "
                                    "preceded by a continued record");
        return;
      }
      PrevRecordType = RecordType;
      PrevWasContinued = I.isContinued();
      continue;
    }
    LLVM_DEBUG(for (size_t J = 0; J < GOFF::RecordLength; ++J) {
      const uint8_t *P = I.getBuffer() + J;
      if (J % 8 == 0)
        dbgs() << "  ";
      dbgs() << format("%02hhX", *P);
    });

    switch (RecordType) {
    case GOFF::RT_ESD:
      if (Error E = addEsdRecord(GOFF::ESDRecord(I))) {
        Err = std::move(E);
        return;
      }
      break;
    case GOFF::RT_TXT:
      // Save TXT records.
      TextPtrs.emplace_back(I);
      LLVM_DEBUG(dbgs() << "  --  TXT\n");
      break;
    case GOFF::RT_RLD:
      LLVM_DEBUG(dbgs() << "  --  RLD\n");
      break;
    case GOFF::RT_LEN:
      LLVM_DEBUG(dbgs() << "  --  LEN\n");
      break;
    case GOFF::RT_END:
      LLVM_DEBUG(dbgs() << "  --  END (GOFF record type) unhandled\n");
      break;
    case GOFF::RT_HDR:
      LLVM_DEBUG(dbgs() << "  --  HDR (GOFF record type) unhandled\n");
      break;
    default:
      Err = createStringError(object_error::parse_failed,
                              "record " + std::to_string(RecordNum) +
                                  " has invalid record type " +
                                  std::to_string(RecordType));
      return;
    }
    PrevRecordType = RecordType;
    PrevWasContinued = I.isContinued();
  }
}

Error GOFFObjectFile::addEsdRecord(GOFF::ESDRecord Esd) {
  uint32_t EsdId = Esd.getEsdId();
  EsdPtrs.grow(EsdId);
  EsdPtrs[EsdId] = Esd;
  LLVM_DEBUG(dbgs() << "  --  ESD " << EsdId << "\n");

  uint32_t Length = Esd.getLength();

  // Determine and save the "sections" in GOFF.
  // A section is saved as a tuple of the form
  // case (1a) (ED,0)
  //   - where the ED is of non-zero length.
  // case (1b) (ED,0)
  //   - where the ED is zero length but
  //     contains a label (LD).
  // case (2): (ED,child PR)
  //    - where the PR must have non-zero length.
  SectionEntryImpl Section;
  switch (Esd.getSymbolType()) {
  case GOFF::ESD_ST_SectionDefinition:
    break;
  case GOFF::ESD_ST_ElementDefinition:
    // case (1a)
    if (Length != 0) {
      Section.d.a = EsdId;
      SectionList.emplace_back(Section);
    }
    break;
  case GOFF::ESD_ST_LabelDefinition: {
    uint32_t ElementEsdId = Esd.getParentEsdId();
    GOFF::ESDRecord ElementEsd = getEsdRecord(ElementEsdId);
    uint32_t ElementLength = ElementEsd.getLength();
    // case (1b)
    if (ElementLength != 0) {
      // LD child of a zero length parent ED.
      // Add the section ED which was previously ignored.
      Section.d.a = ElementEsdId;
      SectionList.emplace_back(Section);
    }
    break;
  }
  case GOFF::ESD_ST_PartReference:
    // case (2)
    if (Length != 0) {
      uint32_t ElementEsdId = Esd.getParentEsdId();
      Section.d.a = ElementEsdId;
      Section.d.b = EsdId;
      SectionList.emplace_back(Section);
    }
    break;
  case GOFF::ESD_ST_ExternalReference:
    break;
  default:
      return createStringError(object_error::parse_failed,
                              "ESD " + std::to_string(EsdId) +
                                  " has invalid symbol type " +
                                  std::to_string(Esd.getSymbolType()));
  }

  return Error::success();
}

GOFF::ESDRecord GOFFObjectFile::getEsdRecord(uint32_t EsdId) const {
  return EsdPtrs[EsdId];
}
GOFF::ESDRecord GOFFObjectFile::getSymbolEsdRecord(DataRefImpl Symb) const {
  return EsdPtrs[Symb.d.a];
}

Expected<StringRef> GOFFObjectFile::getEsdName(uint32_t EsdId) const {
  if (auto It = EsdNamesCache.find(EsdId); It != EsdNamesCache.end()) {
    auto &StrPtr = It->second;
    return StringRef(StrPtr.second.get(), StrPtr.first);
  }

  SmallString<256> SymbolName;
  if (auto Err = getEsdRecord(EsdId).getData(SymbolName))
    return std::move(Err);

  SmallString<256> SymbolNameConverted;
  ConverterEBCDIC::convertToUTF8(SymbolName, SymbolNameConverted);

  size_t Size = SymbolNameConverted.size();
  auto StrPtr = std::make_pair(Size, std::make_unique<char[]>(Size));
  char *Buf = StrPtr.second.get();
  memcpy(Buf, SymbolNameConverted.data(), Size);
  EsdNamesCache[EsdId] = std::move(StrPtr);
  return StringRef(Buf, Size);
}

Expected<StringRef> GOFFObjectFile::getSymbolName(DataRefImpl Symb) const {
  return getEsdName(Symb.d.a);
}

Expected<StringRef> GOFFObjectFile::getSymbolName(SymbolRef Symbol) const {
  return getSymbolName(Symbol.getRawDataRefImpl());
}

Expected<uint64_t> GOFFObjectFile::getSymbolAddress(DataRefImpl Symb) const {
  return getSymbolEsdRecord(Symb).getOffset();
}

uint64_t GOFFObjectFile::getSymbolValueImpl(DataRefImpl Symb) const {
  return getSymbolEsdRecord(Symb).getOffset();
}

uint64_t GOFFObjectFile::getCommonSymbolSizeImpl(DataRefImpl Symb) const {
  return 0;
}

bool GOFFObjectFile::isSymbolUnresolved(DataRefImpl Symb) const {
  GOFF::ESDRecord Esd = getSymbolEsdRecord(Symb);
  GOFF::ESDSymbolType SymbolType = Esd.getSymbolType();

  if (SymbolType == GOFF::ESD_ST_ExternalReference)
    return true;
  if (SymbolType == GOFF::ESD_ST_PartReference) {
    uint32_t Length = Esd.getLength();
    if (Length == 0)
      return true;
  }
  return false;
}

bool GOFFObjectFile::isSymbolIndirect(DataRefImpl Symb) const {
  return getSymbolEsdRecord(Symb).getIndirectReference();
}

Expected<uint32_t> GOFFObjectFile::getSymbolFlags(DataRefImpl Symb) const {
  uint32_t Flags = 0;
  if (isSymbolUnresolved(Symb))
    Flags |= SymbolRef::SF_Undefined;

  GOFF::ESDRecord Esd = getSymbolEsdRecord(Symb);

  GOFF::ESDBindingStrength BindingStrength = Esd.getBindingStrength();
  if (BindingStrength == GOFF::ESD_BST_Weak)
    Flags |= SymbolRef::SF_Weak;

  GOFF::ESDBindingScope BindingScope = Esd.getBindingScope();

  if (BindingScope != GOFF::ESD_BSC_Section) {
    Expected<StringRef> Name = getSymbolName(Symb);
    if (Name && *Name != " ") { // Blank name is local.
      Flags |= SymbolRef::SF_Global;
      if (BindingScope == GOFF::ESD_BSC_ImportExport)
        Flags |= SymbolRef::SF_Exported;
      else if (!(Flags & SymbolRef::SF_Undefined))
        Flags |= SymbolRef::SF_Hidden;
    }
  }

  return Flags;
}

Expected<SymbolRef::Type>
GOFFObjectFile::getSymbolType(DataRefImpl Symb) const {
  GOFF::ESDRecord Esd = getSymbolEsdRecord(Symb);
  uint32_t EsdId = Esd.getEsdId();
  GOFF::ESDSymbolType SymbolType = Esd.getSymbolType();
  GOFF::ESDExecutable Executable = Esd.getExecutable();

  if (SymbolType != GOFF::ESD_ST_SectionDefinition &&
      SymbolType != GOFF::ESD_ST_ElementDefinition &&
      SymbolType != GOFF::ESD_ST_LabelDefinition &&
      SymbolType != GOFF::ESD_ST_PartReference &&
      SymbolType != GOFF::ESD_ST_ExternalReference) {
    return createStringError(llvm::errc::invalid_argument,
                             "ESD record %" PRIu32
                             " has invalid symbol type 0x%02" PRIX8,
                             EsdId, SymbolType);
  }
  switch (SymbolType) {
  case GOFF::ESD_ST_SectionDefinition:
  case GOFF::ESD_ST_ElementDefinition:
    return SymbolRef::ST_Other;
  case GOFF::ESD_ST_LabelDefinition:
  case GOFF::ESD_ST_PartReference:
  case GOFF::ESD_ST_ExternalReference:
    switch (Executable) {
    case GOFF::ESD_EXE_CODE:
      return SymbolRef::ST_Function;
    case GOFF::ESD_EXE_DATA:
      return SymbolRef::ST_Data;
    case GOFF::ESD_EXE_Unspecified:
      return SymbolRef::ST_Unknown;
    }
    return createStringError(llvm::errc::invalid_argument,
                             "ESD record %" PRIu32
                             " has unknown Executable type 0x%02X",
                             EsdId, Executable);
  }
  llvm_unreachable("Unhandled ESDSymbolType");
}

Expected<section_iterator>
GOFFObjectFile::getSymbolSection(DataRefImpl Symb) const {
  DataRefImpl Sec;

  if (isSymbolUnresolved(Symb))
    return section_iterator(SectionRef(Sec, this));

  GOFF::ESDRecord SymEsdRecord = getSymbolEsdRecord(Symb);
  uint32_t SymEdId = SymEsdRecord.getParentEsdId();
  GOFF::ESDRecord SymEdRecord = getEsdRecord(SymEdId);

  for (size_t I = 0, E = SectionList.size(); I < E; ++I) {
    bool Found;
    GOFF::ESDRecord SectionPrRecord = getSectionPrEsdRecord(I);
    if (SectionPrRecord) {
      Found = SymEsdRecord == SectionPrRecord;
    } else {
      GOFF::ESDRecord SectionEdRecord = getSectionEdEsdRecord(I);
      Found = SymEdRecord == SectionEdRecord;
    }

    if (Found) {
      Sec.d.a = I;
      return section_iterator(SectionRef(Sec, this));
    }
  }
  return createStringError(llvm::errc::invalid_argument,
                           "symbol with ESD id " + std::to_string(Symb.d.a) +
                               " refers to invalid section with ESD id " +
                               std::to_string(SymEdId));
}

uint64_t GOFFObjectFile::getSymbolSize(DataRefImpl Symb) const {
  return getSymbolEsdRecord(Symb).getLength();
}

GOFF::ESDRecord GOFFObjectFile::getSectionEdEsdRecord(DataRefImpl &Sec) const {
  return getSectionEdEsdRecord(Sec.d.a);
}
GOFF::ESDRecord
GOFFObjectFile::getSectionEdEsdRecord(uint32_t SectionIndex) const {
  SectionEntryImpl EsdIds = SectionList[SectionIndex];
  return EsdPtrs[EsdIds.d.a];
}

GOFF::ESDRecord GOFFObjectFile::getSectionPrEsdRecord(DataRefImpl &Sec) const {
  return getSectionPrEsdRecord(Sec.d.a);
}
GOFF::ESDRecord
GOFFObjectFile::getSectionPrEsdRecord(uint32_t SectionIndex) const {
  SectionEntryImpl EsdIds = SectionList[SectionIndex];
  GOFF::ESDRecord EsdRecord;
  if (EsdIds.d.b)
    EsdRecord = EsdPtrs[EsdIds.d.b];
  return EsdRecord;
}

uint32_t GOFFObjectFile::getSectionDefEsdId(DataRefImpl &Sec) const {
  GOFF::ESDRecord EsdRecord = getSectionEdEsdRecord(Sec);
  uint32_t Length = EsdRecord.getLength();
  if (Length == 0) {
    GOFF::ESDRecord PrEsdRecord = getSectionPrEsdRecord(Sec);
    if (PrEsdRecord)
      EsdRecord = PrEsdRecord;
  }

  uint32_t DefEsdId = EsdRecord.getEsdId();
  LLVM_DEBUG(dbgs() << "Got def EsdId: " << DefEsdId << '\n');
  return DefEsdId;
}

void GOFFObjectFile::moveSectionNext(DataRefImpl &Sec) const {
  Sec.d.a++;
  if ((Sec.d.a) >= SectionList.size())
    Sec.d.a = 0;
}

Expected<StringRef> GOFFObjectFile::getSectionName(DataRefImpl Sec) const {
  GOFF::ESDRecord EdEsd = getSectionEdEsdRecord(Sec);
  uint32_t SdEsdId = EdEsd.getParentEsdId();
  Expected<StringRef> Name = getEsdName(SdEsdId);

  if (Name) {
    StringRef Res = *Name;
    LLVM_DEBUG(dbgs() << "section name: " << Res << '\n');
    Name = Res;
  }
  return Name;
}

Expected<StringRef> GOFFObjectFile::getSectionClass(DataRefImpl Sec) const {
  GOFF::ESDRecord EdEsd = getSectionEdEsdRecord(Sec);
  Expected<StringRef> Name = getEsdName(EdEsd.getEsdId());

  if (Name) {
    StringRef Res = *Name;
    LLVM_DEBUG(dbgs() << "class name: " << Res << '\n');
    Name = Res;
  }
  return Name;
}

uint64_t GOFFObjectFile::getSectionAddress(DataRefImpl Sec) const {
  GOFF::ESDRecord EsdRecord = getSectionEdEsdRecord(Sec);
  return EsdRecord.getOffset();
}

uint64_t GOFFObjectFile::getSectionSize(DataRefImpl Sec) const {
  uint32_t DefEsdId = getSectionDefEsdId(Sec);
  GOFF::ESDRecord EsdRecord = getEsdRecord(DefEsdId);
  return EsdRecord.getLength();
}

// Unravel TXT records and expand fill characters to produce
// a contiguous sequence of bytes.
Expected<ArrayRef<uint8_t>>
GOFFObjectFile::getSectionContents(DataRefImpl Sec) const {
  if (auto It = SectionDataCache.find(Sec.d.a); It != SectionDataCache.end()) {
    auto &Buf = It->second;
    return ArrayRef<uint8_t>(Buf);
  }
  uint64_t SectionSize = getSectionSize(Sec);
  uint64_t SectionOffset = getSectionAddress(Sec);
  uint32_t DefEsdId = getSectionDefEsdId(Sec);

  GOFF::ESDRecord EdEsdRecord = getSectionEdEsdRecord(Sec);
  uint8_t FillByte = '\0';
  if (EdEsdRecord.getFillBytePresent())
    FillByte = EdEsdRecord.getFillByteValue();

  // Initialize section with fill byte.
  SmallVector<uint8_t> Data(SectionSize, FillByte);
  LLVM_DEBUG(dbgs() << "Section size: " << SectionSize << '\n');

  // Replace section with content from text records.
  for (GOFF::TXTRecord TxtRecord : TextPtrs) {
    uint32_t TxtEsdId = TxtRecord.getElementEsdId();

    if (TxtEsdId != DefEsdId)
      continue;

    LLVM_DEBUG(dbgs() << "Got txt EsdId: " << TxtEsdId << '\n');

    uint32_t TxtDataOffset = TxtRecord.getOffset();
    uint16_t TxtDataSize = TxtRecord.getDataLength();

    LLVM_DEBUG(dbgs() << "Record offset " << TxtDataOffset << ", data size "
                      << TxtDataSize << "\n");

    SmallString<256> CompleteData;
    CompleteData.reserve(TxtDataSize);
    if (Error Err = TxtRecord.getData(CompleteData))
      return std::move(Err);
    assert(CompleteData.size() == TxtDataSize && "Wrong length of data");
    assert(TxtDataOffset + TxtDataSize - SectionOffset <= Data.size());
    std::copy(CompleteData.begin(), CompleteData.end(),
              Data.begin() + TxtDataOffset - SectionOffset);
  }
  auto &Cache = SectionDataCache[Sec.d.a];
  Cache = Data;
  return ArrayRef<uint8_t>(Cache);
}

uint64_t GOFFObjectFile::getSectionAlignment(DataRefImpl Sec) const {
  GOFF::ESDRecord EsdRecord = getSectionEdEsdRecord(Sec);
  GOFF::ESDAlignment Pow2Alignment = EsdRecord.getAlignment();
  return 1ULL << static_cast<uint64_t>(Pow2Alignment);
}

bool GOFFObjectFile::isSectionText(DataRefImpl Sec) const {
  GOFF::ESDRecord EsdRecord = getSectionEdEsdRecord(Sec);
  GOFF::ESDExecutable Executable = EsdRecord.getExecutable();
  return Executable == GOFF::ESD_EXE_CODE;
}

bool GOFFObjectFile::isSectionData(DataRefImpl Sec) const {
  GOFF::ESDRecord EsdRecord = getSectionEdEsdRecord(Sec);
  GOFF::ESDExecutable Executable = EsdRecord.getExecutable();
  return Executable == GOFF::ESD_EXE_DATA;
}

bool GOFFObjectFile::isSectionNoLoad(DataRefImpl Sec) const {
  GOFF::ESDRecord EsdRecord = getSectionEdEsdRecord(Sec);
  GOFF::ESDLoadingBehavior LoadingBehavior = EsdRecord.getLoadingBehavior();
  return LoadingBehavior == GOFF::ESD_LB_NoLoad;
}

bool GOFFObjectFile::isSectionReadOnlyData(DataRefImpl Sec) const {
  if (!isSectionData(Sec))
    return false;

  GOFF::ESDRecord EsdRecord = getSectionEdEsdRecord(Sec);
  GOFF::ESDLoadingBehavior LoadingBehavior = EsdRecord.getLoadingBehavior();
  return LoadingBehavior == GOFF::ESD_LB_Initial;
}

bool GOFFObjectFile::isSectionZeroInit(DataRefImpl Sec) const {
  // GOFF uses fill characters and fill characters are applied
  // on getSectionContents() - so we say false to zero init.
  return false;
}

section_iterator GOFFObjectFile::section_begin() const {
  DataRefImpl Sec;
  moveSectionNext(Sec);
  return section_iterator(SectionRef(Sec, this));
}

section_iterator GOFFObjectFile::section_end() const {
  DataRefImpl Sec;
  return section_iterator(SectionRef(Sec, this));
}

void GOFFObjectFile::moveSymbolNext(DataRefImpl &Symb) const {
  for (uint32_t I = Symb.d.a + 1, E = EsdPtrs.size(); I < E; ++I) {
    if (GOFF::ESDRecord EsdRecord = getEsdRecord(I)) {
      GOFF::ESDSymbolType SymbolType = EsdRecord.getSymbolType();
      // Skip EDs - i.e. section symbols.
      bool IgnoreSpecialGOFFSymbols = true;
      bool SkipSymbol = ((SymbolType == GOFF::ESD_ST_ElementDefinition) ||
                         (SymbolType == GOFF::ESD_ST_SectionDefinition)) &&
                        IgnoreSpecialGOFFSymbols;
      if (!SkipSymbol) {
        Symb.d.a = I;
        return;
      }
    }
  }
  Symb.d.a = 0;
}

basic_symbol_iterator GOFFObjectFile::symbol_begin() const {
  DataRefImpl Symb;
  moveSymbolNext(Symb);
  return basic_symbol_iterator(SymbolRef(Symb, this));
}

basic_symbol_iterator GOFFObjectFile::symbol_end() const {
  DataRefImpl Symb;
  return basic_symbol_iterator(SymbolRef(Symb, this));
}

Error GOFF::Record::getContinuousData(uint16_t DataLength,
                                      uint32_t DataIndex,
                                      SmallString<256> &CompleteData) const {
  // First record.
  const uint8_t *Slice = Ptr + DataIndex;
  size_t SliceLength =
      std::min(DataLength, uint16_t(GOFF::RecordLength - DataIndex));
  CompleteData.append(Slice, Slice + SliceLength);
  DataLength -= SliceLength;
  Slice += SliceLength;

  // Continuation records.
  for (; DataLength > 0;
       DataLength -= SliceLength, Slice += GOFF::PayloadLength) {
    GOFF::Record Record(Slice);
    // Slice points to the start of the new record.
    // Check that this block is a Continuation.
    assert(Record.isContinuation() && "Continuation bit must be set");
    // Check that the last Continuation is terminated correctly.
    if (DataLength <= 77 && Record.isContinued())
      return createStringError(object_error::parse_failed,
                               "continued bit should not be set");

    SliceLength = std::min(DataLength, (uint16_t)GOFF::PayloadLength);
    Slice += GOFF::RecordPrefixLength;
    CompleteData.append(Slice, Slice + SliceLength);
  }
  return Error::success();
}

Error GOFF::HDRRecord::getData(SmallString<256> &CompleteData) const {
  uint16_t Length = getPropertyModuleLength();
  return getContinuousData(Length, 60, CompleteData);
}

Error GOFF::ESDRecord::getData(SmallString<256> &CompleteData) const {
  uint16_t DataSize = getNameLength();
  return getContinuousData(DataSize, 72, CompleteData);
}

Error GOFF::TXTRecord::getData(SmallString<256> &CompleteData) const {
  uint16_t Length = getDataLength();
  return getContinuousData(Length, 24, CompleteData);
}

Error GOFF::ENDRecord::getData(SmallString<256> &CompleteData) const {
  uint16_t Length = getNameLength();
  return getContinuousData(Length, 26, CompleteData);
}
