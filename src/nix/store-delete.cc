#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-cast.hh"
#include "nix/store/gc-store.hh"
#include "nix/util/file-system.hh"

#include <filesystem>
#include <unordered_set>

namespace nix {

struct CmdStoreDelete : StorePathsCommand
{
    GCOptions options{.action = GCOptions::gcDeleteSpecific};
    bool deleteReferrers = false;
    bool unlink = false;

    CmdStoreDelete()
    {
        addFlag({
            .longName = "ignore-liveness",
            .description = "Do not check whether the paths are reachable from a root.",
            .handler = {&options.ignoreLiveness, true},
        });

        addFlag({
            .longName = "skip-alive",
            .aliases = {"skip-live"},
            .description =
                "Do not emit errors when attempting to delete something that is still alive, useful with --recursive.",
            .handler = {&options.action, GCOptions::gcDeleteDead},
        });

        addFlag({
            .longName = "also-referrers",
            .description = "Also allow deletion of any referrers of the specified paths.",
            .handler = {&deleteReferrers, true},
        });
        addFlag({
            .longName = "unlink",
            .description = "Unlink specified GC roots before deleting them.",
            .handler = {&unlink, true},
        });
        realiseMode = Realise::Nothing;
    }

    std::string description() override
    {
        return "delete paths from the Nix store";
    }

    std::string doc() override
    {
        return
#include "store-delete.md"
            ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        auto & gcStore = require<GcStore>(*store);

        if (unlink) {
            for (const auto & path : parsePathsToUnlink(store))
                deletePath(path);
        }

        StorePathSet paths;
        for (auto & path : storePaths)
            paths.insert(path);
        options.pathsToDelete = GCOptions::SpecificPaths{
            .paths = std::move(paths),
            .deleteReferrers = deleteReferrers,
        };

        GCResults results;
        Finally printer([&] { printFreed(false, results); });
        gcStore.collectGarbage(options, results);
    }

    std::unordered_set<std::filesystem::path> parsePathsToUnlink(ref<Store> store) const
    {
        std::unordered_set<std::filesystem::path> pathsToUnlink;

        for (const auto & rawInstallable : this->rawInstallables) {
            if (!rawInstallable.contains('/'))
                continue;

            auto installablePath = absPath(std::filesystem::path(rawInstallable));

            if (store->isInStore(installablePath.string()))
                continue;

            std::error_code ec;
            if (!std::filesystem::is_symlink(installablePath, ec) || ec)
                continue;

            try {
                [[maybe_unused]] auto resolved = store->followLinksToStore(installablePath.string());
                pathsToUnlink.insert(installablePath);
            } catch (const BadStorePath &) {
            }
        }

        return pathsToUnlink;
    }
};

static auto rCmdStoreDelete = registerCommand2<CmdStoreDelete>({"store", "delete"});

} // namespace nix
