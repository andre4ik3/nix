#include "nix/cmd/command.hh"
#include "nix/store/binary-cache-store.hh"
#include "nix/store/store-api.hh"
#include "nix/util/nar-accessor.hh"
#include "nix/main/common-args.hh"
#include <nlohmann/json.hpp>

#include "ls.hh"

namespace nix {

static NarListing parseCachedNarListing(const nlohmann::json & json)
{
    auto listing = json.get<NarListing>();

    [&](this const auto & recurse, const NarListing & current) -> void {
        std::visit(
            overloaded{
                [&](const NarListing::Regular & regular) {
                    if (!regular.contents.narOffset)
                        throw Error("nar listing entry is missing narOffset");
                },
                [&](const NarListing::Directory & directory) {
                    for (const auto & entry : directory.entries)
                        recurse(entry.second);
                },
                [&](const NarListing::Symlink &) {},
            },
            current.raw);
    }(listing);

    return listing;
}

struct MixLs : virtual Args, MixJSON, MixLongListing
{
    bool recursive = false;
    bool showDirectory = false;

    MixLs()
    {
        addFlag({
            .longName = "recursive",
            .shortName = 'R',
            .description = "List subdirectories recursively.",
            .handler = {&recursive, true},
        });

        addFlag({
            .longName = "directory",
            .shortName = 'd',
            .description = "Show directories rather than their contents.",
            .handler = {&showDirectory, true},
        });
    }

    void listText(ref<SourceAccessor> accessor, CanonPath path)
    {
        std::function<void(const SourceAccessor::Stat &, const CanonPath &, std::string_view, bool)> doPath;

        auto showFile = [&](const CanonPath & curPath, std::string_view relPath) {
            if (longListing) {
                auto st = accessor->lstat(curPath);
                std::string tp = st.type == SourceAccessor::Type::tRegular
                                     ? (st.isExecutable ? "-r-xr-xr-x" : "-r--r--r--")
                                 : st.type == SourceAccessor::Type::tSymlink ? "lrwxrwxrwx"
                                                                             : "dr-xr-xr-x";
                auto line = fmt("%s %9d %s", tp, st.fileSize.value_or(0), relPath);
                if (st.type == SourceAccessor::Type::tSymlink)
                    line += " -> " + accessor->readLink(curPath);
                logger->cout(line);
                if (recursive && st.type == SourceAccessor::Type::tDirectory)
                    doPath(st, curPath, relPath, false);
            } else {
                logger->cout(relPath);
                if (recursive) {
                    auto st = accessor->lstat(curPath);
                    if (st.type == SourceAccessor::Type::tDirectory)
                        doPath(st, curPath, relPath, false);
                }
            }
        };

        doPath = [&](const SourceAccessor::Stat & st,
                     const CanonPath & curPath,
                     std::string_view relPath,
                     bool showDirectory) {
            if (st.type == SourceAccessor::Type::tDirectory && !showDirectory) {
                auto names = accessor->readDirectory(curPath);
                for (auto & [name, type] : names)
                    showFile(curPath / name, relPath + "/" + name);
            } else
                showFile(curPath, relPath);
        };

        auto st = accessor->lstat(path);
        doPath(
            st, path, st.type == SourceAccessor::Type::tDirectory ? "." : path.baseName().value_or(""), showDirectory);
    }

    void list(ref<SourceAccessor> accessor, CanonPath path)
    {
        if (json) {
            if (showDirectory)
                throw UsageError("'--directory' is useless with '--json'");
            nlohmann::json j;
            if (recursive)
                j = listNarDeep(*accessor, path);
            else
                j = listNarShallow(*accessor, path);
            logger->cout("%s", j.dump());
        } else
            listText(accessor, std::move(path));
    }
};

struct CmdLsStore : StoreCommand, MixLs
{
    std::string path;

    CmdLsStore()
    {
        expectArgs({.label = "path", .handler = {&path}, .completer = completePath});
    }

    std::string description() override
    {
        return "show information about a path in the Nix store";
    }

    std::string doc() override
    {
        return
#include "store-ls.md"
            ;
    }

    void run(ref<Store> store) override
    {
        auto [storePath, rest] = store->toStorePath(path);
        std::shared_ptr<SourceAccessor> accessor;

        if (auto binaryCacheStore = store.dynamic_pointer_cast<BinaryCacheStore>()) {
            auto warnBadListing = [&](std::string_view msg) {
                warn(
                    "nar listing for %s on %s is bad (falling back to full nar download): %s",
                    store->printStorePath(storePath),
                    binaryCacheStore->config.getHumanReadableURI(),
                    msg);
            };
            try {
                if (auto file = binaryCacheStore->getFile(fmt("%s.ls", storePath.hashPart()))) {
                    auto listing = nlohmann::json::parse(*file, nullptr, true, true);
                    const auto * root = &listing;
                    if (listing.contains("root"))
                        root = &listing["root"];

                    if (root->is_object() && root->contains("type"))
                        accessor = makeLazyNarAccessor(parseCachedNarListing(*root), [](uint64_t, uint64_t, Sink &) {
                                       throw Error("attempted to read NAR content during listing");
                                   }).get_ptr();
                }
            } catch (NoSuchBinaryCacheFile &) {
            } catch (Error & e) {
                warnBadListing(e.what());
            } catch (const nlohmann::json::exception & e) {
                warnBadListing(e.what());
            }
        }

        if (accessor)
            list(ref<SourceAccessor>(accessor), rest);
        else
            list(store->requireStoreObjectAccessor(storePath), rest);
    }
};

struct CmdLsNar : Command, MixLs
{
    std::filesystem::path narPath;

    std::string path;

    CmdLsNar()
    {
        expectArgs({.label = "nar", .handler = {&narPath}, .completer = completePath});
        expectArg("path", &path);
    }

    std::string doc() override
    {
        return
#include "nar-ls.md"
            ;
    }

    std::string description() override
    {
        return "show information about a path inside a NAR file";
    }

    void run() override
    {
        auto fd = openFileReadonly(narPath);
        if (!fd)
            throw NativeSysError("opening NAR file %s", PathFmt(narPath));
        auto source = FdSource{fd.get()};
        list(makeLazyNarAccessor(parseNarListing(source), seekableGetNarBytes(fd.get())), CanonPath{path});
    }
};

static auto rCmdLsStore = registerCommand2<CmdLsStore>({"store", "ls"});
static auto rCmdLsNar = registerCommand2<CmdLsNar>({"nar", "ls"});

} // namespace nix
