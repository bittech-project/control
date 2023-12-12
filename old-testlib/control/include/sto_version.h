#ifndef _STO_VERSION_H_
#define _STO_VERSION_H_

/**
 * Major version number.
 */
#define STO_VERSION_MAJOR	1

/**
 * Minor version number.
 */
#define STO_VERSION_MINOR	0

/**
 * Patch level.
 *
 * Patch level is incremented on maintenance branch releases and reset to 0 for each
 * new major.minor release.
 */
#define STO_VERSION_PATCH	0

/**
 * Version string suffix.
 */
#define STO_VERSION_SUFFIX	"-pre"

/**
 * Single numeric value representing a version number for compile-time comparisons.
 *
 * Example usage:
 *
 * \code
 * #if STO_VERSION >= STO_VERSION_NUM(17, 7, 0)
 *   Use feature from STO v17.07
 * #endif
 * \endcode
 */
#define STO_VERSION_NUM(major, minor, patch) \
	(((major) * 100 + (minor)) * 100 + (patch))

/**
 * Current version as a STO_VERSION_NUM.
 */
#define STO_VERSION	STO_VERSION_NUM(STO_VERSION_MAJOR, STO_VERSION_MINOR, STO_VERSION_PATCH)

#define STO_VERSION_STRINGIFY_x(x)	#x
#define STO_VERSION_STRINGIFY(x)	STO_VERSION_STRINGIFY_x(x)

#define STO_VERSION_MAJOR_STRING	STO_VERSION_STRINGIFY(STO_VERSION_MAJOR)

#if STO_VERSION_MINOR < 10
#define STO_VERSION_MINOR_STRING	".0" STO_VERSION_STRINGIFY(STO_VERSION_MINOR)
#else
#define STO_VERSION_MINOR_STRING	"." STO_VERSION_STRINGIFY(STO_VERSION_MINOR)
#endif

#if STO_VERSION_PATCH != 0
#define STO_VERSION_PATCH_STRING	"." STO_VERSION_STRINGIFY(STO_VERSION_PATCH)
#else
#define STO_VERSION_PATCH_STRING	""
#endif

#ifdef STO_GIT_COMMIT
#define STO_GIT_COMMIT_STRING STO_VERSION_STRINGIFY(STO_GIT_COMMIT)
#define STO_GIT_COMMIT_STRING_SHA1 "-" STO_GIT_COMMIT_STRING
#else
#define STO_GIT_COMMIT_STRING ""
#define STO_GIT_COMMIT_STRING_SHA1 ""
#endif

/**
 * Human-readable version string.
 */
#define STO_VERSION_STRING		\
	"STO Control v"			\
	STO_VERSION_MAJOR_STRING	\
	STO_VERSION_MINOR_STRING	\
	STO_VERSION_PATCH_STRING	\
	STO_VERSION_SUFFIX		\
	STO_GIT_COMMIT_STRING_SHA1

#endif /* _STO_VERSION_H_ */
