#include "nix/store/globals.hh"
#include "nix/store/machines.hh"
#include "nix/util/base-n.hh"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using testing::Contains;
using testing::ElementsAre;
using testing::Eq;
using testing::Field;
using testing::HasSubstr;
using testing::SizeIs;

namespace nix {

MATCHER_P(TOMLAuthorityMatches, authority, "")
{
    *result_listener << "where the authority of " << arg.render() << " is " << authority;
    auto * generic = std::get_if<StoreReference::Specified>(&arg.variant);
    if (!generic)
        return false;
    return generic->authority == authority;
}

#define EXPECT_MESSAGE_THROW(EXPR, EXC, MSG)           \
    EXPECT_THROW(                                      \
        {                                              \
            try {                                      \
                EXPR;                                  \
            } catch (const EXC & e) {                  \
                EXPECT_THAT(e.what(), HasSubstr(MSG)); \
                throw;                                 \
            }                                          \
        },                                             \
        EXC)

TEST(machines, getMachinesTOMLWithEmptyBuilders)
{
    settings.getWorkerSettings().builders.override("");
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(0));
}

TEST(machines, getMachinesTOMLUriOnly)
{
    settings.getWorkerSettings().builders.override(
        "[machines.scratchy]\n"
        "uri = \"ssh://nix@scratchy.labs.cs.uu.nl\"\n");
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, Eq(StoreReference::parse("ssh://nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, Eq(std::nullopt)));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, SizeIs(0)));
}

TEST(machines, getMachinesTOMLMultipleMachines)
{
    settings.getWorkerSettings().builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "[machines.itchy]\n"
        "uri = \"nix@itchy.labs.cs.uu.nl\"\n");

    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(2));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, TOMLAuthorityMatches("nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, TOMLAuthorityMatches("nix@itchy.labs.cs.uu.nl"))));
}

TEST(machines, getMachinesTOMLWithCorrectCompleteSingleBuilder)
{
    const std::string hostKey =
        "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIJYfqESaiQlOrL3Wm1Q9s9q8b4mjj2nIuyqCZub5aGPi nix@scratchy";
    std::span<const char> hostKeyBytes(hostKey.data(), hostKey.size());

    settings.getWorkerSettings().builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "system-types = [\"i686-linux\"]\n"
        "ssh-key = \"/home/nix/.ssh/id_scratchy_auto\"\n"
        "jobs = 8\n"
        "speed-factor = 3.0\n"
        "supported-features = [\"kvm\"]\n"
        "mandatory-features = [\"benchmark\"]\n"
        "ssh-public-host-key = \""
        + hostKey + "\"\n");

    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, TOMLAuthorityMatches("nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("i686-linux")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, Eq("/home/nix/.ssh/id_scratchy_auto")));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(8)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(3.0f)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, ElementsAre("kvm")));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, ElementsAre("benchmark")));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, Eq(base64::encode(std::as_bytes(hostKeyBytes)))));
}

TEST(machines, getMachinesTOMLBothFloatFormats)
{
    settings.getWorkerSettings().builders.override(
        "[machines.andesite]\n"
        "uri = \"ssh://lix@andesite.lix.systems\"\n"
        "speed-factor = 3\n");
    auto actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(3.0f)));

    settings.getWorkerSettings().builders.override(
        "[machines.diorite]\n"
        "uri = \"ssh://lix@diorite.lix.systems\"\n"
        "speed-factor = 3.1\n");
    actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(3.1f)));
}

TEST(machines, getMachinesTOMLWithMultiOptions)
{
    settings.getWorkerSettings().builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "system-types = [\"Arch1\", \"Arch2\"]\n"
        "supported-features = [\"SupportedFeature1\", \"SupportedFeature2\"]\n"
        "mandatory-features = [\"MandatoryFeature1\", \"MandatoryFeature2\"]\n");

    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, TOMLAuthorityMatches("nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("Arch1", "Arch2")));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, ElementsAre("SupportedFeature1", "SupportedFeature2")));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, ElementsAre("MandatoryFeature1", "MandatoryFeature2")));
}

TEST(machines, getMachinesTOMLExtraKeys)
{
    settings.getWorkerSettings().builders.override(
        "[machines.andesite]\n"
        "uri = \"ssh://lix@andesite.lix.systems\"\n"
        "extra-key = 3\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "unexpected key `extra-key`");
}

TEST(machines, getMachinesTOMLWithIncorrectTyping)
{
    settings.getWorkerSettings().builders.override("[machines.a]\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "uri must be present");

    settings.getWorkerSettings().builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "jobs = -3\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "jobs must be >= 0");

    settings.getWorkerSettings().builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "jobs = \"three\"\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad_cast to integer");

    settings.getWorkerSettings().builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "jobs = 8\n"
        "speed-factor = -3.0\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "speed factor must be >= 0");

    settings.getWorkerSettings().builders.override(
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n"
        "jobs = 8\n"
        "speed-factor = \"three\"\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad_cast to floating");

    settings.getWorkerSettings().builders.override(
        "[[machines]]\n"
        "uri = \"lix@andesite.lix.systems\"\n"
        "[[machines]]\n"
        "uri = \"lix@diorite.lix.systems\"\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "Expected key `machines` to be a table");

    settings.getWorkerSettings().builders.override("machines.a = \"lix@andesite.lix.systems\"\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "Each machine must be a table");

    settings.getWorkerSettings().builders.override(
        "version = \"1\"\n"
        "[machines.scratchy]\n"
        "uri = \"nix@scratchy.labs.cs.uu.nl\"\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad_cast to integer");

    settings.getWorkerSettings().builders.override(
        "version = 1\n"
        "[machines.legacy]\n"
        "uri = \"ssh://nix@nix-15-11.nixos.org\"\n"
        "enable = 0\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad_cast to boolean");
}

TEST(machines, getMachinesTOMLBadVersion)
{
    settings.getWorkerSettings().builders.override(
        "version = \"hello\"\n"
        "machines = {}\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "bad_cast to integer");
}

TEST(machines, getMachinesTOMLTooHighVersion)
{
    settings.getWorkerSettings().builders.override(
        "version = 42\n"
        "machines = {}\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "Unable to parse Machines of version 42");
}

TEST(machines, getMachinesTOMLTooLowVersion)
{
    settings.getWorkerSettings().builders.override(
        "version = -1\n"
        "machines = {}\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "Unable to parse Machines of version -1");
}

TEST(machines, getMachinesTOMLInvalidSyntaxButClearlyTOML)
{
    settings.getWorkerSettings().builders.override(
        "version = 1\n"
        "[machines]\n"
        "[machines.hello]\n"
        "uri = \"ssh://hello\"\n"
        " = 5\n");
    EXPECT_MESSAGE_THROW(getMachines(), UsageError, "invalid Machines TOML syntax:");
}

TEST(machines, getMachinesTOMLOneDisabled)
{
    settings.getWorkerSettings().builders.override(
        "version = 1\n"
        "[machines.a]\n"
        "uri = \"ssh://test\"\n"
        "enable = false\n"
        "\n"
        "[machines.b]\n"
        "uri = \"ssh://test2\"\n");

    auto actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, TOMLAuthorityMatches("test2")));
}

#undef EXPECT_MESSAGE_THROW

} // namespace nix
