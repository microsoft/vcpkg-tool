// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { unpackTar, unpackTarBz, unpackTarGz } from '../../archivers/tar';
import { FileEntry, stripPath } from '../../archivers/unpacker';
import { unpackZip } from '../../archivers/ZipUnpacker';
import { Uri } from '../../util/uri';
import { rejects, strict } from 'assert';
import { SuiteLocal } from './SuiteLocal';

const isWindows = process.platform === 'win32';

describe('Unpacker', () => {
  it('StripsPaths', () => {
    ['', '/'].forEach((prefix) => {
      ['', '/'].forEach((suffix) => {
        const d = prefix + 'delta' + suffix;
        const cd = prefix + 'charlie/delta' + suffix;
        const bcd = prefix + 'beta/charlie/delta' + suffix;
        const abcd = prefix + 'alpha/beta/charlie/delta' + suffix;
        strict.equal(stripPath(abcd, 0), abcd);
        strict.equal(stripPath(abcd, 1), bcd);
        strict.equal(stripPath(abcd, 2), cd);
        strict.equal(stripPath(abcd, 3), d);
        strict.equal(stripPath(abcd, 4), undefined);

        strict.equal(stripPath(prefix + 'some///slashes\\\\\\\\here' + suffix, 0), prefix + 'some/slashes/here' + suffix);
      });
    });
  });
});

/** Checks that progress delivers 0, 100, and constantly increasing percentages. */
class PercentageChecker {
  seenZero = false;
  lastSeen: number | undefined = undefined;
  recordPercent(percentage: number) {
    if (percentage === 0) {
      this.seenZero = true;
    }

    if (this.lastSeen !== undefined) {
      strict.ok(percentage >= this.lastSeen, `${percentage} vs ${this.lastSeen}`);
    }

    this.lastSeen = percentage;
  }

  test() {
    strict.equal(this.lastSeen, 100);
  }

  testRequireZero() {
    strict.ok(this.seenZero);
  }

  reset() {
    this.seenZero = false;
    this.lastSeen = undefined;
  }
}

class ProgressCheckerEntry {
  seenZero = false;
  seenUnpacked = false;
  filePercentage = new PercentageChecker();

  constructor(public entryPath: string, public entryIdentity: any) { }

  unpackFileProgress(entry: any, filePercentage: number) {
    strict.equal(this.entryIdentity, entry);
    if (filePercentage === 0) {
      this.seenZero = true;
    }

    this.filePercentage.recordPercent(filePercentage);
  }

  unpackFileComplete(entry: any) {
    strict.equal(this.entryIdentity, entry);
    this.seenUnpacked = true;
  }

  test() {
    strict.ok(this.seenUnpacked, 'Should have got an unpacked message');
    strict.ok(this.seenZero, 'Should have seen a zero progress');
    this.filePercentage.testRequireZero();
  }
}

class ProgressChecker {
  seenEntries = new Map<string, ProgressCheckerEntry>();
  archivePercentage = new PercentageChecker();

  unpackFileProgress(entry: any, filePercentage: number) {
    let checkerEntry = this.seenEntries.get(entry.path);
    if (!checkerEntry) {
      checkerEntry = new ProgressCheckerEntry(entry.path, entry);
      this.seenEntries.set(entry.path, checkerEntry);
    }

    checkerEntry.unpackFileProgress(entry, filePercentage);
  }

  unpackArchiveProgress(archiveUri: Uri, archivePercentage: number) {
    this.archivePercentage.recordPercent(archivePercentage);
  }

  unpackFileComplete(entry: FileEntry) {
    const checkerEntry = this.seenEntries.get(entry.path);
    strict.ok(checkerEntry, `Did not find unpack progress entries for ${entry.path}`);
    checkerEntry.unpackFileComplete(entry);
  }

  reset() {
    this.seenEntries.clear();
    this.archivePercentage.reset();
  }

  test(entryCount: number) {
    strict.equal(entryCount, this.seenEntries.size, `Should have unpacked ${entryCount}, actually unpacked ${this.seenEntries.size}`);
    this.seenEntries.forEach((value) => value.test());
    this.archivePercentage.test();
  }
}

