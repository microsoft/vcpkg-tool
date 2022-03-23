// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { FileType } from '@microsoft/vcpkg-ce/dist/fs/filesystem';
import { HttpsFileSystem } from '@microsoft/vcpkg-ce/dist/fs/http-filesystem';
import { fail, strict } from 'assert';
import { SuiteLocal } from './SuiteLocal';


describe('HttpFileSystemTests', () => {
  const local = new SuiteLocal();

  after(local.after.bind(local));
  const fs = new HttpsFileSystem(local.session);

  it('stat a file', async () => {

    const uri = fs.parse('https://aka.ms/vcpkg-ce.version');
    const s = await fs.stat(uri);
    strict.equal(s.type, FileType.File, 'Should be a file');
    strict.ok(s.size < 40, 'should be less than 40 bytes');
    strict.ok(s.size > 20, 'should be more than 20 bytes');

  });

  it('stat a non existant file', async () => {
    try {
      const uri = fs.parse('https://file.not.found/blabla');
      const s = await fs.stat(uri);
    } catch {
      return;
    }
    fail('Should have thrown');
  });

  it('read a stream', async () => {
    const uri = fs.parse('https://aka.ms/vcpkg-ce.version');

    let text = '';

    for await (const chunk of await fs.readStream(uri)) {
      text += chunk.toString('utf8');
    }
    strict.ok(text.length > 5, 'should have some text');
    strict.ok(text.length < 20, 'shouldnt have too much text');
  });
});
