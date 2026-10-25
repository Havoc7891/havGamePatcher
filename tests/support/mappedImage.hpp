// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHER_TESTS_SUPPORT_MAPPEDIMAGE_HPP
#define HAVGAMEPATCHER_TESTS_SUPPORT_MAPPEDIMAGE_HPP

struct MappedImage
{
  unsigned char *base = nullptr;
  DWORD imageSize = 0;
  std::vector<IMAGE_SECTION_HEADER> sections;

  explicit MappedImage(const char *file, bool preferred, DWORD expectedBase)
  {
    if (preferred)
    {
      base = static_cast<unsigned char *>(VirtualAlloc(reinterpret_cast<void *>(0x20000000),
                                                       0x1000000, MEM_RESERVE | MEM_COMMIT,
                                                       PAGE_EXECUTE_READWRITE));
      Require(base != nullptr, "Owned first-address reservation failed.");
    }

    std::ifstream stream(file, std::ios::binary);
    Require(static_cast<bool>(stream), "Cannot read candidate.");
    Bytes bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    Require(bytes.size() >= sizeof(IMAGE_DOS_HEADER), "Candidate DOS header missing.");
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(bytes.data());
    Require(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew >= 0 &&
                static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) < bytes.size(),
            "Candidate DOS header invalid.");
    const auto *pe = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(bytes.data() + dos->e_lfanew);
    Require(pe->Signature == IMAGE_NT_SIGNATURE &&
                pe->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
                pe->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
                pe->OptionalHeader.ImageBase == expectedBase,
            "Candidate is not the expected fixed-base x86 image.");
    imageSize = pe->OptionalHeader.SizeOfImage;
    Require(imageSize >= 0x1000 && imageSize <= 0x1000000 &&
                pe->OptionalHeader.SizeOfHeaders <= bytes.size(),
            "Candidate image size is invalid.");
    if (!base)
    {
      base = static_cast<unsigned char *>(VirtualAlloc(reinterpret_cast<void *>(0x30000000),
                                                       imageSize, MEM_RESERVE | MEM_COMMIT,
                                                       PAGE_EXECUTE_READWRITE));
    }

    Require(base != nullptr, "Owned candidate memory allocation failed.");
    Require(reinterpret_cast<DWORD>(base) == (preferred ? 0x20000000u : 0x30000000u),
            "Candidate mapping moved away from its test address.");
    std::memcpy(base, bytes.data(), pe->OptionalHeader.SizeOfHeaders);
    const auto *section = IMAGE_FIRST_SECTION(pe);
    Require(reinterpret_cast<const unsigned char *>(section + pe->FileHeader.NumberOfSections) <=
                bytes.data() + bytes.size(),
            "Candidate section table truncated.");
    for (unsigned index = 0; index < pe->FileHeader.NumberOfSections; ++index)
    {
      const auto &current = section[index];
      Require(
          static_cast<uint64_t>(current.VirtualAddress) + current.Misc.VirtualSize <= imageSize &&
              static_cast<uint64_t>(current.VirtualAddress) + current.SizeOfRawData <= imageSize &&
              static_cast<uint64_t>(current.PointerToRawData) + current.SizeOfRawData <=
                  bytes.size(),
          "Candidate section outside the image.");
      if (current.SizeOfRawData)
      {
        std::memcpy(base + current.VirtualAddress, bytes.data() + current.PointerToRawData,
                    current.SizeOfRawData);
      }

      sections.push_back(current);
    }

    const DWORD delta = reinterpret_cast<DWORD>(base) - pe->OptionalHeader.ImageBase;
    const auto &relocations = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    Require(relocations.VirtualAddress != 0 && relocations.Size != 0 &&
                static_cast<uint64_t>(relocations.VirtualAddress) + relocations.Size <= imageSize,
            "Candidate relocation directory invalid.");
    DWORD consumed = 0;

    while (consumed < relocations.Size)
    {
      Require(relocations.Size - consumed >= sizeof(IMAGE_BASE_RELOCATION),
              "Truncated relocation block.");
      const auto *block = reinterpret_cast<const IMAGE_BASE_RELOCATION *>(
          base + relocations.VirtualAddress + consumed);
      Require(block->SizeOfBlock >= sizeof(IMAGE_BASE_RELOCATION) &&
                  block->SizeOfBlock <= relocations.Size - consumed &&
                  (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) % 2 == 0,
              "Invalid relocation block size.");
      const auto *entries = reinterpret_cast<const WORD *>(block + 1);
      const size_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);

      for (size_t index = 0; index < count; ++index)
      {
        const WORD type = entries[index] >> 12;

        if (type == IMAGE_REL_BASED_ABSOLUTE)
        {
          continue;
        }

        Require(type == IMAGE_REL_BASED_HIGHLOW, "Unexpected relocation type.");
        const DWORD target = block->VirtualAddress + (entries[index] & 0xfff);
        Require(static_cast<uint64_t>(target) + sizeof(DWORD) <= imageSize,
                "Relocation target outside image.");
        *reinterpret_cast<DWORD *>(base + target) += delta;
      }

      consumed += block->SizeOfBlock;
    }

    FlushInstructionCache(GetCurrentProcess(), base, imageSize);
  }

  ~MappedImage()
  {
    if (base)
    {
      VirtualFree(base, 0, MEM_RELEASE);
    }
  }
};

#endif // HAVGAMEPATCHER_TESTS_SUPPORT_MAPPEDIMAGE_HPP