describe('ZipUnpacker', () => {
  const local = new SuiteLocal();
  after(local.after.bind(local));
  const progressChecker = new ProgressChecker();
  it('UnpacksLegitimateSmallZips', async () => {
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example');
    await unpackZip(local.session, zipUri, targetUri, progressChecker, {});
    strict.equal((await targetUri.readFile('a.txt')).toString(), 'The contents of a.txt.\n');
    strict.equal((await targetUri.stat('a.txt')).mtime, Date.parse('2021-03-23T09:31:14.000Z'));
    strict.equal((await targetUri.readFile('b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('c.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('only-not-directory.txt')).toString(),
      'This content is only not in the directory.\n');
    strict.equal((await targetUri.readFile('a-directory/a.txt')).toString(), 'The contents of a.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/c.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/only-directory.txt')).toString(),
      'This content is only in the directory.\n');
    strict.equal((await targetUri.readFile('a-directory/inner/only-directory-directory.txt')).toString(),
      'This content is only doubly nested.\n');
    progressChecker.test(9);
  });

  it('Truncates', async () => {
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example-truncates');
    await unpackZip(local.session, zipUri, targetUri, progressChecker, {});
    progressChecker.reset();
    await unpackZip(local.session, zipUri, targetUri, progressChecker, {}); // intentionally doubled
    strict.equal((await targetUri.readFile('a.txt')).toString(), 'The contents of a.txt.\n');
    strict.equal((await targetUri.readFile('b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('c.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('only-not-directory.txt')).toString(),
      'This content is only not in the directory.\n');
    strict.equal((await targetUri.readFile('a-directory/a.txt')).toString(), 'The contents of a.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/c.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/only-directory.txt')).toString(),
      'This content is only in the directory.\n');
    strict.equal((await targetUri.readFile('a-directory/inner/only-directory-directory.txt')).toString(),
      'This content is only doubly nested.\n');
    progressChecker.test(9);
  });

  it('UnpacksZipsWithCompression', async () => {
    // big-compression.zip is an example input from yauzl:
    // https://github.com/thejoshwolfe/yauzl/blob/96f0eb552c560632a754ae0e1701a7edacbda389/test/big-compression.zip
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('big-compression.zip');
    const targetUri = local.tempFolderUri.join('big-compression');
    await unpackZip(local.session, zipUri, targetUri, progressChecker, {});
    const contents = await targetUri.readFile('0x100000');
    strict.equal(contents.length, 0x100000);
    strict.ok(contents.every((value: number) => value === 0x0));
    progressChecker.test(1);
  });

  it('FailsToUnpackMalformed', async () => {
    // wrong-entry-sizes.zip is an example input from yauzl:
    // https://github.com/thejoshwolfe/yauzl/blob/96f0eb552c560632a754ae0e1701a7edacbda389/test/wrong-entry-sizes/wrong-entry-sizes.zip
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('wrong-entry-sizes.zip');
    const targetUri = local.tempFolderUri.join('wrong-entry-sizes');
    await rejects(unpackZip(local.session, zipUri, targetUri, progressChecker, {}));
  });

  it('Strips1', async () => {
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example-strip-1');
    await unpackZip(local.session, zipUri, targetUri, progressChecker, { strip: 1 });
    strict.equal((await targetUri.readFile('a.txt')).toString(), 'The contents of a.txt.\n');
    strict.equal((await targetUri.readFile('b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('c.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('only-directory.txt')).toString(),
      'This content is only in the directory.\n');
    strict.equal((await targetUri.readFile('inner/only-directory-directory.txt')).toString(),
      'This content is only doubly nested.\n');
    progressChecker.test(5);
  });

  it('Strips2', async () => {
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example-strip-2');
    await unpackZip(local.session, zipUri, targetUri, progressChecker, { strip: 2 });
    strict.equal((await targetUri.readFile('only-directory-directory.txt')).toString(),
      'This content is only doubly nested.\n');
    progressChecker.test(1);
  });

  it('StripsAll', async () => {
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example-strip-all');
    await unpackZip(local.session, zipUri, targetUri, progressChecker, { strip: 3 });
    strict.ok(!await targetUri.exists());
    progressChecker.test(0);
  });

  it('TransformsOne', async () => {
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example-transform-one');
    await unpackZip(local.session, zipUri, targetUri, progressChecker, { transform: ['s/a\\.txt/ehh.txt/'] });
    strict.equal((await targetUri.readFile('ehh.txt')).toString(), 'The contents of a.txt.\n');
    strict.equal((await targetUri.readFile('b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('c.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('only-not-directory.txt')).toString(),
      'This content is only not in the directory.\n');
    strict.equal((await targetUri.readFile('a-directory/ehh.txt')).toString(), 'The contents of a.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/c.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/only-directory.txt')).toString(),
      'This content is only in the directory.\n');
    strict.equal((await targetUri.readFile('a-directory/inner/only-directory-directory.txt')).toString(),
      'This content is only doubly nested.\n');
    progressChecker.test(9);
  });

  it('TransformsArray', async () => {
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example-transform-array');
    await unpackZip(local.session, zipUri, targetUri, progressChecker, {
      transform: [
        's/a\\.txt/ehh.txt/',
        's/c\\.txt/see.txt/',
        's/see\\.txt/seeee.txt/',
        's/directory//g',
      ]
    });
    strict.equal((await targetUri.readFile('ehh.txt')).toString(), 'The contents of a.txt.\n');
    strict.equal((await targetUri.readFile('b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('seeee.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('only-not-.txt')).toString(),
      'This content is only not in the directory.\n');
    strict.equal((await targetUri.readFile('a-/ehh.txt')).toString(), 'The contents of a.txt.\n');
    strict.equal((await targetUri.readFile('a-/b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('a-/seeee.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('a-/only-.txt')).toString(),
      'This content is only in the directory.\n');
    strict.equal((await targetUri.readFile('a-/inner/only--.txt')).toString(),
      'This content is only doubly nested.\n');
    progressChecker.test(9);
  });

  it('StripsThenTransforms', async () => {
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example-strip-then-transform');
    await unpackZip(local.session, zipUri, targetUri, progressChecker, { strip: 1, transform: ['s/b/beeee/'] });
    strict.equal((await targetUri.readFile('a.txt')).toString(), 'The contents of a.txt.\n');
    strict.equal((await targetUri.readFile('beeee.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('c.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('only-directory.txt')).toString(),
      'This content is only in the directory.\n');
    strict.equal((await targetUri.readFile('inner/only-directory-directory.txt')).toString(),
      'This content is only doubly nested.\n');
    progressChecker.test(5);
  });

  it('AllowsTransformToNotExtract', async () => {
    progressChecker.reset();
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example-transform-no-extract');
    await unpackZip(local.session, zipUri, targetUri, progressChecker, { transform: ['s/.+a.txt$//'] });
    strict.equal((await targetUri.readFile('b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('c.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('only-not-directory.txt')).toString(),
      'This content is only not in the directory.\n');
    strict.equal((await targetUri.readFile('a-directory/b.txt')).toString(), 'The contents of b.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/c.txt')).toString(), 'The contents of c.txt.\n');
    strict.equal((await targetUri.readFile('a-directory/only-directory.txt')).toString(),
      'This content is only in the directory.\n');
    strict.equal((await targetUri.readFile('a-directory/inner/only-directory-directory.txt')).toString(),
      'This content is only doubly nested.\n');
    progressChecker.test(8);
  });
});

