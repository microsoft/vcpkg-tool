var path = require('path')

exports.ONE_FILE_TAR = path.join(__dirname, 'one-file.tar')
exports.MULTI_FILE_TAR = path.join(__dirname, 'multi-file.tar')
exports.PAX_TAR = path.join(__dirname, 'pax.tar')
exports.TYPES_TAR = path.join(__dirname, 'types.tar')
exports.LONG_NAME_TAR = path.join(__dirname, 'long-name.tar')
exports.UNICODE_BSD_TAR = path.join(__dirname, 'unicode-bsd.tar')
exports.UNICODE_TAR = path.join(__dirname, 'unicode.tar')
exports.NAME_IS_100_TAR = path.join(__dirname, 'name-is-100.tar')
exports.INVALID_TGZ = path.join(__dirname, 'invalid.tgz')
exports.SPACE_TAR_GZ = path.join(__dirname, 'space.tar')
exports.GNU_LONG_PATH = path.join(__dirname, 'gnu-long-path.tar')
exports.BASE_256_UID_GID = path.join(__dirname, 'base-256-uid-gid.tar')
exports.LARGE_UID_GID = path.join(__dirname, 'large-uid-gid.tar')
exports.BASE_256_SIZE = path.join(__dirname, 'base-256-size.tar')
exports.HUGE = path.join(__dirname, 'huge.tar.gz')
exports.LATIN1_TAR = path.join(__dirname, 'latin1.tar')
exports.INCOMPLETE_TAR = path.join(__dirname, 'incomplete.tar')
// Created using gnu tar: tar cf gnu-incremental.tar --format gnu --owner=myuser:12345 --group=mygroup:67890 test.txt
exports.GNU_TAR = path.join(__dirname, 'gnu.tar')
// Created using gnu tar: tar cf gnu-incremental.tar -G --format gnu --owner=myuser:12345 --group=mygroup:67890 test.txt
exports.GNU_INCREMENTAL_TAR = path.join(__dirname, 'gnu-incremental.tar')
// Created from multi-file.tar, removing the magic and recomputing the checksum
exports.UNKNOWN_FORMAT = path.join(__dirname, 'unknown-format.tar')
// Created using gnu tar: tar cf v7.tar --format v7 test.txt
exports.V7_TAR = path.join(__dirname, 'v7.tar')
