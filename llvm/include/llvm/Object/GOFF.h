//===- GOFF.h - GOFF object file implementation -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the GOFFObjectFile class.
// Record classes and derivatives are also declared and implemented.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_GOFF_H
#define LLVM_OBJECT_GOFF_H

#include "llvm/ADT/SmallString.h"
#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

namespace GOFF {

/// \brief Represents a GOFF physical record.
///
/// Specifies protected member functions to manipulate the record. These should
/// be called from deriving classes to change values as that record specifies.
class Record {
private:
  const uint8_t *Ptr = nullptr;

public:
  Record() = default;
  Record(const uint8_t *Buffer) : Ptr(Buffer) {};

  /// Returns true if the record is non-null.
  explicit operator bool() const {
    return Ptr != nullptr;
  }
  bool operator==(Record Other) const {
    return Ptr == Other.Ptr;
  }

  /// Increment the record pointer.
  Record &operator++() {
    Ptr += RecordLength;
    return *this;
  }

  [[nodiscard]] const uint8_t *getBuffer() const {
    return Ptr;
  }

  [[nodiscard]] RecordType getType() const {
    return static_cast<GOFF::RecordType>(Ptr[1] >> 4);
  }

  [[nodiscard]] bool isContinued() const {
    return static_cast<bool>(getBits(1, 7, 1));
  }

  [[nodiscard]] bool isContinuation() const {
    return static_cast<bool>(getBits(1, 6, 1));
  }

protected:
  Error getContinuousData(uint16_t DataLength, uint32_t Offset,
                                 SmallString<256> &CompleteData) const;

  /// \brief Get bit field of specified byte.
  ///
  /// Used to pack bit fields into one byte. Fields are packed left to right.
  /// Bit index zero is the most significant bit of the byte.
  ///
  /// \param ByteIndex index of byte the field is in.
  /// \param BitIndex index of first bit of field.
  /// \param Length length of bit field.
  /// \param Value value of bit field.
  [[nodiscard]] uint8_t getBits(uint8_t ByteIndex, uint8_t BitIndex, uint8_t Length) const {
    assert(Ptr != nullptr);
    assert(ByteIndex < GOFF::RecordLength && "Byte index out of bounds!");
    assert(BitIndex < 8 && "Bit index out of bounds!");
    assert(Length + BitIndex <= 8 && "Bit length too long!");

    auto Value = get<uint8_t>(ByteIndex);
    Value = (Value >> (8 - BitIndex - Length)) & ((1 << Length) - 1);
    return Value;
  }

  template <class T>
  [[nodiscard]] T get(uint8_t ByteIndex) const {
    assert(Ptr != nullptr);
    assert(ByteIndex + sizeof(T) <= GOFF::RecordLength &&
           "Byte index out of bounds!");
    return support::endian::read<T, llvm::endianness::big>(Ptr + ByteIndex);
  }
};

class TXTRecord : public Record {
public:
  /// \brief Maximum length of data; any more must go in continuation.
  static constexpr uint8_t TXTMaxDataLength = 56;

  TXTRecord() = default;
  TXTRecord(Record R) : Record(R) {}

  Error getData(SmallString<256> &CompleteData) const ;

  [[nodiscard]] uint32_t getElementEsdId() const {
    return get<uint32_t>(4);
  }

  [[nodiscard]] uint32_t getOffset() const {
    return get<uint32_t>(12);
  }

  [[nodiscard]] uint16_t getDataLength() const {
    return get<uint16_t>(22);
  }
};

class HDRRecord : public Record {
public:
  HDRRecord() = default;
  HDRRecord(Record R) : Record(R) {}
  Error getData(SmallString<256> &CompleteData) const;

  [[nodiscard]] uint16_t getPropertyModuleLength() const {
    return get<uint16_t>(52);
  }
};

class ESDRecord : public Record {
public:
  ESDRecord() = default;
  ESDRecord(Record R) : Record(R) {}

  Error getData(SmallString<256> &CompleteData) const;

  // ESD Get routines.
  [[nodiscard]] ESDSymbolType getSymbolType() const {
    return static_cast<ESDSymbolType>(get<uint8_t>(3));
  }