async function checkExtractedTar(targetUri: Uri): Promise<void> {
  strict.equal((await targetUri.readFile('a.txt')).toString(), 'The contents of a.txt.\n');
  strict.equal((await targetUri.stat('a.txt')).mtime, Date.parse('2021-03-23T09:31:14.000Z'));
  strict.equal((await targetUri.readFile('b.txt')).toString(), 'The contents of b.txt.\n');
  strict.equal((await targetUri.readFile('executable.sh')).toString(), '#/bin/sh\necho "Hello world!"\n\n');
  if (!isWindows) {
    // executable must be executable
    const execStat = await targetUri.stat('executable.sh');
    strict.ok((execStat.mode & 0o111) !== 0);
  }
  strict.equal((await targetUri.readFile('only-not-directory.txt')).toString(),
    'This content is only not in the directory.\n');
  strict.equal((await targetUri.readFile('a-directory/a.txt')).toString(), 'The contents of a.txt.\n');
  strict.equal((await targetUri.readFile('a-directory/b.txt')).toString(), 'The contents of b.txt.\n');
  strict.equal((await targetUri.readFile('a-directory/only-directory.txt')).toString(),
    'This content is only in the directory.\n');
  strict.equal((await targetUri.readFile('a-directory/inner/only-directory-directory.txt')).toString(),
    'This content is only doubly nested.\n');
}

