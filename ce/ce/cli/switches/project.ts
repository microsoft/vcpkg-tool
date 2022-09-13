// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { resolve } from 'path';
import { ProjectManifest } from '../../artifacts/artifact';
import { configurationName } from '../../constants';
import { FileType } from '../../fs/filesystem';
import { i } from '../../i18n';
import { session } from '../../main';
import { Uri } from '../../util/uri';
import { projectFile } from '../format';
import { debug, error } from '../styling';
import { Switch } from '../switch';

interface ResolvedProjectUri {
  filename: string;
  uri: Uri;
}

export class Project extends Switch {
  switch = 'project';
  get help() {
    return [
      i`override the path to the project folder`
    ];
  }

  async resolveProjectUri() : Promise<ResolvedProjectUri | undefined> {
    const v = super.value;
    if (v) {
      const uri = session.fileSystem.file(resolve(v));
      const stat = await uri.stat();

      if (stat.type & FileType.File) {
        return {'filename': v, uri: uri};
      }
      if (stat.type & FileType.Directory) {
        const project = uri.join(configurationName);
        if (await project.exists()) {
          return {'filename': project.fsPath, uri: project};
        }
      }

      error(i`Unable to find project environment ${projectFile(uri)}`);
      return undefined;
    }

    const sessionProject = await session.findProjectProfile();
    if (sessionProject) {
      return {'filename': sessionProject.fsPath, 'uri': sessionProject};
    }

    return undefined;
  }

  override get value(): Promise<Uri | undefined> {
    return this.resolveProjectUri().then(v => v?.uri);
  }

  get manifest(): Promise<ProjectManifest | undefined> {
    return this.resolveProjectUri().then(async (resolved) => {
      if (!resolved) {
        debug('No project manifest');
        return undefined;
      }

      debug(`Loading project manifest ${resolved.filename} `);
      return await new ProjectManifest(session, await session.openManifest(resolved.filename, resolved.uri));
    });
  }
}
