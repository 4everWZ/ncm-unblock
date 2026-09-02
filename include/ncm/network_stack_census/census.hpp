#pragma once

namespace ncm::network_stack_census {

// Fixed classifications for the modules this experiment is allowed to record.
// Anything outside the allowlist is counted and discarded, so a report never
// carries an arbitrary module path out of the client process.
enum class stack_class {
  other,
  winmm,
  winsock,
  winhttp,
  wininet,
  schannel,
  openssl,
  libcurl,
  cef,
  audio_render,
};

[[nodiscard]] stack_class classify_module(const wchar_t* base_name) noexcept;
[[nodiscard]] const char* stack_class_name(stack_class value) noexcept;

// Role of the calling process within the client's multi-process tree, read from
// the CEF-style `--type=` switch on its own command line.
[[nodiscard]] const char* process_role() noexcept;

// One recorded module transition.
struct census_event {
  unsigned long long elapsed_ms;
  stack_class classification;
  bool loaded;
  wchar_t base_name[64];
};

// Registers the loader notification and then snapshots the modules already
// mapped. Registration comes first on purpose: a module that loads between the
// two steps is reported twice rather than missed. Returns false when the
// notification could not be registered, in which case only the snapshot exists.
bool begin_capture() noexcept;

[[nodiscard]] unsigned recorded_event_count() noexcept;
[[nodiscard]] bool recorded_event(unsigned index, census_event* out) noexcept;
// Allowlisted transitions the fixed capacity could not hold.
[[nodiscard]] unsigned dropped_event_count() noexcept;
// Every module transition seen, including the ones outside the allowlist.
[[nodiscard]] unsigned observed_module_count() noexcept;

// Writes the current timeline. Never called from the loader callback.
bool write_report(const wchar_t* path) noexcept;

// Bootstrap body for the census-flavored proxy. Captures for a bounded window
// and rewrites the report as the timeline grows.
void run_census() noexcept;

}  // namespace ncm::network_stack_census