  [[nodiscard]] uint32_t getEsdId() const {
    return get<uint32_t>(4);
  }

  [[nodiscard]] uint32_t getParentEsdId() const {
    return get<uint32_t>(8);
  }

  [[nodiscard]] uint32_t getOffset() const {
    return get<uint32_t>(16);
  }

  [[nodiscard]] uint32_t getLength() const {
    return get<uint32_t>(24);
  }

  [[nodiscard]] ESDNameSpaceId getNameSpaceId() const {
    return static_cast<GOFF::ESDNameSpaceId>(get<uint8_t>(40));
  }

  [[nodiscard]] bool getFillBytePresent() const {
    return static_cast<bool>(getBits(41, 0, 1));
  }

  [[nodiscard]] bool getNameMangled() const {
    return static_cast<bool>(getBits(41, 1, 1));
  }

  [[nodiscard]] bool getRenamable() const {
    return static_cast<bool>(getBits(41, 2, 1));
  }

  [[nodiscard]] bool getRemovable() const {
    return static_cast<bool>(getBits(41, 3, 1));
  }

  [[nodiscard]] uint8_t getFillByteValue() const {
    return get<uint8_t>(42);
  }

  [[nodiscard]] uint32_t getAdaEsdId() const {
    return get<uint32_t>(44);
  }

  [[nodiscard]] uint32_t getSortPriority() const {
    return get<uint32_t>(48);
  }

  [[nodiscard]] ESDAmode getAmode() const {
    return static_cast<GOFF::ESDAmode>(get<uint8_t>(60));
  }

  [[nodiscard]] ESDRmode getRmode() const {
    return static_cast<GOFF::ESDRmode>(get<uint8_t>(61));
  }

  [[nodiscard]] ESDTextStyle getTextStyle() const {
    return static_cast<GOFF::ESDTextStyle>(getBits(62, 0, 4));
  }

  [[nodiscard]] ESDBindingAlgorithm getBindingAlgorithm() const {
    return static_cast<GOFF::ESDBindingAlgorithm>(getBits(62, 4, 4));
  }

  [[nodiscard]] ESDTaskingBehavior getTaskingBehavior() const {
    return static_cast<GOFF::ESDTaskingBehavior>(getBits(63, 0, 3));
  }

  [[nodiscard]] bool getReadOnly() const {
    return static_cast<bool>(getBits(63, 4, 1));
  }

  [[nodiscard]] ESDExecutable getExecutable() const {
    return static_cast<GOFF::ESDExecutable>(getBits(63, 5, 3));
  }

  [[nodiscard]] ESDDuplicateSymbolSeverity getDuplicateSeverity() const {
    return static_cast<GOFF::ESDDuplicateSymbolSeverity>(getBits(64, 2, 2));
  }

  [[nodiscard]] ESDBindingStrength getBindingStrength() const {
    return static_cast<GOFF::ESDBindingStrength>(getBits(64, 4, 4));
  }

  [[nodiscard]] ESDLoadingBehavior getLoadingBehavior() const {
    return static_cast<GOFF::ESDLoadingBehavior>(getBits(65, 0, 2));
  }

  [[nodiscard]] bool getIndirectReference() const {
    return static_cast<bool>(getBits(65, 3, 1));
  }

  [[nodiscard]] ESDBindingScope getBindingScope() const {
    return static_cast<GOFF::ESDBindingScope>(getBits(65, 4, 4));
  }

  [[nodiscard]] ESDLinkageType getLinkageType() const {
    return static_cast<GOFF::ESDLinkageType>(getBits(66, 2, 1));
  }

  [[nodiscard]] ESDAlignment getAlignment() const {
    return static_cast<GOFF::ESDAlignment>(getBits(66, 3, 5));
  }

  [[nodiscard]] uint16_t getNameLength() const {
    return get<uint16_t>(70);
  }
};

class ENDRecord : public Record {
public:
  ENDRecord() = default;
  ENDRecord(Record R) : Record(R) {}
  Error getData(SmallString<256> &CompleteData) const;

  [[nodiscard]] uint16_t getNameLength() const {
    return get<uint16_t>(24);
  }
};

} // end namespace GOFF

} // end namespace llvm

#endif
