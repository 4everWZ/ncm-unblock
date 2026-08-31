#include "ncm/runtime_probe/pe_image.hpp"

#include <Windows.h>
#include <Softpub.h>
#include <Wintrust.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace ncm::runtime_probe {
namespace {

class image_reader {
 public:
  explicit image_reader(const std::filesystem::path& path) {
    handle_ = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("unable to open image");
    }

    try {
      LARGE_INTEGER size{};
      constexpr long long maximum_image_size = 1024LL * 1024LL * 1024LL;
      if (!GetFileSizeEx(handle_, &size) || size.QuadPart <= 0 ||
          size.QuadPart > maximum_image_size) {
        throw std::runtime_error("invalid or unsupported image size");
      }

      bytes_.resize(static_cast<std::size_t>(size.QuadPart));
      DWORD bytes_read{};
      if (!ReadFile(
              handle_, bytes_.data(), static_cast<DWORD>(bytes_.size()),
              &bytes_read, nullptr) || bytes_read != static_cast<DWORD>(bytes_.size())) {
        throw std::runtime_error("unable to read image");
      }
    } catch (...) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
      throw;
    }
  }

  ~image_reader() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }

  image_reader(const image_reader&) = delete;
  image_reader& operator=(const image_reader&) = delete;

  template <typename T>
  [[nodiscard]] T read(std::size_t offset) const {
    if (offset > bytes_.size() || sizeof(T) > bytes_.size() - offset) {
      throw std::runtime_error("truncated PE structure");
    }
    T value{};
    std::memcpy(&value, bytes_.data() + offset, sizeof(T));
    return value;
  }

  [[nodiscard]] std::string read_ascii_z(std::size_t offset, std::size_t maximum_length) const {
    if (offset >= bytes_.size() || maximum_length > bytes_.size() - offset) {
      throw std::runtime_error("invalid PE string offset");
    }

    const auto begin = reinterpret_cast<const char*>(bytes_.data() + offset);
    const auto terminator = static_cast<const char*>(std::memchr(begin, '\0', maximum_length));
    if (terminator == nullptr) {
      throw std::runtime_error("unterminated PE string");
    }
    return {begin, terminator};
  }

  [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
  [[nodiscard]] HANDLE handle() const noexcept { return handle_; }

 private:
  HANDLE handle_{INVALID_HANDLE_VALUE};
  std::vector<std::byte> bytes_;
};

struct parsed_headers {
  IMAGE_FILE_HEADER file_header{};
  std::uint16_t optional_magic{};
  std::uint64_t image_base{};
  std::uint32_t size_of_headers{};
  IMAGE_DATA_DIRECTORY import_directory{};
  IMAGE_DATA_DIRECTORY delay_import_directory{};
  std::vector<IMAGE_SECTION_HEADER> sections;
};

struct delay_import_descriptor {
  std::uint32_t attributes;
  std::uint32_t name;
  std::uint32_t module_handle;
  std::uint32_t import_address_table;
  std::uint32_t import_name_table;
  std::uint32_t bound_import_address_table;
  std::uint32_t unload_information_table;
  std::uint32_t timestamp;
};

[[nodiscard]] parsed_headers parse_headers(const image_reader& image) {
  const auto dos = image.read<IMAGE_DOS_HEADER>(0);
  if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) {
    throw std::runtime_error("not a DOS/PE image");
  }

  const auto nt_offset = static_cast<std::size_t>(dos.e_lfanew);
  if (image.read<std::uint32_t>(nt_offset) != IMAGE_NT_SIGNATURE) {
    throw std::runtime_error("missing PE signature");
  }

  parsed_headers result;
  const auto file_header_offset = nt_offset + sizeof(std::uint32_t);
  result.file_header = image.read<IMAGE_FILE_HEADER>(file_header_offset);
  const auto optional_offset = file_header_offset + sizeof(IMAGE_FILE_HEADER);
  result.optional_magic = image.read<std::uint16_t>(optional_offset);

  std::size_t directory_offset{};
  std::uint32_t directory_count{};
  if (result.optional_magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    if (result.file_header.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32)) {
      throw std::runtime_error("PE32 optional header is truncated");
    }
    const auto optional = image.read<IMAGE_OPTIONAL_HEADER32>(optional_offset);
    result.image_base = optional.ImageBase;
    result.size_of_headers = optional.SizeOfHeaders;
    directory_count = optional.NumberOfRvaAndSizes;
    directory_offset = offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
  } else if (result.optional_magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    if (result.file_header.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64)) {
      throw std::runtime_error("PE32+ optional header is truncated");
    }
    const auto optional = image.read<IMAGE_OPTIONAL_HEADER64>(optional_offset);
    result.image_base = optional.ImageBase;
    result.size_of_headers = optional.SizeOfHeaders;
    directory_count = optional.NumberOfRvaAndSizes;
    directory_offset = offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory);
  } else {
    throw std::runtime_error("unsupported PE optional header");
  }

  if (directory_count > IMAGE_DIRECTORY_ENTRY_IMPORT) {
    result.import_directory = image.read<IMAGE_DATA_DIRECTORY>(
        optional_offset + directory_offset + IMAGE_DIRECTORY_ENTRY_IMPORT * sizeof(IMAGE_DATA_DIRECTORY));
  }
  if (directory_count > IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT) {
    result.delay_import_directory = image.read<IMAGE_DATA_DIRECTORY>(
        optional_offset + directory_offset + IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT * sizeof(IMAGE_DATA_DIRECTORY));
  }

  const auto sections_offset = optional_offset + result.file_header.SizeOfOptionalHeader;
  result.sections.reserve(result.file_header.NumberOfSections);
  for (std::uint16_t index = 0; index < result.file_header.NumberOfSections; ++index) {
    result.sections.push_back(image.read<IMAGE_SECTION_HEADER>(
        sections_offset + static_cast<std::size_t>(index) * sizeof(IMAGE_SECTION_HEADER)));
  }
  return result;
}

