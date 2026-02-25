#include "nix/util/terminal.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/sync.hh"
#include "nix/util/error.hh"
#include "nix/util/fmt.hh"
#include "nix/util/url.hh"

#ifdef _WIN32
#  include <io.h>
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  define isatty _isatty
#else
#  include <sys/ioctl.h>
#endif
#include <unistd.h>
#include <widechar_width.h>
#include <cstdlib> // for ptsname and ptsname_r
#include <limits.h>

namespace {

inline std::pair<int, size_t> charWidthUTF8Helper(std::string_view s)
{
    size_t bytes = 1;
    uint32_t ch = s[0];
    uint32_t max = 1U << 7;
    if ((ch & 0x80U) == 0U) {
    } else if ((ch & 0xe0U) == 0xc0U) {
        ch &= 0x1fU;
        bytes = 2;
        max = 1U << 11;
    } else if ((ch & 0xf0U) == 0xe0U) {
        ch &= 0x0fU;
        bytes = 3;
        max = 1U << 16;
    } else if ((ch & 0xf8U) == 0xf0U) {
        ch &= 0x07U;
        bytes = 4;
        max = 0x110000U;
    } else {
        return {bytes, bytes}; // invalid UTF-8 start byte
    }
    for (size_t i = 1; i < bytes; i++) {
        if (i < s.size() && (s[i] & 0xc0) == 0x80) {
            ch = (ch << 6) | (s[i] & 0x3f);
        } else {
            return {i, i}; // invalid UTF-8 encoding; assume one character per byte
        }
    }
    int width = bytes; // in case of overlong encoding
    if (ch < max) {
        width = widechar_wcwidth(ch);
        if (width == widechar_ambiguous) {
            width = 1; // just a guess...
        } else if (width == widechar_widened_in_9) {
            width = 2;
        } else if (width < 0) {
            width = 0;
        }
    }
    return {width, bytes};
}

} // namespace

namespace nix {

bool isTTY(Descriptor fd)
{
#ifndef _WIN32
    return isatty(fd);
#else
    DWORD mode;
    return GetConsoleMode(fd, &mode);
#endif
}

bool isTTY()
{
    static const bool tty = isatty(STDERR_FILENO) && getEnv("TERM").value_or("dumb") != "dumb"
                            && !(getEnv("NO_COLOR").has_value() || getEnv("NOCOLOR").has_value());

    return tty;
}

std::string filterANSIEscapes(std::string_view s, bool filterAll, unsigned int width)
{
    std::string t;
    size_t w = 0;
    auto i = s.begin();

    while (i != s.end()) {

        if (*i == '\e') {
            std::string e;
            e += *i++;
            char last = 0;

            if (i != s.end() && *i == '[') {
                e += *i++;
                // eat parameter bytes
                while (i != s.end() && *i >= 0x30 && *i <= 0x3f)
                    e += *i++;
                // eat intermediate bytes
                while (i != s.end() && *i >= 0x20 && *i <= 0x2f)
                    e += *i++;
                // eat final byte
                if (i != s.end() && *i >= 0x40 && *i <= 0x7e)
                    e += last = *i++;
            } else if (i != s.end() && *i == ']') {
                // OSC
                e += *i++;
                // https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda defines
                // two forms of a URI separator:
                // 1. ESC '\' (standard)
                // 2. BEL ('\a') (xterm-style, used by gcc)

                // eat ESC or BEL
                while (i != s.end() && *i != '\e' && *i != '\a')
                    e += *i++;
                if (i != s.end()) {
                    char v = *i;
                    e += *i++;
                    // eat backslash after ESC
                    if (i != s.end() && v == '\e' && *i == '\\')
                        e += last = *i++;
                }
            } else {
                if (i != s.end() && *i >= 0x40 && *i <= 0x5f)
                    e += *i++;
            }

            if (!filterAll && last == 'm')
                t += e;
        }

        else if (*i == '\t') {
            do {
                if (++w > (size_t) width)
                    return t;
                t += ' ';
            } while (w % 8);
            i++;
        }

        else if (*i == '\r' || *i == '\a')
            // do nothing for now
            i++;

        else {
            auto [chWidth, bytes] = charWidthUTF8Helper({i, s.end()});
            w += chWidth;
            if (w > (size_t) width) {
                break;
            }
            t += {i, i + bytes};
            i += bytes;
        }
    }
    return t;
}

//////////////////////////////////////////////////////////////////////

// Note: this object intentionally leaks to avoid a destructor ordering issue (specifically, ~ProgressBar() calling
// getWindowSize() after windowSize has been destroyed).
static auto * const windowSize = new Sync<std::pair<unsigned short, unsigned short>>{{0, 0}};

void updateWindowSize()
{
#ifndef _WIN32
    struct winsize ws;
    if (ioctl(2, TIOCGWINSZ, &ws) == 0) {
        auto windowSize_(windowSize->lock());
        windowSize_->first = ws.ws_row;
        windowSize_->second = ws.ws_col;
    }
#else
    CONSOLE_SCREEN_BUFFER_INFO info;
    // From https://stackoverflow.com/a/12642749
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) != 0) {
        auto windowSize_(windowSize->lock());
        // From https://github.com/libuv/libuv/blob/v1.48.0/src/win/tty.c#L1130
        windowSize_->first = info.srWindow.Bottom - info.srWindow.Top + 1;
        windowSize_->second = info.dwSize.X;
    }
