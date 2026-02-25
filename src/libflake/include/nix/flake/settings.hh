#pragma once
///@file

#include <sys/types.h>
#include <string>

#include "nix/util/configuration.hh"

namespace nix {
// Forward declarations
struct EvalSettings;

} // namespace nix

namespace nix::flake {

enum class AcceptFlakeConfig { False, Ask, True };

} // namespace nix::flake

namespace nix {

template<>
flake::AcceptFlakeConfig BaseSetting<flake::AcceptFlakeConfig>::parse(const std::string & str) const;
template<>
std::string BaseSetting<flake::AcceptFlakeConfig>::to_string() const;

} // namespace nix

namespace nix::flake {

struct Settings : public Config
{
    Settings();

    void configureEvalSettings(nix::EvalSettings & evalSettings) const;

    Setting<bool> useRegistries{
        this, true, "use-registries", "Whether to use flake registries to resolve flake references.", {}, true};

    Setting<AcceptFlakeConfig> acceptFlakeConfig{
        this,
        AcceptFlakeConfig::Ask,
        "accept-flake-config",
        R"(
          Whether to accept Nix configuration from the `nixConfig` attribute of
          a flake.

          If set to `true`, such configuration will be accepted without asking;
          this is almost always a very bad idea. Setting this to `ask` will
          prompt the user each time whether to allow a certain configuration
          option set this way, and offer to optionally remember their choice.
          When set to `false`, the configuration will be automatically
          declined.
        )",
        {},
        true};

    Setting<std::string> commitLockFileSummary{
        this,
        "",
        "commit-lock-file-summary",
        R"(
          The commit summary to use when committing changed flake lock files. If
          empty, the summary is generated based on the action performed.
        )",
        {"commit-lockfile-summary"},
        true};
};

} // namespace nix::flake
