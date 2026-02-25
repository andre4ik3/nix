#pragma once
///@file

#include <limits>
#include <optional>
#include <string>

#include "nix/util/file-descriptor.hh"

namespace nix {

/**
 * Determine whether \param fd is a terminal.
 */
bool isTTY(Descriptor fd);

/**
 * Determine whether ANSI escape sequences are appropriate for the
 * present output.
 */
bool isTTY();

/**
 * Truncate a string to 'width' printable characters. If 'filterAll'
 * is true, all ANSI escape sequences are filtered out. Otherwise,
 * some escape sequences (such as colour setting) are copied but not
 * included in the character count. Also, tabs are expanded to
 * spaces.
 */
std::string filterANSIEscapes(
    std::string_view s, bool filterAll = false, unsigned int width = std::numeric_limits<unsigned int>::max());

/**
 * Recalculate the window size, updating a global variable.
 *
 * Used in the `SIGWINCH` signal handler on Unix, for example.
 */
void updateWindowSize();

/**
 * @return the number of rows and columns of the terminal.
 *
 * The value is cached so this is quick. The cached result is computed
 * by `updateWindowSize()`.
 */
std::pair<unsigned short, unsigned short> getWindowSize();

/**
 * Makes a terminal hyperlink using OSC 8.
 *
 * If the link target is too long (700 bytes is the current limit), the link is
 * skipped and the link text is emitted as-is. This limits the maximum amount
 * of context required to a manageable amount that doesn't break any terminals.
 *
 * See: https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda
 *
 * @see makeHyperlinkLocalPath
 */
std::string makeHyperlink(std::string_view linkText, std::string_view target);

/**
 * Creates an OSC 8 compliant `file://` path for a given filesystem path.
 */
std::string makeHyperlinkLocalPath(std::string_view path, std::optional<unsigned> lineNumber = std::nullopt);

/**
 * @return The number of columns of the terminal, or std::numeric_limits<unsigned int>::max() if unknown.
 */
unsigned int getWindowWidth();

/**
 * Get the slave name of a pseudoterminal in a thread-safe manner.
 *
 * @param fd The file descriptor of the pseudoterminal master
 * @return The slave device name as a string
 */
std::string getPtsName(int fd);

} // namespace nix
