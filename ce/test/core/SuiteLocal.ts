// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { LocalFileSystem } from '@microsoft/vcpkg-ce/dist/fs/local-filesystem';
import { Session } from '@microsoft/vcpkg-ce/dist/session';
import { Uri } from '@microsoft/vcpkg-ce/dist/util/uri';
import { strict } from 'assert';
import { statSync } from 'fs';
import { rm } from 'fs/promises';
import { join, resolve } from 'path';
import { uniqueTempFolder } from './uniqueTempFolder';


require('@microsoft/vcpkg-ce/dist/exports');

export function rootFolder(from = __dirname): string {
  try {
    const resources = join(from, 'resources');
    const s = statSync(resources);
    s.isDirectory();
    return from;
  }
  catch {
    // shh!
  }
  const up = resolve(from, '..');
  strict.notEqual(up, from, 'O_o unable to find root folder');
  return rootFolder(up);
}

export class SuiteLocal {
  readonly tempFolder = uniqueTempFolder();
  readonly session: Session;
  readonly fs: LocalFileSystem;
  readonly rootFolder: string = rootFolder();
  readonly resourcesFolder = this.rootFolder + '/resources';
  readonly rootFolderUri: Uri;
  readonly tempFolderUri: Uri;
  readonly resourcesFolderUri: Uri;

  constructor() {
    this.tempFolder = uniqueTempFolder();
    this.session = new Session(this.tempFolder, <any>{}, {
      homeFolder: join(this.tempFolder, 'vcpkg_root'),
    }, {});

    this.fs = new LocalFileSystem(this.session);
    this.rootFolderUri = this.fs.file(this.rootFolder);
    this.tempFolderUri = this.fs.file(this.tempFolder);
    this.resourcesFolderUri = this.fs.file(this.resourcesFolder);
    // set the debug=1 in the environment to have the debug messages dumped during testing
    if (process.env['DEBUG'] || process.env['debug']) {
      this.session.channels.on('debug', (text, context, msec) => {
        SuiteLocal.log(`[${msec}msec] ${text}`);
      });
    }
  }

  async after() {
    await rm(this.tempFolder, { recursive: true });
  }
  static log(args: any) {
    console['log'](args);
  }
}
