// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { statSync } from 'fs';
import { rm } from 'fs/promises';
import { join, resolve } from 'path';
import { LocalFileSystem } from '../../fs/local-filesystem';
import { Session } from '../../session';
import { Uri } from '../../util/uri';
import { uniqueTempFolder } from './uniqueTempFolder';

// eslint-disable-next-line @typescript-eslint/no-require-imports
require('../../exports');

function resourcesFolder(from = __dirname): string {
  for (;;) {
    try {
      const resources = join(from, 'test-resources');
      const s = statSync(resources);
      s.isDirectory();
      return resources;
    }
    catch {
      // shh!
    }

    const up = resolve(from, '..');
    strict.notEqual(up, from, 'O_o unable to find root folder');
    from = up;
  }
}

export class SuiteLocal {
  readonly tempFolder = uniqueTempFolder();
  readonly session: Session;
  readonly fs: LocalFileSystem;
  readonly resourcesFolder = resourcesFolder();
  readonly tempFolderUri: Uri;
  readonly resourcesFolderUri: Uri;

  constructor() {
    this.tempFolder = uniqueTempFolder();
    this.session = new Session(this.tempFolder, <any>{}, {
      vcpkgCommand: undefined,
      homeFolder: join(this.tempFolder, 'vcpkg_root'),
      vcpkgArtifactsRoot: join(this.tempFolder, 'artifacts'),
      vcpkgDownloads: join(this.tempFolder, 'downloads'),
      vcpkgRegistriesCache: join(this.tempFolder, 'registries'),
    });

    this.fs = new LocalFileSystem(this.session);
    this.tempFolderUri = this.fs.file(this.tempFolder);
    this.resourcesFolderUri = this.fs.file(this.resourcesFolder);
    // set the debug=1 in the environment to have the debug messages dumped during testing
    if (process.env['DEBUG'] || process.env['debug']) {
      this.session.channels.on('debug', (text, msec) => {
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