#endif
}

std::pair<unsigned short, unsigned short> getWindowSize()
{
    return *windowSize->lock();
}

std::string makeHyperlink(std::string_view linkText, std::string_view target)
{
    // 700 is arbitrarily chosen as a length limit as it's where screen breaks
    // according to https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda#length-limits
    if (target.empty() || target.length() > 700) {
        return std::string{linkText};
    }

#define OSC "\e]"
#define ST "\e\\"

    return fmt(OSC "8;;%s" ST "%s" OSC "8;;" ST, target, linkText);

#undef OSC
#undef ST
}

std::string makeHyperlinkLocalPath(std::string_view path, std::optional<unsigned> lineNumber)
{
    // File paths in OSC 8 are required to have the hostname in them per the
    // spec.
    static std::string theHostname = []() -> std::string {
#ifndef _WIN32
        // According to POSIX if the hostname is too long, there is no guarantee of
        // null termination so let's make sure there's always one.
        char theHostname_[_POSIX_HOST_NAME_MAX + 1] = {};

        int err = gethostname(theHostname_, sizeof(theHostname_) - 1);
        // Who knows why getting the hostname would fail, but it is fallible.
        if (err < 0) {
            return "localhost";
        } else {
            return theHostname_;
        }
#else
        return "localhost";
#endif
    }();

    if (!path.starts_with('/')) {
        // Problematic to have non-absolute paths.
        return "";
    }

    auto content = percentEncode(path, "/");

    // These schemes are not standardized and even file URL line numbers are
    // not guaranteed to work across terminals/editors.
    auto result = fmt("file://%s%s", theHostname, content);
    if (lineNumber.has_value()) {
        result += fmt("#%d", *lineNumber);
    }
    return result;
}

unsigned int getWindowWidth()
{
    unsigned int width = getWindowSize().second;
    if (width <= 0)
        width = std::numeric_limits<unsigned int>::max();
    return width;
}

#ifndef _WIN32
std::string getPtsName(int fd)
{
#  ifdef __APPLE__
    static std::mutex ptsnameMutex;
    // macOS doesn't have ptsname_r, use mutex-protected ptsname
    std::lock_guard<std::mutex> lock(ptsnameMutex);
    const char * name = ptsname(fd);
    if (!name) {
        throw SysError("getting pseudoterminal slave name");
    }
    return name;
#  else
    // Use thread-safe ptsname_r on platforms that support it
    // PTY names are typically short:
    // - Linux: /dev/pts/N (where N is usually < 1000)
    // - FreeBSD: /dev/pts/N
    // 64 bytes is more than sufficient for any Unix PTY name
    char buf[64];
    if (ptsname_r(fd, buf, sizeof(buf)) != 0) {
        throw SysError("getting pseudoterminal slave name");
    }
    return buf;
#  endif
}
#endif

} // namespace nix