struct mapped_range {
  std::size_t offset;
  std::size_t size;
};

[[nodiscard]] mapped_range map_rva(
    const image_reader& image, const parsed_headers& headers, std::uint32_t rva) {
  if (rva < headers.size_of_headers) {
    if (rva >= image.size()) {
      throw std::runtime_error("header RVA is outside the image");
    }
    const auto header_end = std::min<std::size_t>(headers.size_of_headers, image.size());
    return {rva, header_end - rva};
  }

  for (const auto& section : headers.sections) {
    const auto section_size = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
    if (rva < section.VirtualAddress || rva - section.VirtualAddress >= section_size) {
      continue;
    }
    if (rva - section.VirtualAddress >= section.SizeOfRawData) {
      throw std::runtime_error("RVA maps to virtual data without file content");
    }
    const auto offset = static_cast<std::uint64_t>(section.PointerToRawData) +
        (rva - section.VirtualAddress);
    if (offset >= image.size()) {
      throw std::runtime_error("section RVA is outside the image");
    }
    const auto raw_remaining = static_cast<std::size_t>(section.SizeOfRawData -
        (rva - section.VirtualAddress));
    return {
        static_cast<std::size_t>(offset),
        std::min(raw_remaining, image.size() - static_cast<std::size_t>(offset))};
  }
  throw std::runtime_error("RVA is not mapped by a PE section");
}

[[nodiscard]] std::string lowercase_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    if (character >= 'A' && character <= 'Z') {
      return static_cast<char>(character - 'A' + 'a');
    }
    return static_cast<char>(character);
  });
  return value;
}

[[nodiscard]] std::vector<std::string> read_imports(
    const image_reader& image, const parsed_headers& headers) {
  std::set<std::string> imports;
  if (headers.import_directory.VirtualAddress == 0) {
    return {};
  }

  const auto directory = map_rva(image, headers, headers.import_directory.VirtualAddress);
  if (headers.import_directory.Size > directory.size) {
    throw std::runtime_error("import directory crosses raw section data");
  }
  const auto descriptor_count = std::min<std::size_t>(
      headers.import_directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR), 4096);
  for (std::size_t index = 0; index < descriptor_count; ++index) {
    const auto descriptor = image.read<IMAGE_IMPORT_DESCRIPTOR>(
        directory.offset + index * sizeof(IMAGE_IMPORT_DESCRIPTOR));
    if (descriptor.OriginalFirstThunk == 0 && descriptor.FirstThunk == 0 && descriptor.Name == 0) {
      return {imports.begin(), imports.end()};
    }
    if (descriptor.Name == 0) {
      throw std::runtime_error("import descriptor has no DLL name");
    }
    const auto name = map_rva(image, headers, descriptor.Name);
    imports.insert(lowercase_ascii(image.read_ascii_z(name.offset, name.size)));
  }
  throw std::runtime_error("import descriptor table has no terminator");
}

