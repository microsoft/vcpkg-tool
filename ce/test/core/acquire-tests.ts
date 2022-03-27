// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { acquireArtifactFile, resolveNugetUrl } from '@microsoft/vcpkg-ce/dist/fs/acquire';
import { strict } from 'assert';
import { SuiteLocal } from './SuiteLocal';

describe('Acquire', () => {
  const local = new SuiteLocal();
  const fs = local.fs;

  after(local.after.bind(local));

  it('try some downloads', async () => {

    const remoteFile = local.session.parseUri('https://raw.githubusercontent.com/microsoft/vscode/main/README.md');

    let acq = acquireArtifactFile(local.session, [remoteFile], 'readme.md', {});

    const outputFile = await acq;

    strict.ok(await outputFile.exists(), 'File should exist!');
    const size = await outputFile.size();
    // let's try some resume scenarios

    // chopped file, very small.
    // let's chop the file in half
    const fullFile = await outputFile.readFile();
    const halfFile = fullFile.slice(0, fullFile.length / 2);

    await outputFile.delete();
    await outputFile.writeFile(halfFile);

    local.session.channels.debug('==== chopped the file in half, redownload');

    acq = acquireArtifactFile(local.session, [remoteFile], 'readme.md', {});
    await acq;
    const newsize = await outputFile.size();
    strict.equal(newsize, size, 'the file should be the right size at the end');

  });


  it('larger file', async () => {
    const remoteFile = local.session.parseUri('https://user-images.githubusercontent.com/1487073/58344409-70473b80-7e0a-11e9-8570-b2efc6f8fa44.png');

    let acq = acquireArtifactFile(local.session, [remoteFile], 'xyz.png', {});

    const outputFile = await acq;

    const fullSize = await outputFile.size();

    strict.ok(await outputFile.exists(), 'File should exist!');
    strict.ok(fullSize > 1 << 16, 'Should be at least 64k');

    const size = await outputFile.size();


    // try getting the same file again (so, should hit the cache.)
    local.session.channels.debug('==== get the same large file again. should hit cache');
    await acquireArtifactFile(local.session, [remoteFile], 'xyz.png', {});

    local.session.channels.debug('==== was that ok?');

    // chopped file, big.
    // let's chop the file in half
    const fullFile = await outputFile.readFile();
    const halfFile = fullFile.slice(0, fullFile.length / 2);

    await outputFile.delete();
    await outputFile.writeFile(halfFile);

    local.session.channels.debug('==== chopped the large file in half, should resume');
    acq = acquireArtifactFile(local.session, [remoteFile], 'xyz.png', {});

    await acq;
    const newsize = await outputFile.size();
    strict.equal(newsize, size, 'the file should be the right size at the end');

    const newfull = <Buffer>(await outputFile.readFile());
    strict.equal(newfull.compare(fullFile), 0, 'files should be identical');
  });

  /**
  * The NuGet gallery servers don't do redirects on HEAD requests, and to work around it we have to issue a second GET
  * for each HEAD, after the HEAD fails, which increases the overhead of getting the target file (or verifying that we have it.)
  *
  * I've made the test call resolve redirects up front, which did reduce the cost, so... it's about as fast as I can make it.
  * (~400msec for the whole test, which ain't terrible.)
  *
  * The same thing can be accomplished by the all-encompassing nuget() call, but the test suffers if I use that directly, since we're
  * calling for the same package multple times. 🤷
  */
  it('Download a nuget file', async () => {
    const url = await resolveNugetUrl(local.session, 'zlib-msvc14-x64/1.2.11.7795');

    local.session.channels.debug('==== Downloading nuget package');

    const acq = acquireArtifactFile(local.session, [url], 'zlib-msvc.zip', {});
    // or const acq = nuget(local.session, 'zlib-msvc14-x64/1.2.11.7795', 'zlib-msvc.zip');

    const outputFile = await acq;
    local.session.channels.debug('==== done downloading');
    const fullSize = await outputFile.size();

    strict.ok(await outputFile.exists(), 'File should exist!');
    strict.ok(fullSize > 1 << 16, 'Should be at least 64k');

    const size = await outputFile.size();
    local.session.channels.debug(`==== Size: ${size}`);

    // what happens if we try again? We should hit our local cache
    await acquireArtifactFile(local.session, [url], 'zlib-msvc.zip', {});

  });

});
