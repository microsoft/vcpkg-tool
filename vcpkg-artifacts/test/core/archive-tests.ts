// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Uri } from '../../util/uri';
import { rejects, strict } from 'assert';
import { SuiteLocal } from './SuiteLocal';
import { vcpkgExtract } from '../../vcpkg';

const isWindows = process.platform === 'win32';

describe('ZipUnpacker', () => {
  const local = new SuiteLocal();
  after(local.after.bind(local));
  it('UnpacksLegitimateSmallZips', async () => {
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example');
    await vcpkgExtract(local.session, zipUri.path, targetUri.fsPath);
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
  });


  it('Truncates', async () => {
    const zipUri = local.resourcesFolderUri.join('example-zip.zip');
    const targetUri = local.tempFolderUri.join('example-truncates');
    await vcpkgExtract(local.session, zipUri.path, targetUri.fsPath);
    await vcpkgExtract(local.session, zipUri.path, targetUri.fsPath); // intentionally doubled
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
  });

  it('UnpacksZipsWithCompression', async () => {
    // big-compression.zip is an example input from yauzl:
    // https://github.com/thejoshwolfe/yauzl/blob/96f0eb552c560632a754ae0e1701a7edacbda389/test/big-compression.zip
    const zipUri = local.resourcesFolderUri.join('big-compression.zip');
    const targetUri = local.tempFolderUri.join('big-compression');
    await vcpkgExtract(local.session, zipUri.path, targetUri.fsPath); 
    const contents = await targetUri.readFile('0x100000');
    strict.equal(contents.length, 0x100000);
    strict.ok(contents.every((value: number) => value === 0x0));
  });

  it('FailsToUnpackMalformed', async () => {
    // wrong-entry-sizes.zip is an example input from yauzl:
    // https://github.com/thejoshwolfe/yauzl/blob/96f0eb552c560632a754ae0e1701a7edacbda389/test/wrong-entry-sizes/wrong-entry-sizes.zip
    const zipUri = local.resourcesFolderUri.join('wrong-entry-sizes.zip');
    const targetUri = local.tempFolderUri.join('wrong-entry-sizes');
    await rejects(vcpkgExtract(local.session, zipUri.path, targetUri.fsPath));
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
  const archiveUri = local.resourcesFolderUri.join('example-tar.tar');
  it('UnpacksLegitimateSmallTar', async () => {
    const targetUri = local.tempFolderUri.join('example-tar');
    await vcpkgExtract(local.session, archiveUri.path, targetUri.fsPath);
    await checkExtractedTar(targetUri);
  });
  it('ImplementsUnpackOptions', async () => {
    const targetUri = local.tempFolderUri.join('example-tar-transformed');
    await vcpkgExtract(local.session, archiveUri.path, targetUri.fsPath);
    await checkExtractedTransformedTar(targetUri);
  });
});

describe('TarBzUnpacker', () => {
  const local = new SuiteLocal();
  after(local.after.bind(local));
  const archiveUri = local.resourcesFolderUri.join('example-tar.tar.bz2');
  it('UnpacksLegitimateSmallTarBz', async () => {
    const targetUri = local.tempFolderUri.join('example-tar-bz');
    await vcpkgExtract(local.session, archiveUri.path, targetUri.fsPath);
    await checkExtractedTar(targetUri);
  });
  it('ImplementsUnpackOptions', async () => {
    const targetUri = local.tempFolderUri.join('example-tar-bz2-transformed');
    await vcpkgExtract(local.session, archiveUri.path, targetUri.fsPath);
    await checkExtractedTransformedTar(targetUri);
  });
});

describe('TarGzUnpacker', () => {
  const local = new SuiteLocal();
  after(local.after.bind(local));
  const archiveUri = local.resourcesFolderUri.join('example-tar.tar.gz');
  it('UnpacksLegitimateSmallTarGz', async () => {
    const targetUri = local.tempFolderUri.join('example-tar-gz');
    await vcpkgExtract(local.session, archiveUri.path, targetUri.path);
    await checkExtractedTar(targetUri);
  });
  it('ImplementsUnpackOptions', async () => {
    const targetUri = local.tempFolderUri.join('example-tar-gz-transformed');
    await vcpkgExtract(local.session, archiveUri.path, targetUri.path);
    await checkExtractedTransformedTar(targetUri);
  });
});