const transformedTarUnpackOptions = {
  strip: 1,
  transform: ['s/a\\.txt/ehh\\.txt/']
};

async function checkExtractedTransformedTar(targetUri: Uri): Promise<void> {
  strict.equal((await targetUri.readFile('ehh.txt')).toString(), 'The contents of a.txt.\n');
  strict.equal((await targetUri.readFile('b.txt')).toString(), 'The contents of b.txt.\n');
  strict.equal((await targetUri.readFile('only-directory.txt')).toString(),
    'This content is only in the directory.\n');
  strict.equal((await targetUri.readFile('inner/only-directory-directory.txt')).toString(),
    'This content is only doubly nested.\n');
}

describe('TarUnpacker', () => {
  const local = new SuiteLocal();
  after(local.after.bind(local));
  const progressChecker = new ProgressChecker();
  const archiveUri = local.resourcesFolderUri.join('example-tar.tar');
  it('UnpacksLegitimateSmallTar', async () => {
    progressChecker.reset();
    const targetUri = local.tempFolderUri.join('example-tar');
    await unpackTar(local.session, archiveUri, targetUri, progressChecker, {});
    await checkExtractedTar(targetUri);
    progressChecker.test(8);
  });
  it('ImplementsUnpackOptions', async () => {
    progressChecker.reset();
    const targetUri = local.tempFolderUri.join('example-tar-transformed');
    await unpackTar(local.session, archiveUri, targetUri, progressChecker, transformedTarUnpackOptions);
    await checkExtractedTransformedTar(targetUri);
    progressChecker.test(4);
  });
});

describe('TarBzUnpacker', () => {
  const local = new SuiteLocal();
  after(local.after.bind(local));
  const progressChecker = new ProgressChecker();
  const archiveUri = local.resourcesFolderUri.join('example-tar.tar.bz2');
  it('UnpacksLegitimateSmallTarBz', async () => {
    progressChecker.reset();
    const targetUri = local.tempFolderUri.join('example-tar-bz');
    await unpackTarBz(local.session, archiveUri, targetUri, progressChecker, {});
    await checkExtractedTar(targetUri);
    progressChecker.test(8);
  });
  it('ImplementsUnpackOptions', async () => {
    progressChecker.reset();
    const targetUri = local.tempFolderUri.join('example-tar-bz2-transformed');
    await unpackTarBz(local.session, archiveUri, targetUri, progressChecker, transformedTarUnpackOptions);
    await checkExtractedTransformedTar(targetUri);
    progressChecker.test(4);
  });
});

describe('TarStripAuto', () => {
  const local = new SuiteLocal();
  after(local.after.bind(local));
  const archiveUri = local.resourcesFolderUri.join('test.directories.tar.gz');

  it('Strips off unnecessary folders off the front', async () => {
    const targetUri = local.tempFolderUri.join('test-directories-gz');
    const expected = [
      'test-directories-gz/three/test.txt',
      'test-directories-gz/four/test.txt'
    ];
    const actual = new Array<string>();
    await unpackTarGz(local.session, archiveUri, targetUri, {
      unpackFileComplete(entry) {
        if (entry.destination) {
          actual.push(entry.destination.path);
        }
      },
    }, { strip: -1 });
    strict.equal(actual.length, 2, 'Should have two entries only');
    for (const e of expected) {
      // make sure the output has each of the expected outputs
      strict.ok(actual.find((each) => each.endsWith(e)), `Should have element ending in expected value ${e}`);
    }
  });
});

describe('TarGzUnpacker', () => {
  const local = new SuiteLocal();
  after(local.after.bind(local));
  const progressChecker = new ProgressChecker();
  const archiveUri = local.resourcesFolderUri.join('example-tar.tar.gz');
  it('UnpacksLegitimateSmallTarGz', async () => {
    progressChecker.reset();
    const targetUri = local.tempFolderUri.join('example-tar-gz');
    await unpackTarGz(local.session, archiveUri, targetUri, progressChecker, {});
    await checkExtractedTar(targetUri);
    progressChecker.test(8);
  });
  it('ImplementsUnpackOptions', async () => {
    progressChecker.reset();
    const targetUri = local.tempFolderUri.join('example-tar-gz-transformed');
    await unpackTarGz(local.session, archiveUri, targetUri, progressChecker, transformedTarUnpackOptions);
    await checkExtractedTransformedTar(targetUri);
    progressChecker.test(4);
  });
});
