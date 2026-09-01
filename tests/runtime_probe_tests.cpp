#include "ncm/runtime_probe/pe_image.hpp"
#include "ncm/runtime_probe/process_snapshot.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path current_executable() {
  std::wstring buffer(32768, L'\0');
  const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size == 0 || size >= buffer.size()) {
    throw std::runtime_error("GetModuleFileNameW failed");
  }
  buffer.resize(size);
  return buffer;
}

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class temporary_file {
 public:
  explicit temporary_file(std::wstring_view label)
      : path_(std::filesystem::temp_directory_path() /
              (L"ncm-runtime-probe-" + std::wstring(label) + L"-" +
               std::to_wstring(GetCurrentProcessId()) + L".bin")) {}

  ~temporary_file() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  temporary_file(const temporary_file&) = delete;
  temporary_file& operator=(const temporary_file&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  void write(const std::vector<std::byte>& bytes) const {
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) {
      throw std::runtime_error("unable to write PE test fixture");
    }
  }

 private:
  std::filesystem::path path_;
};

template <typename T>
void write_at(std::vector<std::byte>& bytes, std::size_t offset, const T& value) {
  require(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset,
          "PE test fixture write is out of range");
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

[[nodiscard]] std::vector<std::byte> make_pe_fixture(
    std::uint32_t directory_count, bool cross_section_name) {
  constexpr std::size_t nt_offset = 0x80;
  constexpr std::size_t headers_size = 0x200;
  constexpr std::size_t section_size = 0x200;
  constexpr std::uint32_t section_rva = 0x1000;

  std::vector<std::byte> bytes(headers_size + section_size + 1);

  IMAGE_DOS_HEADER dos{};
  dos.e_magic = IMAGE_DOS_SIGNATURE;
  dos.e_lfanew = nt_offset;
  write_at(bytes, 0, dos);
  write_at<std::uint32_t>(bytes, nt_offset, IMAGE_NT_SIGNATURE);

  IMAGE_FILE_HEADER file_header{};
  file_header.Machine = IMAGE_FILE_MACHINE_I386;
  file_header.NumberOfSections = 1;
  file_header.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
  const auto file_header_offset = nt_offset + sizeof(std::uint32_t);
  write_at(bytes, file_header_offset, file_header);

  IMAGE_OPTIONAL_HEADER32 optional{};
  optional.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
  optional.ImageBase = 0x400000;
  optional.SizeOfHeaders = headers_size;
  optional.NumberOfRvaAndSizes = directory_count;
  optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = section_rva;
  optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size =
      2 * sizeof(IMAGE_IMPORT_DESCRIPTOR);
  const auto optional_offset = file_header_offset + sizeof(IMAGE_FILE_HEADER);
  write_at(bytes, optional_offset, optional);

  IMAGE_SECTION_HEADER section{};
  section.Misc.VirtualSize = section_size;
  section.VirtualAddress = section_rva;
  section.SizeOfRawData = section_size;
  section.PointerToRawData = headers_size;
  write_at(bytes, optional_offset + sizeof(optional), section);

  IMAGE_IMPORT_DESCRIPTOR descriptor{};
  descriptor.Name = cross_section_name
      ? section_rva + static_cast<std::uint32_t>(section_size - 1)
      : section_rva + 0x40;
  write_at(bytes, headers_size, descriptor);

  if (cross_section_name) {
    bytes[headers_size + section_size - 1] = std::byte{'x'};
    bytes[headers_size + section_size] = std::byte{0};
  } else {
    constexpr char module_name[] = "TestDLL.DLL";
    std::memcpy(bytes.data() + headers_size + 0x40, module_name, sizeof(module_name));
  }
  return bytes;
}

void test_current_image() {
  const auto path = current_executable();
  const auto info = ncm::runtime_probe::inspect_pe_image(path);
  require(info.machine == IMAGE_FILE_MACHINE_I386, "test executable is not Win32");
  require(!info.pe32_plus, "Win32 test executable reported PE32+");
  require(std::ranges::find(info.imports, "kernel32.dll") != info.imports.end(),
          "kernel32.dll import was not parsed");
}

void test_invalid_image() {
  const temporary_file file(L"invalid");
  {
    std::ofstream output(file.path(), std::ios::binary | std::ios::trunc);
    output << "not a PE image";
  }

  bool rejected = false;
  try {
    static_cast<void>(ncm::runtime_probe::inspect_pe_image(file.path()));
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "invalid image was accepted");
}

void test_directory_count_controls_imports() {
  const temporary_file file(L"directory-count");
  file.write(make_pe_fixture(0, false));

  const auto info = ncm::runtime_probe::inspect_pe_image(file.path());
  require(info.imports.empty(), "imports outside NumberOfRvaAndSizes were parsed");
}

void test_import_name_cannot_cross_raw_section_data() {
  const temporary_file file(L"cross-section-name");
  file.write(make_pe_fixture(IMAGE_NUMBEROF_DIRECTORY_ENTRIES, true));

  bool rejected = false;
  try {
    static_cast<void>(ncm::runtime_probe::inspect_pe_image(file.path()));
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "import name crossed the raw section boundary");
}

void test_process_snapshot() {
  const auto path = current_executable();
  const auto processes = ncm::runtime_probe::find_processes(path.filename().wstring());
  const auto current_id = GetCurrentProcessId();
  const auto current = std::ranges::find_if(processes, [current_id](const auto& process) {
    return process.process_id == current_id;
  });
  require(current != processes.end(), "current process was not found in the snapshot");
  require(current->ipv4_tcp_connections.has_value(), "TCP ownership collection was unavailable");
  require(current->network_modules_complete, "module collection was incomplete");
}

}  // namespace

int main() {
  try {
    test_current_image();
    test_invalid_image();
    test_directory_count_controls_imports();
    test_import_name_cannot_cross_raw_section_data();
    test_process_snapshot();
    std::cout << "runtime probe tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
