// ===================================================================
// File:            version.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#include "version.h"

// --------------------------------------------------------------
// Public functions
// --------------------------------------------------------------

Version Version::parse(const String& v) {
    Version ver = {0, 0, 0};
    int first = v.indexOf('.');
    int second = v.indexOf('.', first + 1);
    if (first == -1) return ver;
    ver.major = v.substring(0, first).toInt();
    ver.minor = v.substring(first + 1, second).toInt();
    ver.patch = v.substring(second + 1).toInt();
    return ver;
}

String Version::toString() const {
    return String(major) + "." + String(minor) + "." + String(patch);
}

bool Version::operator>(const Version& o) const {
    if (major != o.major) return major > o.major;
    if (minor != o.minor) return minor > o.minor;
    return patch > o.patch;
}

bool Version::operator<(const Version& o) const {
    return o > *this;
}

bool Version::operator>=(const Version& o) const {
    return *this > o || *this == o;
}

bool Version::operator<=(const Version& o) const {
    return o >= *this;
}

bool Version::operator==(const Version& o) const {
    return major == o.major && minor == o.minor && patch == o.patch;
}

bool Version::operator!=(const Version& o) const {
    return !(*this == o);
}