[[nodiscard]] std::vector<std::string> read_delay_imports(
    const image_reader& image, const parsed_headers& headers) {
  std::set<std::string> imports;
  if (headers.delay_import_directory.VirtualAddress == 0) {
    return {};
  }

  const auto directory = map_rva(image, headers, headers.delay_import_directory.VirtualAddress);
  if (headers.delay_import_directory.Size > directory.size) {
    throw std::runtime_error("delay import directory crosses raw section data");
  }
  const auto descriptor_count = std::min<std::size_t>(
      headers.delay_import_directory.Size / sizeof(delay_import_descriptor), 4096);
  for (std::size_t index = 0; index < descriptor_count; ++index) {
    const auto descriptor = image.read<delay_import_descriptor>(
        directory.offset + index * sizeof(delay_import_descriptor));
    if (descriptor.attributes == 0 && descriptor.name == 0 && descriptor.import_address_table == 0) {
      return {imports.begin(), imports.end()};
    }
    if (descriptor.name == 0) {
      throw std::runtime_error("delay import descriptor has no DLL name");
    }

    std::uint64_t name_rva = descriptor.name;
    if ((descriptor.attributes & 1U) == 0) {
      if (name_rva < headers.image_base) {
        throw std::runtime_error("invalid delay import name address");
      }
      name_rva -= headers.image_base;
    }
    if (name_rva > std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("delay import name RVA is too large");
    }
    const auto name = map_rva(image, headers, static_cast<std::uint32_t>(name_rva));
    imports.insert(lowercase_ascii(image.read_ascii_z(name.offset, name.size)));
  }
  throw std::runtime_error("delay import descriptor table has no terminator");
}

[[nodiscard]] std::string read_file_version(const std::filesystem::path& path) {
  DWORD ignored{};
  const auto size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
  if (size == 0) {
    return {};
  }

  std::vector<std::byte> buffer(size);
  if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) {
    return {};
  }

  struct translation {
    WORD language;
    WORD code_page;
  };

  translation* translations{};
  UINT translations_size{};
  if (VerQueryValueW(
          buffer.data(), L"\\VarFileInfo\\Translation",
          reinterpret_cast<void**>(&translations), &translations_size) &&
      translations != nullptr && translations_size >= sizeof(translation)) {
    wchar_t query[64]{};
    if (swprintf_s(
            query, L"\\StringFileInfo\\%04x%04x\\FileVersion",
            translations[0].language, translations[0].code_page) > 0) {
      wchar_t* value{};
      UINT value_size{};
      if (VerQueryValueW(buffer.data(), query, reinterpret_cast<void**>(&value), &value_size) &&
          value != nullptr && value_size > 1) {
        const auto required = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
        if (required > 1) {
          std::string version(static_cast<std::size_t>(required), '\0');
          if (WideCharToMultiByte(
                  CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                  version.data(), required, nullptr, nullptr) == required) {
            version.pop_back();
            return version;
          }
        }
      }
    }
  }

  VS_FIXEDFILEINFO* fixed_info{};
  UINT fixed_info_size{};
  if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&fixed_info), &fixed_info_size) ||
      fixed_info == nullptr || fixed_info_size < sizeof(VS_FIXEDFILEINFO)) {
    return {};
  }
  return std::to_string(HIWORD(fixed_info->dwFileVersionMS)) + "." +
      std::to_string(LOWORD(fixed_info->dwFileVersionMS)) + "." +
      std::to_string(HIWORD(fixed_info->dwFileVersionLS)) + "." +
      std::to_string(LOWORD(fixed_info->dwFileVersionLS));
}

[[nodiscard]] signature_info verify_signature(const std::filesystem::path& path, HANDLE image_handle) {
  WINTRUST_FILE_INFO file_info{};
  file_info.cbStruct = sizeof(file_info);
  file_info.pcwszFilePath = path.c_str();
  file_info.hFile = image_handle;

  WINTRUST_DATA trust_data{};
  trust_data.cbStruct = sizeof(trust_data);
  trust_data.dwUIChoice = WTD_UI_NONE;
  trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
  trust_data.dwUnionChoice = WTD_CHOICE_FILE;
  trust_data.pFile = &file_info;
  trust_data.dwStateAction = WTD_STATEACTION_VERIFY;
  trust_data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

  GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  const auto status = WinVerifyTrust(nullptr, &policy, &trust_data);
  trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
  static_cast<void>(WinVerifyTrust(nullptr, &policy, &trust_data));
  return {status == ERROR_SUCCESS, status};
}

}  // namespace

pe_image_info inspect_pe_image(const std::filesystem::path& path) {
  const image_reader image(path);
  const auto headers = parse_headers(image);

  pe_image_info result;
  result.machine = headers.file_header.Machine;
  result.pe32_plus = headers.optional_magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
  result.file_version = read_file_version(path);
  result.signature = verify_signature(path, image.handle());
  result.imports = read_imports(image, headers);
  result.delay_imports = read_delay_imports(image, headers);
  return result;
}

std::string machine_name(std::uint16_t machine) {
  switch (machine) {
    case IMAGE_FILE_MACHINE_I386:
      return "x86";
    case IMAGE_FILE_MACHINE_AMD64:
      return "x64";
    case IMAGE_FILE_MACHINE_ARM64:
      return "arm64";
    default:
      return "unknown";
  }
}

}  // namespace ncm::runtime_probe
